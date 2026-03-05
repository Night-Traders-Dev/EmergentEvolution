#include "cosmos/cosmos_raytracer.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <glm/gtc/matrix_inverse.hpp>

// ── GPU data layout (must match shader) ─────────────────────────────────────

struct alignas(16) CameraUBOData {
    glm::mat4 inv_vp;           // 64 bytes
    glm::vec4 eye_pos;          // 16 bytes
    glm::vec4 screen_info;      // 16 bytes (w,h,count,time)
    glm::vec4 lighting_params;  // 16 bytes (star,uniform,ambient,fastStar)
    glm::vec4 quality_params;   // 16 bytes (quality,hq,bgPreset,simTime)
    glm::vec4 render_flags;     // 16 bytes (bg,corona,cometTails,bhLensing)
    glm::vec4 fabric_params;    // 16 bytes (enabled,gridSize,warpStrength,G)
    glm::vec4 fabric_center;    // 16 bytes (plane center xyz)
    glm::vec4 fabric_right;     // 16 bytes (plane right xyz)
    glm::vec4 fabric_up;        // 16 bytes (plane up xyz)
    glm::vec4 nebula_params;    // 16 bytes (mode, computeActive, simTime, reserved)
};                              // Total: 224 bytes

struct SphereGPU {
    glm::vec4 pos_radius;         // xyz = position, w = radius
    glm::vec4 base_emit;          // rgb = base tint, a = emissive mode
    glm::vec4 class_seed_temp;    // x = seed, y = render_class, z = subtype, w = temperature
    glm::vec4 terrain_params;     // x = terrain_amp, y = terrain_freq, z = ridge_amp, w = crater_density
    glm::vec4 material_params;    // x = roughness, y = metallic, z = specular, w = normal_strength
    glm::vec4 feature_params;     // x = continents, y = islands, z = rivers, w = ice sheets
    glm::vec4 composition_params; // x = rock_frac, y = ice_frac, z = metal_frac, w = dust_frac
    glm::vec4 atmosphere_params;  // x = cloud_cov, y = pressure, z = haze_density, w = rayleigh_strength
    glm::vec4 activity_params;    // x = weather/flaring, y = corona/coma, z = tail/accretion, w = lensing/jet
    glm::vec4 magnetosphere_params; // x = field, y = outer radius, z = axis angle, w = trapping
    glm::vec4 gravity_params;     // x = mass, y = stage, z = fuel, w = luminosity
    glm::vec4 ring_params;        // x = inner radius, y = outer radius, z = density, w = tilt
    glm::vec4 phase_params;       // x = material phase, y = phase intensity, z = collapse, w = ring ice fraction
    glm::vec4 impact_axis;        // xyz = recent impact normal
    glm::vec4 impact_params;      // x = crater, y = heat, z = radius, w = ejecta
};

struct alignas(16) NebulaGPU {
    glm::vec4 flow;    // xyz=advection flow, w=turbulence
    glm::vec4 optical; // x=density scale, y=absorption, z=scatter anisotropy, w=emission gain
};

// ── Body color helper (matches cosmos_app.cpp) ──────────────────────────────

static glm::vec3 blackbody_tint_cpu(float temperature) {
    float t = std::clamp((temperature - 1200.0f) / 32000.0f, 0.0f, 1.0f);
    float r = std::clamp(1.25f - t * 0.95f, 0.0f, 1.0f);
    float g = std::clamp(0.45f + t * 0.6f, 0.0f, 1.0f);
    float b = std::clamp(-0.1f + t * 1.25f, 0.0f, 1.0f);
    return {r, g, b};
}

