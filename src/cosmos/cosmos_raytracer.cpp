#include "cosmos/cosmos_raytracer.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>

// ── GPU data layout (must match shader) ─────────────────────────────────────

struct alignas(16) CameraUBOData {
    glm::mat4 inv_vp;           // 64 bytes
    glm::vec4 eye_pos;          // 16 bytes
    glm::vec4 screen_info;      // 16 bytes (w,h,count,time)
    glm::vec4 lighting_params;  // 16 bytes (star,uniform,ambient,fastStar)
};                              // Total: 112 bytes

struct SphereGPU {
    glm::vec4 pos_radius;   // xyz = position, w = radius
    glm::vec4 color_emit;   // rgb = base color (0-1), a = emissive
    glm::vec4 planet_data;  // x = seed, y = surface_type, z = ocean_coverage(0-1), w = temperature
    glm::vec4 atmo_data;    // x = cloud_coverage(0-1), y = atm_pressure, z = vegetation(0-1), w = body_flags
};

// body_flags bits: 0=is_planet, 1=is_moon, 2=has_atmosphere, 3=ocean_type(2 bits)

// ── Body color helper (matches cosmos_app.cpp) ──────────────────────────────

static glm::vec3 body_color_vec3(const CelestialBody& b) {
    if (is_star_type(b.type)) {
        float t = std::clamp((b.temperature - 2000.0f) / 30000.0f, 0.0f, 1.0f);
        float r = std::clamp(1.4f - t * 1.2f, 0.0f, 1.0f);
        float g = std::clamp(0.8f + t * 0.2f - std::abs(t - 0.4f), 0.0f, 1.0f);
        float bl = std::clamp(t * 1.8f - 0.3f, 0.0f, 1.0f);
        return {r, g, std::min(bl, 1.0f)};
    }
    if (is_black_hole_type(b.type))
        return {0.078f, 0.0f, 0.157f};
    switch (b.type) {
    case CTYPE_PLANET:     return {0.235f, 0.549f, 0.863f};
    case CTYPE_MOON:       return {0.706f, 0.706f, 0.745f};
    case CTYPE_ASTEROID:   return {0.549f, 0.510f, 0.431f};
    case CTYPE_COMET:      return {0.627f, 0.863f, 1.0f};
    case CTYPE_NEBULA:     return {0.471f, 0.235f, 0.706f};
    default:               return {0.784f, 0.784f, 0.784f};
    }
}

// ── Init ────────────────────────────────────────────────────────────────────

