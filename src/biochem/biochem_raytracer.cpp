#include "biochem/biochem_raytracer.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>

// ── GPU data layout (must match biochem_rt.frag) ────────────────────────────

struct alignas(16) BioCameraUBOData {
    glm::mat4 inv_vp;             // 64 bytes
    glm::vec4 eye_pos;            // 16 bytes
    glm::vec4 screen_info;        // 16 bytes (w,h,count,time)
    glm::vec4 lighting_params;    // 16 bytes (unused,unused,ambient,unused)
    glm::vec4 environment_color;  // 16 bytes (rgb = tint, a = haze density)
    glm::vec4 environment_factors;// 16 bytes (oxygen,nutrients,pH,toxicity)
};                                // Total: 144 bytes

struct BioSphereGPU {
    glm::vec4 pos_radius;    // xyz = position, w = bounding radius
    glm::vec4 axis_morph;    // xyz = orientation axis, w = morphology
    glm::vec4 color_type;    // rgb = base color (0-1), a = entity type
    glm::vec4 shape_params;  // x = aspect, y = noise, z = phase, w = reserved
};

struct BioEnvironmentFeatureGPU {
    glm::vec4 pos_radius;     // xyz = world position, w = radius
    glm::vec4 axis_strength;  // xyz = dominant direction, w = strength
    glm::vec4 tint_type;      // rgb = tint, a = feature type
    glm::vec4 meta;           // x = falloff, y = noise, z = unused, w = unused
};

// ── Entity type colors (0-1 range) ─────────────────────────────────────────

static glm::vec3 bio_type_color(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return {0.275f, 0.627f, 1.0f};    // blue
    case BIO_BACTERIUM:   return {0.902f, 0.588f, 0.196f};  // orange
    case BIO_VIRUS:       return {0.863f, 0.196f, 0.196f};  // red
    case BIO_NUTRIENT:    return {0.314f, 0.863f, 0.314f};  // green
    case BIO_TOXIN:       return {0.784f, 0.196f, 0.784f};  // purple
    case BIO_ANTIBODY:    return {1.0f,   1.0f,   0.275f};  // yellow
    case BIO_RED_BLOOD:   return {0.863f, 0.275f, 0.275f};  // red
    case BIO_WHITE_BLOOD: return {0.941f, 0.941f, 1.0f};    // white
    default:              return {0.7f,   0.7f,   0.7f};
    }
}

// ── Init ────────────────────────────────────────────────────────────────────