static glm::vec3 star_tint_cpu(const CelestialBody& b) {
    glm::vec3 tint = blackbody_tint_cpu(b.temperature);
    float giant = (b.stellar_stage == SSTAGE_RED_GIANT) ? 1.0f
        : std::clamp((b.radius - 40.0f) / 120.0f, 0.0f, 1.0f);
    float white_dwarf = (b.stellar_stage == SSTAGE_WHITE_DWARF) ? 1.0f : 0.0f;
    float neutron_star = (b.stellar_stage == SSTAGE_NEUTRON_STAR) ? 1.0f : 0.0f;
    float massive_hot = std::clamp((b.mass - 8.0f) / 32.0f, 0.0f, 1.0f) *
        std::clamp((b.temperature - 9000.0f) / 26000.0f, 0.0f, 1.0f);
    float cool = std::clamp((6000.0f - b.temperature) / 3600.0f, 0.0f, 1.0f);
    tint = glm::mix(tint, glm::vec3(1.00f, 0.62f, 0.34f), giant * std::max(cool, 0.35f) * 0.75f);
    tint = glm::mix(tint, glm::vec3(0.76f, 0.86f, 1.00f), massive_hot * 0.70f);
    tint = glm::mix(tint, glm::vec3(0.92f, 0.96f, 1.00f), white_dwarf * 0.90f);
    tint = glm::mix(tint, glm::vec3(0.72f, 0.84f, 1.00f), neutron_star * 0.96f);
    return glm::clamp(tint, glm::vec3(0.0f), glm::vec3(1.0f));
}

static glm::vec3 body_color_vec3(const CelestialBody& b) {
    if (is_star_type(b.type))
        return star_tint_cpu(b);
    if (is_black_hole_type(b.type))
        return {0.0f, 0.0f, 0.0f};
    switch (b.type) {
    case CTYPE_PLANET:     return {0.235f, 0.549f, 0.863f};
    case CTYPE_MOON:       return {0.706f, 0.706f, 0.745f};
    case CTYPE_ASTEROID:   return {0.549f, 0.510f, 0.431f};
    case CTYPE_COMET:      return {0.627f, 0.863f, 1.0f};
    case CTYPE_NEBULA:     return {0.471f, 0.235f, 0.706f};
    case CTYPE_DUST:       return {0.804f, 0.725f, 0.627f};
    default:               return {0.784f, 0.784f, 0.784f};
    }
}

// ── Init ────────────────────────────────────────────────────────────────────

