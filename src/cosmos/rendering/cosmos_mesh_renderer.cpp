#include "cosmos/rendering/cosmos_mesh_renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>

// ── Body color helper (matches cosmos_raytracer.cpp) ────────────────────────

static glm::vec3 blackbody_tint(float temperature) {
    float t = std::clamp((temperature - 1200.0f) / 32000.0f, 0.0f, 1.0f);
    return glm::clamp(glm::vec3(
        1.25f - t * 0.95f,
        0.45f + t * 0.6f,
        -0.1f + t * 1.25f), glm::vec3(0.0f), glm::vec3(1.0f));
}

static glm::vec3 mesh_body_color(const CelestialBody& b) {
    if (is_star_type(b.type))
        return blackbody_tint(b.temperature);
    if (is_black_hole_type(b.type))
        return {0.0f, 0.0f, 0.0f};
    switch (b.type) {
    case CTYPE_PLANET:   return {0.235f, 0.549f, 0.863f};
    case CTYPE_MOON:     return {0.706f, 0.706f, 0.745f};
    case CTYPE_ASTEROID: return {0.549f, 0.510f, 0.431f};
    case CTYPE_COMET:    return {0.627f, 0.863f, 1.0f};
    default:             return {0.784f, 0.784f, 0.784f};
    }
}

// ── Init ────────────────────────────────────────────────────────────────────