void CosmosRaytracer::init(VulkanContext& vk, VkRenderPass render_pass) {
    // ── Descriptor set layout ──────────────────────────────────────────────
    VkDescriptorSetLayoutBinding bindings[2]{};
    // Binding 0: Camera UBO
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Binding 1: Sphere SSBO
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_ci.bindingCount = 2;
    layout_ci.pBindings    = bindings;
    vkCreateDescriptorSetLayout(vk.device, &layout_ci, nullptr, &desc_layout_);

    // ── Pipeline layout ────────────────────────────────────────────────────
    VkPipelineLayoutCreateInfo pipe_layout_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipe_layout_ci.setLayoutCount = 1;
    pipe_layout_ci.pSetLayouts    = &desc_layout_;
    vkCreatePipelineLayout(vk.device, &pipe_layout_ci, nullptr, &pipe_layout_);

    // ── Shader modules ─────────────────────────────────────────────────────
    VkShaderModule vert_mod = vk.create_shader_module("shaders/cosmos_rt.vert.spv");
    VkShaderModule frag_mod = vk.create_shader_module("shaders/cosmos_rt.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName  = "main";

    // ── Vertex input (none — fullscreen triangle) ──────────────────────────
    VkPipelineVertexInputStateCreateInfo vert_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo input_asm{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ── Viewport / scissor (dynamic) ───────────────────────────────────────
    VkPipelineViewportStateCreateInfo vp_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp_state.viewportCount = 1;
    vp_state.scissorCount  = 1;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_ci{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates    = dyn_states;

    // ── Rasterizer ─────────────────────────────────────────────────────────
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    // ── Multisampling (off) ────────────────────────────────────────────────
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Depth/stencil (off) ────────────────────────────────────────────────
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    // ── Color blending (opaque write) ──────────────────────────────────────
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    // ── Create pipeline ────────────────────────────────────────────────────
    VkGraphicsPipelineCreateInfo pipe_ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipe_ci.stageCount          = 2;
    pipe_ci.pStages             = stages;
    pipe_ci.pVertexInputState   = &vert_input;
    pipe_ci.pInputAssemblyState = &input_asm;
    pipe_ci.pViewportState      = &vp_state;
    pipe_ci.pRasterizationState = &raster;
    pipe_ci.pMultisampleState   = &ms;
    pipe_ci.pDepthStencilState  = &depth;
    pipe_ci.pColorBlendState    = &blend;
    pipe_ci.pDynamicState       = &dyn_ci;
    pipe_ci.layout              = pipe_layout_;
    pipe_ci.renderPass          = render_pass;
    pipe_ci.subpass             = 0;

    vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipe_ci, nullptr, &pipeline_);

    vkDestroyShaderModule(vk.device, vert_mod, nullptr);
    vkDestroyShaderModule(vk.device, frag_mod, nullptr);

    // ── Buffers ────────────────────────────────────────────────────────────
    camera_ubo_ = vk.create_buffer(
        sizeof(CameraUBOData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    sphere_ssbo_ = vk.create_buffer(
        MAX_SPHERES * sizeof(SphereGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // ── Descriptor pool + set ──────────────────────────────────────────────
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  1},
    };
    VkDescriptorPoolCreateInfo dp_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp_ci.maxSets       = 1;
    dp_ci.poolSizeCount = 2;
    dp_ci.pPoolSizes    = pool_sizes;
    vkCreateDescriptorPool(vk.device, &dp_ci, nullptr, &desc_pool_);

    VkDescriptorSetAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_ci.descriptorPool     = desc_pool_;
    alloc_ci.descriptorSetCount = 1;
    alloc_ci.pSetLayouts        = &desc_layout_;
    vkAllocateDescriptorSets(vk.device, &alloc_ci, &desc_set_);

    // Write descriptors
    VkDescriptorBufferInfo ubo_info{};
    ubo_info.buffer = camera_ubo_.handle;
    ubo_info.offset = 0;
    ubo_info.range  = sizeof(CameraUBOData);

    VkDescriptorBufferInfo ssbo_info{};
    ssbo_info.buffer = sphere_ssbo_.handle;
    ssbo_info.offset = 0;
    ssbo_info.range  = MAX_SPHERES * sizeof(SphereGPU);

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = desc_set_;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &ubo_info;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = desc_set_;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo     = &ssbo_info;

    vkUpdateDescriptorSets(vk.device, 2, writes, 0, nullptr);
}

// ── Destroy ─────────────────────────────────────────────────────────────────

void CosmosRaytracer::destroy(VulkanContext& vk) {
    vkDestroyPipeline(vk.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(vk.device, pipe_layout_, nullptr);
    vkDestroyDescriptorPool(vk.device, desc_pool_, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, desc_layout_, nullptr);
    vk.destroy_buffer(camera_ubo_);
    vk.destroy_buffer(sphere_ssbo_);
}

// ── Update + draw ───────────────────────────────────────────────────────────

void CosmosRaytracer::update_and_draw(VulkanContext& vk, VkCommandBuffer cmd,
                                       const CosmosState& state,
                                       const OrbitCamera& camera,
                                       const CosmosConfig& cfg,
                                       float screen_w, float screen_h,
                                       float time) {
    float aspect = screen_w / screen_h;
    glm::mat4 view = camera.view_matrix();
    glm::mat4 proj = camera.proj_matrix(aspect);
    glm::mat4 vp   = proj * view;

    // ── Upload camera UBO ──────────────────────────────────────────────────
    CameraUBOData cam{};
    cam.inv_vp         = glm::inverse(vp);
    cam.eye_pos        = glm::vec4(camera.eye_position(), 0.0f);
    cam.screen_info    = glm::vec4(screen_w, screen_h,
                                    (float)std::min((int)state.bodies.size(), MAX_SPHERES),
                                    time);
    // Auto-enable uniform lighting if star_lighting is on but no stars exist,
    // otherwise planets are nearly invisible (only 8% ambient).
    bool has_stars = false;
    if (cfg.star_lighting) {
        for (auto& b : state.bodies) {
            if (is_star_type(b.type)) { has_stars = true; break; }
        }
    }
    bool effective_uniform = cfg.uniform_lighting || (cfg.star_lighting && !has_stars);

    cam.lighting_params = glm::vec4(
        (cfg.star_lighting && has_stars) ? 1.0f : 0.0f,
        effective_uniform ? 1.0f : 0.0f,
        cfg.ambient_strength,
        cfg.fast_star_lighting ? 1.0f : 0.0f);

    void* mapped = nullptr;
    vkMapMemory(vk.device, camera_ubo_.memory, 0, sizeof(CameraUBOData), 0, &mapped);
    memcpy(mapped, &cam, sizeof(CameraUBOData));
    vkUnmapMemory(vk.device, camera_ubo_.memory);

    // ── Upload sphere SSBO ─────────────────────────────────────────────────
    int n = std::min((int)state.bodies.size(), MAX_SPHERES);
    static thread_local std::vector<SphereGPU> spheres;
    spheres.resize(n);
    for (int i = 0; i < n; i++) {
        const auto& b = state.bodies[i];
        glm::vec3 col = body_color_vec3(b);
        float emissive = 0.0f;
        if (is_star_type(b.type))       emissive = 1.5f;
        if (is_black_hole_type(b.type)) emissive = -1.0f;

        spheres[i].pos_radius = glm::vec4(b.pos, b.radius);
        spheres[i].color_emit = glm::vec4(col, emissive);

        // Normalize seed to shader-friendly range: raw uint32 can be ~4 billion,
        // far beyond float32 precision for noise functions using fract().
        // Pack into [0, 10000) so seed*0.01 stays in [0, 100).
        float shader_seed = (float)(b.seed % 10000u) + (float)((b.seed >> 16) & 0xFFu) * 0.001f;

        // Pack body data for shader texturing / shape hints.        
        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            const PlanetProperties& pp = b.cached_props;

            float body_flags = 0.0f;
            if (b.type == CTYPE_PLANET) body_flags += 1.0f;
            if (b.type == CTYPE_MOON)   body_flags += 2.0f;
            if (pp.atmosphere.pressure > 0.01f) body_flags += 4.0f;
            body_flags += (float)pp.ocean_type * 8.0f;

            // Normalize seed to shader-friendly range: raw uint32 can be ~4 billion,
            // far beyond float32 precision for noise functions using fract().
            // Pack into [0, 10000) so seed*0.01 stays in [0, 100).
            float shader_seed = (float)(b.seed % 10000u) + (float)((b.seed >> 16) & 0xFFu) * 0.001f;

            spheres[i].planet_data = glm::vec4(
                shader_seed,
                (float)pp.surface,
                pp.ocean_coverage / 100.0f,
                b.temperature);
            spheres[i].atmo_data = glm::vec4(
                pp.cloud_coverage / 100.0f,
                pp.atmosphere.pressure,
                pp.vegetation_coverage / 100.0f,
                body_flags);
        } else {
            float body_flags = 0.0f;
            if (b.type == CTYPE_ASTEROID) body_flags += 64.0f;
            if (b.type == CTYPE_COMET)    body_flags += 128.0f;

            spheres[i].planet_data = glm::vec4(shader_seed, 0.0f, 0.0f, b.temperature);
            spheres[i].atmo_data   = glm::vec4(0.0f, 0.0f, 0.0f, body_flags);

        }
    }

    if (n > 0) {
        vkMapMemory(vk.device, sphere_ssbo_.memory, 0,
                    n * sizeof(SphereGPU), 0, &mapped);
        memcpy(mapped, spheres.data(), n * sizeof(SphereGPU));
        vkUnmapMemory(vk.device, sphere_ssbo_.memory);
    }

    // ── Draw fullscreen triangle ───────────────────────────────────────────
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width    = screen_w;
    viewport.height   = screen_h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width  = (uint32_t)screen_w;
    scissor.extent.height = (uint32_t)screen_h;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipe_layout_, 0, 1, &desc_set_, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}