void BiochemRaytracer::init(VulkanContext& vk, VkRenderPass render_pass) {
    // ── Descriptor set layout ──────────────────────────────────────────────
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_ci.bindingCount = 3;
    layout_ci.pBindings    = bindings;
    vkCreateDescriptorSetLayout(vk.device, &layout_ci, nullptr, &desc_layout_);

    // ── Pipeline layout ────────────────────────────────────────────────────
    VkPipelineLayoutCreateInfo pipe_layout_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipe_layout_ci.setLayoutCount = 1;
    pipe_layout_ci.pSetLayouts    = &desc_layout_;
    vkCreatePipelineLayout(vk.device, &pipe_layout_ci, nullptr, &pipe_layout_);

    // ── Shader modules ─────────────────────────────────────────────────────
    VkShaderModule vert_mod = vk.create_shader_module("shaders/biochem_rt.vert.spv");
    VkShaderModule frag_mod = vk.create_shader_module("shaders/biochem_rt.frag.spv");

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

    vkCreateGraphicsPipelines(vk.device, vk.pipeline_cache, 1, &pipe_ci, nullptr, &pipeline_);

    vkDestroyShaderModule(vk.device, vert_mod, nullptr);
    vkDestroyShaderModule(vk.device, frag_mod, nullptr);

    // ── Buffers ────────────────────────────────────────────────────────────
    camera_ubo_ = vk.create_buffer(
        sizeof(BioCameraUBOData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    sphere_ssbo_ = vk.create_buffer(
        MAX_SPHERES * sizeof(BioSphereGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    feature_ssbo_ = vk.create_buffer(
        MAX_ENV_FEATURES * sizeof(BioEnvironmentFeatureGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // ── Descriptor pool + set ──────────────────────────────────────────────
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  1},
    };
    VkDescriptorPoolCreateInfo dp_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp_ci.maxSets       = 1;
    dp_ci.poolSizeCount = 3;
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
    ubo_info.range  = sizeof(BioCameraUBOData);

    VkDescriptorBufferInfo ssbo_info{};
    ssbo_info.buffer = sphere_ssbo_.handle;
    ssbo_info.offset = 0;
    ssbo_info.range  = MAX_SPHERES * sizeof(BioSphereGPU);

    VkDescriptorBufferInfo feature_info{};
    feature_info.buffer = feature_ssbo_.handle;
    feature_info.offset = 0;
    feature_info.range  = MAX_ENV_FEATURES * sizeof(BioEnvironmentFeatureGPU);

    VkWriteDescriptorSet writes[3]{};
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

    writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet          = desc_set_;
    writes[2].dstBinding      = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo     = &feature_info;

    vkUpdateDescriptorSets(vk.device, 3, writes, 0, nullptr);
}

// ── Destroy ─────────────────────────────────────────────────────────────────

void BiochemRaytracer::destroy(VulkanContext& vk) {
    vkDestroyPipeline(vk.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(vk.device, pipe_layout_, nullptr);
    vkDestroyDescriptorPool(vk.device, desc_pool_, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, desc_layout_, nullptr);
    vk.destroy_buffer(camera_ubo_);
    vk.destroy_buffer(sphere_ssbo_);
    vk.destroy_buffer(feature_ssbo_);
}

// ── Update + draw ───────────────────────────────────────────────────────────

void BiochemRaytracer::update_and_draw(VulkanContext& vk, VkCommandBuffer cmd,
                                        const BiochemState& state,
                                        const BiochemEnvironment& environment,
                                        const OrbitCamera& camera,
                                        const BiochemConfig& cfg,
                                        float screen_w, float screen_h,
                                        float time) {
    float aspect = screen_w / screen_h;
    glm::mat4 view = camera.view_matrix();
    glm::mat4 proj = camera.proj_matrix(aspect);
    glm::mat4 vp   = proj * view;

    // Count alive entities for upload
    int alive_count = 0;
    for (const auto& e : state.entities)
        if (e.alive) alive_count++;
    int n = std::min(alive_count, MAX_SPHERES);
    int feature_count = std::min(static_cast<int>(environment.features.size()), MAX_ENV_FEATURES);

    // ── Upload camera UBO ──────────────────────────────────────────────────
    BioCameraUBOData cam{};
    cam.inv_vp         = glm::inverse(vp);
    cam.eye_pos        = glm::vec4(camera.eye_position(), 0.0f);
    cam.screen_info    = glm::vec4(screen_w, screen_h, (float)n, time);
    cam.lighting_params = glm::vec4((float)feature_count, 0.0f, cfg.ambient_strength, 0.0f);
    cam.environment_color = glm::vec4(cfg.environment_tint,
        0.010f + cfg.flow_strength * 0.0007f + cfg.nutrient_density * 0.0025f);
    cam.environment_factors = glm::vec4(
        cfg.oxygen_level,
        cfg.nutrient_density,
        cfg.acidity_ph,
        cfg.toxicity);

    void* mapped = nullptr;
    vkMapMemory(vk.device, camera_ubo_.memory, 0, sizeof(BioCameraUBOData), 0, &mapped);
    memcpy(mapped, &cam, sizeof(BioCameraUBOData));
    vkUnmapMemory(vk.device, camera_ubo_.memory);

    // ── Upload sphere SSBO ─────────────────────────────────────────────────
    std::vector<BioSphereGPU> spheres;
    spheres.reserve(n);
    for (const auto& e : state.entities) {
        if (!e.alive) continue;
        if ((int)spheres.size() >= MAX_SPHERES) break;

        glm::vec3 col = bio_type_color(e.type);
        BioSphereGPU s;
        s.pos_radius = glm::vec4(e.pos, e.radius);
        s.axis_morph = glm::vec4(e.axis, (float)e.morphology);
        s.color_type = glm::vec4(col, (float)e.type);
        s.shape_params = glm::vec4(e.shape_aspect, e.shape_noise, e.shape_phase, 0.0f);
        spheres.push_back(s);
    }

    if (!spheres.empty()) {
        vkMapMemory(vk.device, sphere_ssbo_.memory, 0,
                    spheres.size() * sizeof(BioSphereGPU), 0, &mapped);
        memcpy(mapped, spheres.data(), spheres.size() * sizeof(BioSphereGPU));
        vkUnmapMemory(vk.device, sphere_ssbo_.memory);
    }

    std::vector<BioEnvironmentFeatureGPU> features;
    features.reserve(feature_count);
    for (const auto& feature : environment.features) {
        if ((int)features.size() >= MAX_ENV_FEATURES) break;

        BioEnvironmentFeatureGPU gpu{};
        gpu.pos_radius = glm::vec4(feature.pos, feature.radius);
        gpu.axis_strength = glm::vec4(feature.axis, feature.strength);
        gpu.tint_type = glm::vec4(feature.tint, (float)feature.type);
        gpu.meta = glm::vec4(feature.falloff, feature.noise, 0.0f, 0.0f);
        features.push_back(gpu);
    }

    if (!features.empty()) {
        vkMapMemory(vk.device, feature_ssbo_.memory, 0,
                    features.size() * sizeof(BioEnvironmentFeatureGPU), 0, &mapped);
        memcpy(mapped, features.data(), features.size() * sizeof(BioEnvironmentFeatureGPU));
        vkUnmapMemory(vk.device, feature_ssbo_.memory);
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