void CosmosRaytracer::init(VulkanContext& vk, VkRenderPass render_pass) {
    // ── Descriptor set layout ──────────────────────────────────────────────
    VkDescriptorSetLayoutBinding bindings[3]{};
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
    // Binding 2: Nebula SSBO (compute-assisted advection/optical params)
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

    nebula_ssbo_ = vk.create_buffer(
        MAX_SPHERES * sizeof(NebulaGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    nebula_compute_.init(vk);

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
    ubo_info.range  = sizeof(CameraUBOData);

    VkDescriptorBufferInfo ssbo_info{};
    ssbo_info.buffer = sphere_ssbo_.handle;
    ssbo_info.offset = 0;
    ssbo_info.range  = MAX_SPHERES * sizeof(SphereGPU);

    VkDescriptorBufferInfo nebula_info{};
    nebula_info.buffer = nebula_ssbo_.handle;
    nebula_info.offset = 0;
    nebula_info.range  = MAX_SPHERES * sizeof(NebulaGPU);

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
    writes[2].pBufferInfo     = &nebula_info;

    vkUpdateDescriptorSets(vk.device, 3, writes, 0, nullptr);
}

// ── Destroy ─────────────────────────────────────────────────────────────────

void CosmosRaytracer::destroy(VulkanContext& vk) {
    nebula_compute_.destroy(vk);
    vkDestroyPipeline(vk.device, pipeline_, nullptr);
    vkDestroyPipelineLayout(vk.device, pipe_layout_, nullptr);
    vkDestroyDescriptorPool(vk.device, desc_pool_, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, desc_layout_, nullptr);
    vk.destroy_buffer(camera_ubo_);
    vk.destroy_buffer(sphere_ssbo_);
    vk.destroy_buffer(nebula_ssbo_);
}

// ── Update + draw ───────────────────────────────────────────────────────────

void CosmosRaytracer::update_and_draw(VulkanContext& vk, VkCommandBuffer cmd,
                                       const CosmosState& state,
                                       const OrbitCamera& camera,
                                       const CosmosConfig& cfg,
                                       float screen_w, float screen_h,
                                       float time) {
    float aspect = screen_w / screen_h;
    glm::dvec3 target_origin = glm::dvec3(camera.target);
    glm::dvec3 eye_rel = camera.eye_position_d() - target_origin;
    glm::dmat4 view = glm::lookAt(eye_rel, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dmat4 proj = camera.proj_matrix_d(aspect);
    glm::dmat4 vp   = proj * view;

    // ── Upload camera UBO ──────────────────────────────────────────────────
    CameraUBOData cam{};
    cam.inv_vp         = glm::mat4(glm::inverse(vp));
    cam.eye_pos        = glm::vec4(glm::vec3(eye_rel), 0.0f);
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
    cam.quality_params = glm::vec4(
        (float)cfg.cosmos_quality,
        cfg.cosmos_hq_shading ? 1.0f : 0.0f,
        (float)std::clamp(cfg.cosmos_background_preset, 0, 11),
        (float)cfg.sim_time_accumulated);
    cam.render_flags = glm::vec4(
        cfg.cosmos_background_starfield ? 1.0f : 0.0f,
        cfg.cosmos_star_corona ? 1.0f : 0.0f,
        cfg.cosmos_comet_tails ? 1.0f : 0.0f,
        cfg.cosmos_blackhole_lensing ? 1.0f : 0.0f);
    cam.fabric_params = glm::vec4(
        cfg.cosmos_space_fabric ? 1.0f : 0.0f,
        std::max(cfg.cosmos_space_fabric_grid_size, 1.0f),
        std::max(cfg.cosmos_space_fabric_strength, 0.0f),
        std::max(cfg.G, 0.001f));
    const float iso_axis = 0.70710678f;
    cam.fabric_center = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    // Keep the fabric anchored to a world-space horizontal plane so it reads
    // like the classic isometric gravity grid instead of a camera-facing sheet.
    cam.fabric_right = glm::vec4(iso_axis, 0.0f, iso_axis, 0.0f);
    cam.fabric_up = glm::vec4(-iso_axis, 0.0f, iso_axis, 0.0f);
    float nebula_mode = (float)std::clamp(cfg.nebula_render_mode, 0, 2);
    float compute_active = (cfg.nebula_render_mode == 1 && nebula_compute_.is_ready()) ? 1.0f : 0.0f;
    cam.nebula_params = glm::vec4(nebula_mode, compute_active, (float)cfg.sim_time_accumulated, 0.0f);

    void* mapped = nullptr;
    if (vkMapMemory(vk.device, camera_ubo_.memory, 0, sizeof(CameraUBOData), 0, &mapped) != VK_SUCCESS || !mapped)
        throw std::runtime_error("CosmosRaytracer: failed to map camera UBO memory");
    memcpy(mapped, &cam, sizeof(CameraUBOData));
    vkUnmapMemory(vk.device, camera_ubo_.memory);

    // ── Upload sphere SSBO ─────────────────────────────────────────────────
    int n = std::min((int)state.bodies.size(), MAX_SPHERES);
    std::vector<SphereGPU> spheres((size_t)n);
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
        ? static_cast<size_t>(std::thread::hardware_concurrency())
        : 1;
    const bool use_parallel_pack = cfg.parallel_gravity && hw_threads > 1 && n >= 256;
    auto pack_range = [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            const auto& b = state.bodies[i];
        glm::vec3 col = body_color_vec3(b);
        float emissive = 0.0f;
        if (is_star_type(b.type))
            emissive = 1.4f + b.cached_visuals.corona_strength * 0.45f;
        if (is_black_hole_type(b.type))
            emissive = -1.0f;

        float render_radius = b.radius;
        if (b.type == CTYPE_DUST) {
            // Keep dust rings visible at normal zoom without changing physics radius.
            render_radius = std::max(render_radius, 0.16f);
            render_radius *= 1.0f + std::clamp(b.phase_intensity, 0.0f, 1.0f) * 0.15f;
        } else if (b.type == CTYPE_NEBULA) {
            // Slightly expanded visual shell to read as a diffuse cloud.
            render_radius *= 1.12f;
        }
        spheres[i].pos_radius = glm::vec4(glm::vec3(glm::dvec3(b.pos) - target_origin), render_radius);
        spheres[i].base_emit = glm::vec4(col, emissive);

        // Keep enough entropy in shader seed while staying in float-safe range.
        // Include index jitter so two identical body seeds still don't alias visually.
        float shader_seed = (float)(b.seed & 0xFFFFu) +
                            (float)((b.seed >> 16) & 0xFFFFu) * 0.0001f +
                            (float)i * 0.173f;

        const PlanetProperties& pp = b.cached_props;
        const BodyVisualProperties& vp = b.cached_visuals;
        MagneticSignature magnetic = estimate_magnetic_signature(b, cfg.G);
        float stellar_flux = 0.0f;
        float bh_flux = 0.0f;
        float space_weather = estimate_space_weather_flux(b, &state, cfg.G, &stellar_flux, &bh_flux);
        float storm_visual = std::clamp(space_weather * (0.35f + magnetic.magnetic_field * 0.09f), 0.0f, 2.5f);
        float radiation_belts = magnetic.has_magnetosphere
            ? std::clamp((stellar_flux + bh_flux * 1.4f) * (0.18f + magnetic.particle_trapping * 0.72f), 0.0f, 2.2f)
            : 0.0f;
        bool physical_planet = (b.type == CTYPE_PLANET || b.type == CTYPE_MOON);
        bool planet_like_visual = (vp.render_class == RENDER_PLANET || vp.render_class == RENDER_MOON);

        float cloud_cov = physical_planet ? (pp.cloud_coverage / 100.0f)
            : (b.type == CTYPE_NEBULA ? vp.cloud_detail : 0.0f);
        float pressure = physical_planet ? pp.atmosphere.pressure
            : (b.type == CTYPE_NEBULA ? (35.0f + b.collapse_progress * 140.0f) : 0.0f);

        spheres[i].class_seed_temp = glm::vec4(
            shader_seed,
            (float)vp.render_class,
            (float)vp.subtype,
            b.temperature);
        spheres[i].terrain_params = glm::vec4(
            vp.terrain_amp,
            vp.terrain_freq,
            vp.ridge_amp,
            vp.crater_density);
        spheres[i].material_params = glm::vec4(
            vp.roughness,
            vp.metallic,
            vp.specular,
            vp.normal_strength);
        spheres[i].feature_params = glm::vec4(
            vp.continent_coverage,
            vp.island_coverage,
            vp.river_density,
            vp.ice_sheet_coverage);
        float comp_w = vp.dust_frac;
        if (physical_planet && planet_like_visual)
            comp_w = pp.ocean_coverage / 100.0f;
        if (vp.render_class == RENDER_STAR) {
            spheres[i].composition_params = glm::vec4(
                vp.star_spot_coverage,
                vp.star_flare_frequency,
                vp.star_pulsation,
                vp.star_differential_rotation);
        } else {
            spheres[i].composition_params = glm::vec4(
                vp.rock_frac,
                vp.ice_frac,
                vp.metal_frac,
                comp_w);
        }
        spheres[i].atmosphere_params = glm::vec4(
            cloud_cov,
            pressure,
            vp.haze_density,
            vp.rayleigh_strength);
        if (planet_like_visual) {
            spheres[i].activity_params = glm::vec4(
                vp.weather_strength,
                vp.aurora_strength,
                vp.volcanic_activity,
                vp.mie_strength);
        } else if (vp.render_class == RENDER_NEBULA) {
            spheres[i].activity_params = glm::vec4(
                vp.weather_strength,
                vp.cloud_detail,
                std::clamp(b.collapse_progress, 0.0f, 1.5f),
                vp.mie_strength);
        } else if (vp.render_class == RENDER_STAR) {
            spheres[i].activity_params = glm::vec4(
                vp.flare_activity,
                vp.corona_strength,
                vp.spin_visual,
                vp.star_spot_coverage);
        } else if (vp.render_class == RENDER_COMET) {
            spheres[i].activity_params = glm::vec4(
                0.0f,
                vp.coma_strength,
                vp.tail_strength,
                0.0f);
        } else if (vp.render_class == RENDER_BLACK_HOLE) {
            spheres[i].activity_params = glm::vec4(
                vp.spin_visual,
                vp.accretion_strength,
                vp.jet_strength,
                vp.lensing_strength);
        } else {
            spheres[i].activity_params = glm::vec4(
                0.0f,
                0.0f,
                0.0f,
                0.0f);
        }
        spheres[i].magnetosphere_params = glm::vec4(
            magnetic.magnetic_field,
            magnetic.magnetosphere_size,
            magnetic.axis_angle,
            magnetic.particle_trapping);
        if (planet_like_visual) {
            spheres[i].gravity_params = glm::vec4(
                std::max(b.mass, 0.0f),
                storm_visual,
                std::clamp(b.atmosphere_retention, 0.0f, 1.0f),
                radiation_belts);
        } else {
            spheres[i].gravity_params = glm::vec4(
                std::max(b.mass, 0.0f),
                (float)b.stellar_stage,
                std::clamp(b.fuel, 0.0f, 1.0f),
                std::max(b.luminosity, 0.0f));
        }
        spheres[i].ring_params = glm::vec4(
            std::max(b.ring_inner_radius, 0.0f),
            std::max(b.ring_outer_radius, 0.0f),
            std::clamp(b.ring_density, 0.0f, 1.0f),
            std::clamp(b.ring_tilt, 0.0f, 1.30f));
        spheres[i].phase_params = glm::vec4(
            (float)b.material_phase,
            std::clamp(b.phase_intensity, 0.0f, 1.0f),
            std::clamp(b.collapse_progress, 0.0f, 1.0f),
            std::clamp(b.ring_ice_fraction, 0.0f, 1.0f));
        glm::vec3 impact_axis = glm::length(b.impact_normal) > 1.0e-4f
            ? glm::normalize(b.impact_normal)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        float spin_rate = std::isfinite(b.angular_vel) ? b.angular_vel : 0.0f;
        spheres[i].impact_axis = glm::vec4(impact_axis, spin_rate);
        spheres[i].impact_params = glm::vec4(
            std::clamp(b.impact_crater_strength, 0.0f, 1.0f),
            std::clamp(b.impact_heat, 0.0f, 1.0f),
            std::clamp(b.impact_radius, 0.0f, 1.0f),
            std::clamp(b.impact_ejecta, 0.0f, 1.0f));
        }
    };
    if (use_parallel_pack) {
        const size_t worker_count = std::min(hw_threads, (size_t)n);
        std::vector<std::thread> workers;
        workers.reserve(worker_count - 1);
        for (size_t t = 1; t < worker_count; ++t) {
            int begin = (int)((size_t)n * t / worker_count);
            int end = (int)((size_t)n * (t + 1) / worker_count);
            workers.emplace_back([&, begin, end]() { pack_range(begin, end); });
        }
        pack_range(0, (int)((size_t)n / worker_count));
        for (auto& worker : workers) worker.join();
    } else {
        pack_range(0, n);
    }

    if (n > 0) {
        mapped = nullptr;
        if (vkMapMemory(vk.device, sphere_ssbo_.memory, 0,
                        n * sizeof(SphereGPU), 0, &mapped) != VK_SUCCESS || !mapped)
            throw std::runtime_error("CosmosRaytracer: failed to map sphere SSBO memory");
        memcpy(mapped, spheres.data(), n * sizeof(SphereGPU));
        vkUnmapMemory(vk.device, sphere_ssbo_.memory);
    }

    // ── Nebula compute prepass (optional) + upload ────────────────────────
    std::vector<NebulaGPU> nebula_data((size_t)n);
    for (int i = 0; i < n; ++i) {
        nebula_data[(size_t)i].flow = glm::vec4(0.0f);
        nebula_data[(size_t)i].optical = glm::vec4(1.0f, 1.0f, 0.25f, 1.0f);
    }

    bool used_nebula_compute = false;
    if (n > 0 && cfg.nebula_render_mode == 1 && nebula_compute_.is_ready()) {
        std::vector<NebulaComputeInput> nebula_in((size_t)n);
        for (int i = 0; i < n; ++i) {
            nebula_in[(size_t)i].seed_class_temp = spheres[(size_t)i].class_seed_temp;
            nebula_in[(size_t)i].atmosphere = spheres[(size_t)i].atmosphere_params;
            nebula_in[(size_t)i].activity = spheres[(size_t)i].activity_params;
            nebula_in[(size_t)i].composition = spheres[(size_t)i].composition_params;
        }
        used_nebula_compute = nebula_compute_.compute_to_buffer(vk, nebula_in, time, nebula_ssbo_, (size_t)MAX_SPHERES);
    }
    if (!used_nebula_compute) {
        for (int i = 0; i < n; ++i) {
            const auto& s = spheres[(size_t)i];
            if ((int)(s.class_seed_temp.y + 0.5f) != RENDER_NEBULA)
                continue;
            float dust = std::clamp(s.composition_params.w, 0.0f, 1.0f);
            float haze = std::clamp(s.atmosphere_params.z, 0.0f, 2.5f);
            float flow = std::clamp(s.activity_params.x, 0.0f, 2.5f);
            float cloud = std::clamp(s.activity_params.y, 0.0f, 2.5f);
            float collapse = std::clamp(s.activity_params.z, 0.0f, 2.5f);
            float seed = s.class_seed_temp.x;
            float t = time * 0.1f;
            glm::vec3 adv(
                std::sin(seed * 0.017f + t),
                std::cos(seed * 0.013f - t * 0.7f),
                std::sin(seed * 0.011f + t * 0.6f));
            adv *= (0.05f + flow * 0.18f + cloud * 0.06f);
            nebula_data[(size_t)i].flow = glm::vec4(adv, glm::length(adv));
            nebula_data[(size_t)i].optical = glm::vec4(
                std::clamp(0.75f + haze * 0.40f + cloud * 0.15f, 0.45f, 4.0f),
                std::clamp(0.95f + dust * 0.90f + haze * 0.30f, 0.50f, 4.0f),
                std::clamp(0.18f + dust * 0.38f + collapse * 0.08f, 0.02f, 0.90f),
                std::clamp(0.90f + collapse * 0.30f, 0.55f, 2.4f));
        }
    }
    if (n > 0 && !used_nebula_compute) {
        mapped = nullptr;
        if (vkMapMemory(vk.device, nebula_ssbo_.memory, 0,
                        n * sizeof(NebulaGPU), 0, &mapped) != VK_SUCCESS || !mapped)
            throw std::runtime_error("CosmosRaytracer: failed to map nebula SSBO memory");
        memcpy(mapped, nebula_data.data(), n * sizeof(NebulaGPU));
        vkUnmapMemory(vk.device, nebula_ssbo_.memory);
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