void CosmosMeshRenderer::init(VulkanContext& vk, VkRenderPass render_pass) {
    // ── Pipeline layout (push constants only, no descriptor sets) ────────
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges    = &pc_range;
    vkCreatePipelineLayout(vk.device, &layout_ci, nullptr, &pipe_layout_);

    // ── Shader modules ──────────────────────────────────────────────────
    VkShaderModule vert_mod = vk.create_shader_module("shaders/cosmos/cosmos_terrain.vert.spv");
    VkShaderModule frag_mod = vk.create_shader_module("shaders/cosmos/cosmos_terrain.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName  = "main";

    // ── Vertex input (matches QuadSphereVertex layout) ──────────────────
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(QuadSphereVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    // location 0: position (vec3, offset 0)
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(QuadSphereVertex, position);
    // location 1: normal (vec3, offset 12)
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(QuadSphereVertex, normal);
    // location 2: uv (vec2, offset 24)
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset   = offsetof(QuadSphereVertex, uv);

    VkPipelineVertexInputStateCreateInfo vert_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vert_input.vertexBindingDescriptionCount   = 1;
    vert_input.pVertexBindingDescriptions      = &binding;
    vert_input.vertexAttributeDescriptionCount = 3;
    vert_input.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_asm{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ── Viewport / scissor (dynamic) ────────────────────────────────────
    VkPipelineViewportStateCreateInfo vp_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp_state.viewportCount = 1;
    vp_state.scissorCount  = 1;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_ci{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates    = dyn_states;

    // ── Rasterizer: back-face culling for performance ───────────────────
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    // ── Multisampling (off) ─────────────────────────────────────────────
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Depth testing: ON, write: ON ────────────────────────────────────
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable  = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp   = VK_COMPARE_OP_LESS;
    depth.minDepthBounds   = 0.0f;
    depth.maxDepthBounds   = 1.0f;

    // ── Color blending: opaque ──────────────────────────────────────────
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    // ── Create pipeline ─────────────────────────────────────────────────
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

    // ── Allocate GPU buffers (HOST_VISIBLE for simplicity) ──────────────
    vertex_buffer_ = vk.create_buffer(
        MAX_VERTICES * sizeof(QuadSphereVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    index_buffer_ = vk.create_buffer(
        MAX_INDICES * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

// ── Destroy ─────────────────────────────────────────────────────────────────

void CosmosMeshRenderer::destroy(VulkanContext& vk) {
    vkDestroyPipeline(vk.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(vk.device, pipe_layout_, nullptr);
    vk.destroy_buffer(vertex_buffer_);
    vk.destroy_buffer(index_buffer_);
    pipeline_ = VK_NULL_HANDLE;
    pipe_layout_ = VK_NULL_HANDLE;
}

// ── Upload ──────────────────────────────────────────────────────────────────

void CosmosMeshRenderer::upload_meshes(VulkanContext& vk,
                                        const std::vector<TerrainMeshEntry>& cache,
                                        const std::vector<CelestialBody>& bodies) {
    draw_entries_.clear();
    total_vertices_ = 0;
    total_indices_  = 0;

    // First pass: compute total sizes and validate
    uint32_t vert_total = 0;
    uint32_t idx_total  = 0;
    for (size_t i = 0; i < cache.size() && i < bodies.size(); ++i) {
        if (!cache[i].valid) continue;
        const auto& mesh = cache[i].mesh;
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;

        uint32_t nv = static_cast<uint32_t>(mesh.vertices.size());
        uint32_t ni = static_cast<uint32_t>(mesh.indices.size());
        if (vert_total + nv > MAX_VERTICES || idx_total + ni > MAX_INDICES)
            break; // budget exceeded — stop adding bodies

        DrawEntry entry;
        entry.vertex_offset = vert_total;
        entry.index_offset  = idx_total;
        entry.index_count   = ni;
        entry.body_index    = static_cast<int>(i);
        draw_entries_.push_back(entry);

        vert_total += nv;
        idx_total  += ni;
    }

    total_vertices_ = vert_total;
    total_indices_  = idx_total;

    if (vert_total == 0) return;

    // Upload vertex data
    void* mapped = nullptr;
    vkMapMemory(vk.device, vertex_buffer_.memory, 0,
                vert_total * sizeof(QuadSphereVertex), 0, &mapped);
    if (mapped) {
        uint32_t offset = 0;
        for (const auto& entry : draw_entries_) {
            const auto& mesh = cache[entry.body_index].mesh;
            memcpy(static_cast<char*>(mapped) + offset * sizeof(QuadSphereVertex),
                   mesh.vertices.data(),
                   mesh.vertices.size() * sizeof(QuadSphereVertex));
            offset += static_cast<uint32_t>(mesh.vertices.size());
        }
        vkUnmapMemory(vk.device, vertex_buffer_.memory);
    }

    // Upload index data
    mapped = nullptr;
    vkMapMemory(vk.device, index_buffer_.memory, 0,
                idx_total * sizeof(uint32_t), 0, &mapped);
    if (mapped) {
        uint32_t offset = 0;
        for (const auto& entry : draw_entries_) {
            const auto& mesh = cache[entry.body_index].mesh;
            memcpy(static_cast<char*>(mapped) + offset * sizeof(uint32_t),
                   mesh.indices.data(),
                   mesh.indices.size() * sizeof(uint32_t));
            offset += static_cast<uint32_t>(mesh.indices.size());
        }
        vkUnmapMemory(vk.device, index_buffer_.memory);
    }
}

// ── Draw ────────────────────────────────────────────────────────────────────

void CosmosMeshRenderer::draw(VkCommandBuffer cmd,
                               const std::vector<CelestialBody>& bodies,
                               const OrbitCamera& camera,
                               float screen_w, float screen_h,
                               float sim_time) {
    if (draw_entries_.empty() || total_vertices_ == 0) return;

    // Camera setup (camera-relative rendering, matching the raytracer)
    glm::dvec3 target_origin = glm::dvec3(camera.target);
    glm::dvec3 eye_rel = camera.eye_position_d() - target_origin;
    float aspect = screen_w / screen_h;
    glm::dmat4 view = glm::lookAt(eye_rel, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dmat4 proj = camera.proj_matrix_d(aspect);
    proj[1][1] *= -1.0; // Vulkan Y-flip (GLM uses OpenGL convention)
    glm::dmat4 vp   = proj * view;

    float fov_rad = glm::radians(camera.fov);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Dynamic viewport + scissor
    VkViewport viewport{};
    viewport.width    = screen_w;
    viewport.height   = screen_h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width  = static_cast<uint32_t>(screen_w);
    scissor.extent.height = static_cast<uint32_t>(screen_h);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind VBO + IBO
    VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_.handle, &vb_offset);
    vkCmdBindIndexBuffer(cmd, index_buffer_.handle, 0, VK_INDEX_TYPE_UINT32);

    // Draw each body
    for (const auto& entry : draw_entries_) {
        if (entry.body_index < 0 || entry.body_index >= static_cast<int>(bodies.size()))
            continue;
        const auto& body = bodies[entry.body_index];

        // Screen-size gating: skip bodies too small to see terrain detail
        glm::dvec3 body_cam_rel = glm::dvec3(body.pos) - target_origin;
        double dist = glm::length(body_cam_rel - eye_rel);
        if (dist < 0.01) dist = 0.01;
        float screen_diam = (2.0f * body.radius / static_cast<float>(dist)) *
                            (screen_h / (2.0f * std::tan(fov_rad * 0.5f)));
        if (screen_diam < min_screen_pixels)
            continue;

        // Frustum culling: check if body sphere is in front of camera
        glm::dvec3 to_body = body_cam_rel - eye_rel;
        glm::dvec3 cam_fwd = glm::normalize(-eye_rel); // camera looks toward origin
        double behind = glm::dot(to_body, cam_fwd);
        if (behind < -(double)body.radius)
            continue; // entirely behind camera

        // Model matrix: translate body to camera-relative position
        glm::dmat4 model = glm::translate(glm::dmat4(1.0), body_cam_rel);
        glm::mat4 mvp = glm::mat4(vp * model);

        // Build push constants
        PushConstants pc{};
        pc.mvp          = mvp;
        pc.cam_pos_time = glm::vec4(glm::vec3(eye_rel), sim_time);
        pc.body_params  = glm::vec4(
            static_cast<float>(body.cached_visuals.render_class),
            body.temperature,
            body.cached_props.ocean_coverage / 100.0f,
            body.radius);

        glm::vec3 col = mesh_body_color(body);
        float emissive = is_star_type(body.type) ? 1.4f : 0.0f;
        pc.color_params = glm::vec4(col, emissive);

        pc.surface_params = glm::vec4(
            body.cached_visuals.terrain_amp,
            body.cached_visuals.ice_sheet_coverage,
            body.cached_visuals.rock_frac,
            body.cached_props.ocean_coverage / 100.0f);

        vkCmdPushConstants(cmd, pipe_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);

        vkCmdDrawIndexed(cmd, entry.index_count, 1,
                         entry.index_offset, static_cast<int32_t>(entry.vertex_offset), 0);
    }
}
