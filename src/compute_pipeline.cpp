#include "compute_pipeline.h"
#include <stdexcept>
#include <cstring>
#include <array>

// ── Init / Destroy ────────────────────────────────────────────────────────────

void ComputePipeline::init(VulkanContext& ctx, const std::string& shader_spv_path) {
    render_w = REGION_W;
    render_h = REGION_H;

    // Create the render texture (render_w × render_h, rgba32f)
    particle_texture = ctx.create_image(
        render_w, render_h,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    particle_texture.view = ctx.create_image_view(
        particle_texture.handle,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    // Transition the image to GENERAL so the compute shader can write to it
    ctx.transition_image_layout(
        particle_texture.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    particle_texture_view = particle_texture.view;

    sampler = ctx.create_sampler_nearest();

    create_descriptor_set_layout(ctx.device);
    create_pipeline_layout(ctx.device);
    create_compute_pipeline(ctx, shader_spv_path);
    create_descriptor_pool(ctx.device);
}

void ComputePipeline::destroy(VulkanContext& ctx) {
    clear_buffers(ctx);

    if (desc_pool_       != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, desc_pool_, nullptr);
    if (pipeline_        != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, pipeline_layout_, nullptr);
    if (desc_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, desc_set_layout_, nullptr);
    if (sampler          != VK_NULL_HANDLE) vkDestroySampler(ctx.device, sampler, nullptr);
    ctx.destroy_image(particle_texture);
}

// ── Resize render texture ─────────────────────────────────────────────────────

void ComputePipeline::resize_render_texture(VulkanContext& ctx, uint32_t w, uint32_t h) {
    if (w == render_w && h == render_h) return;

    vkDeviceWaitIdle(ctx.device);

    // Destroy old texture
    ctx.destroy_image(particle_texture);

    render_w = w;
    render_h = h;

    // Create new texture at requested size
    particle_texture = ctx.create_image(
        render_w, render_h,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    particle_texture.view = ctx.create_image_view(
        particle_texture.handle,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    ctx.transition_image_layout(
        particle_texture.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    particle_texture_view = particle_texture.view;

    // Update binding 7 (storage image) in both descriptor sets
    if (desc_set_a_ != VK_NULL_HANDLE) {
        VkDescriptorImageInfo img_info{ VK_NULL_HANDLE, particle_texture.view, VK_IMAGE_LAYOUT_GENERAL };

        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = (i == 0) ? desc_set_a_ : desc_set_b_;
            writes[i].dstBinding      = 7;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo      = &img_info;
        }
        vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
    }
}

// ── Descriptor set layout ─────────────────────────────────────────────────────
// Bindings 0-6: particle storage buffers
// Binding  7:   storage image (render texture)
// Binding  8:   behavior flags (shared, static per type)
// Bindings 9-12: polar angle/angular-velocity (double-buffered)

void ComputePipeline::create_descriptor_set_layout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 21> bindings{};

    // Bindings 0-6 and 8-20: storage buffers
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    for (uint32_t i = 8; i < 21; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // Binding 7: storage image
    bindings[7].binding         = 7;
    bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &desc_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor set layout");
}

// ── Pipeline layout (includes push constants) ─────────────────────────────────

void ComputePipeline::create_pipeline_layout(VkDevice device) {
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &desc_set_layout_;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pc_range;

    if (vkCreatePipelineLayout(device, &ci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline layout");
}

// ── Compute pipeline ──────────────────────────────────────────────────────────

void ComputePipeline::create_compute_pipeline(VulkanContext& ctx,
                                              const std::string& shader_spv_path)
{
    VkShaderModule module = ctx.create_shader_module(shader_spv_path);

    VkPipelineShaderStageCreateInfo stage_ci{};
    stage_ci.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_ci.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = module;
    stage_ci.pName  = "main";

    VkComputePipelineCreateInfo ci{};
    ci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage  = stage_ci;
    ci.layout = pipeline_layout_;

    if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline");

    vkDestroyShaderModule(ctx.device, module, nullptr);
}

// ── Descriptor pool ───────────────────────────────────────────────────────────

void ComputePipeline::create_descriptor_pool(VkDevice device) {
    // 2 sets × 16 storage buffers (7 orig + 1 behavior + 4 angle/avel + 2 energy + 1 genome + 1 bond) = 32
    // 2 sets × 1 storage image = 2
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 42 };
    pool_sizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   2 };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = 2;
    ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    ci.pPoolSizes    = pool_sizes.data();

    if (vkCreateDescriptorPool(device, &ci, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor pool");
}

// ── Buffer creation ───────────────────────────────────────────────────────────

void ComputePipeline::create_buffers(VulkanContext& ctx, const Particles& particles) {
    live_particle_count_ = static_cast<uint32_t>(particles.positions.size());

    // ── Detect discrete GPU: check for DEVICE_LOCAL-only memory (no HOST_VISIBLE) ──
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &mem_props);
    bool has_device_only = false;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        auto flags = mem_props.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            has_device_only = true;
            break;
        }
    }

    const VkMemoryPropertyFlags HOST_PROPS =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // For particle buffers on discrete GPUs: DEVICE_LOCAL + transfer flags
    // On integrated GPUs: HOST_VISIBLE (same as before, no benefit from staging)
    VkBufferUsageFlags    PARTICLE_USAGE;
    VkMemoryPropertyFlags PARTICLE_MEM;
    if (has_device_only) {
        PARTICLE_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                       | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        PARTICLE_MEM   = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        use_device_local_ = true;
    } else {
        PARTICLE_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        PARTICLE_MEM   = HOST_PROPS;
        use_device_local_ = false;
    }

    // Per-frame CPU-written buffers always HOST_VISIBLE (small, CPU-written each frame)
    const VkBufferUsageFlags CPU_BUF_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VkDeviceSize pos_size      = particles.positions.size()  * sizeof(glm::vec2);
    VkDeviceSize vel_size      = particles.velocities.size() * sizeof(glm::vec2);
    VkDeviceSize type_size     = particles.types.size()       * sizeof(uint32_t);
    VkDeviceSize force_size    = particles.forces.size()      * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()      * sizeof(glm::vec4);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES            * sizeof(uint32_t);
    VkDeviceSize angle_size    = particles.angles.size()       * sizeof(float);
    VkDeviceSize energy_size   = particles.energies.size()     * sizeof(float);

    // Double-buffered particle buffers (DEVICE_LOCAL on discrete GPU)
    pos_buffer_a_          = ctx.create_buffer(pos_size,   PARTICLE_USAGE, PARTICLE_MEM);
    pos_buffer_b_          = ctx.create_buffer(pos_size,   PARTICLE_USAGE, PARTICLE_MEM);
    vel_buffer_a_          = ctx.create_buffer(vel_size,   PARTICLE_USAGE, PARTICLE_MEM);
    vel_buffer_b_          = ctx.create_buffer(vel_size,   PARTICLE_USAGE, PARTICLE_MEM);
    angle_buffer_a_        = ctx.create_buffer(angle_size, PARTICLE_USAGE, PARTICLE_MEM);
    angle_buffer_b_        = ctx.create_buffer(angle_size, PARTICLE_USAGE, PARTICLE_MEM);
    angular_vel_buffer_a_  = ctx.create_buffer(angle_size, PARTICLE_USAGE, PARTICLE_MEM);
    angular_vel_buffer_b_  = ctx.create_buffer(angle_size, PARTICLE_USAGE, PARTICLE_MEM);
    energy_buffer_a_       = ctx.create_buffer(energy_size, PARTICLE_USAGE, PARTICLE_MEM);
    energy_buffer_b_       = ctx.create_buffer(energy_size, PARTICLE_USAGE, PARTICLE_MEM);

    // Per-frame CPU-written buffers (always HOST_VISIBLE)
    type_buffer_           = ctx.create_buffer(type_size,     CPU_BUF_USAGE, HOST_PROPS);
    force_buffer_          = ctx.create_buffer(force_size,    CPU_BUF_USAGE, HOST_PROPS);
    color_buffer_          = ctx.create_buffer(color_size,    CPU_BUF_USAGE, HOST_PROPS);
    behavior_buffer_       = ctx.create_buffer(behavior_size, CPU_BUF_USAGE, HOST_PROPS);

    // Create staging buffers for DEVICE_LOCAL transfers
    if (use_device_local_) {
        // Staging size: enough for pos+vel+energy (the largest single transfer)
        VkDeviceSize staging_size = pos_size + vel_size + energy_size;
        VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        staging_upload_   = ctx.create_buffer(staging_size, staging_usage, HOST_PROPS);
        staging_readback_ = ctx.create_buffer(staging_size, staging_usage, HOST_PROPS);
    }

    // Upload initial data
    if (use_device_local_) {
        // Stage and copy each buffer pair via command buffer
        auto stage_copy = [&](const Buffer& dst, const void* data, VkDeviceSize sz) {
            void* mapped = nullptr;
            vkMapMemory(ctx.device, staging_upload_.memory, 0, sz, 0, &mapped);
            std::memcpy(mapped, data, static_cast<size_t>(sz));
            vkUnmapMemory(ctx.device, staging_upload_.memory);

            VkCommandBuffer cmd = ctx.begin_single_command();
            VkBufferCopy region{};
            region.size = sz;
            vkCmdCopyBuffer(cmd, staging_upload_.handle, dst.handle, 1, &region);
            ctx.end_single_command(cmd);
        };
        stage_copy(pos_buffer_a_,         particles.positions.data(),          pos_size);
        stage_copy(pos_buffer_b_,         particles.positions.data(),          pos_size);
        stage_copy(vel_buffer_a_,         particles.velocities.data(),         vel_size);
        stage_copy(vel_buffer_b_,         particles.velocities.data(),         vel_size);
        stage_copy(angle_buffer_a_,       particles.angles.data(),             angle_size);
        stage_copy(angle_buffer_b_,       particles.angles.data(),             angle_size);
        stage_copy(angular_vel_buffer_a_, particles.angular_velocities.data(), angle_size);
        stage_copy(angular_vel_buffer_b_, particles.angular_velocities.data(), angle_size);
        stage_copy(energy_buffer_a_,      particles.energies.data(),           energy_size);
        stage_copy(energy_buffer_b_,      particles.energies.data(),           energy_size);
    } else {
        ctx.update_buffer(pos_buffer_a_,  particles.positions.data(),        pos_size);
        ctx.update_buffer(pos_buffer_b_,  particles.positions.data(),        pos_size);
        ctx.update_buffer(vel_buffer_a_,  particles.velocities.data(),       vel_size);
        ctx.update_buffer(vel_buffer_b_,  particles.velocities.data(),       vel_size);
        ctx.update_buffer(angle_buffer_a_,  particles.angles.data(),         angle_size);
        ctx.update_buffer(angle_buffer_b_,  particles.angles.data(),         angle_size);
        ctx.update_buffer(angular_vel_buffer_a_, particles.angular_velocities.data(), angle_size);
        ctx.update_buffer(angular_vel_buffer_b_, particles.angular_velocities.data(), angle_size);
        ctx.update_buffer(energy_buffer_a_, particles.energies.data(), energy_size);
        ctx.update_buffer(energy_buffer_b_, particles.energies.data(), energy_size);
    }

    // CPU-written buffers: always direct map
    ctx.update_buffer(type_buffer_,   particles.types.data(),            type_size);
    ctx.update_buffer(force_buffer_,  particles.forces.data(),           force_size);
    ctx.update_buffer(color_buffer_,  particles.colors.data(),           color_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,        behavior_size);

    VkDeviceSize genome_size = particles.genomes.size() * sizeof(float);
    genome_buffer_ = ctx.create_buffer(genome_size, CPU_BUF_USAGE, HOST_PROPS);
    ctx.update_buffer(genome_buffer_, particles.genomes.data(), genome_size);

    // Bond buffer: particle_count × MAX_BONDS_PER_PARTICLE uint32_t, all EMPTY
    VkDeviceSize bond_size = static_cast<VkDeviceSize>(particles.positions.size())
                             * MAX_BONDS_PER_PARTICLE * sizeof(uint32_t);
    bond_buffer_ = ctx.create_buffer(bond_size, CPU_BUF_USAGE, HOST_PROPS);
    {
        std::vector<uint32_t> empty_bonds(particles.positions.size() * MAX_BONDS_PER_PARTICLE, 0xFFFFFFFFu);
        ctx.update_buffer(bond_buffer_, empty_bonds.data(), bond_size);
    }

    // Force object buffer (binding 17, not double-buffered)
    VkDeviceSize force_obj_size = MAX_FORCE_OBJECTS * sizeof(ForceObject);
    force_obj_buffer_ = ctx.create_buffer(force_obj_size, CPU_BUF_USAGE, HOST_PROPS);
    {
        ForceObject empty_objs[MAX_FORCE_OBJECTS] = {};
        ctx.update_buffer(force_obj_buffer_, empty_objs, force_obj_size);
    }

    // Per-type mass_inv + ZPE lookup table (binding 18)
    // Layout: [type*2+0] = mass_inv, [type*2+1] = zpe
    {
        std::vector<float> mass_zpe(MAX_PARTICLE_TYPES * 2, 1.0f);
        // Mass inverse values (matching shader get_mass_inv)
        auto set = [&](uint32_t t, float mi, float zpe) {
            mass_zpe[t * 2 + 0] = mi;
            mass_zpe[t * 2 + 1] = zpe;
        };
        auto zpe_from_mass = [](float mi) -> float {
            if (mi >= 50.0f) return 0.0f; // massless
            float mass = (mi > 0.001f) ? 1.0f / mi : 1000.0f;
            float z = mass * 0.0001f;
            return (z > 0.05f) ? 0.05f : (z < 0.0f ? 0.0f : z);
        };
        // SM particles
        set(0,  0.025f,    zpe_from_mass(0.025f));    // Proton
        set(1,  0.025f,    zpe_from_mass(0.025f));    // Neutron
        set(2,  1.0f,      zpe_from_mass(1.0f));      // Electron
        set(3,  100.0f,    0.0f);                      // Photon (massless)
        set(4,  1.0f,      zpe_from_mass(1.0f));      // Positron
        set(5,  0.025f,    zpe_from_mass(0.025f));    // Antiproton
        set(6,  100.0f,    0.0f);                      // Neutrino_e (massless)
        set(7,  0.005f,    zpe_from_mass(0.005f));    // Muon (μ⁻)
        set(8,  0.005f,    zpe_from_mass(0.005f));    // Antimuon (μ⁺)
        set(9,  0.0003f,   zpe_from_mass(0.0003f));   // Tau (τ⁻)
        set(10, 0.0003f,   zpe_from_mass(0.0003f));   // Antitau (τ⁺)
        set(11, 100.0f,    0.0f);                      // Muon Neutrino (νμ, massless)
        set(12, 100.0f,    0.0f);                      // Tau Neutrino (ντ, massless)
        // Quarks
        set(13, 0.2f,      zpe_from_mass(0.2f));      // Up
        set(14, 0.15f,     zpe_from_mass(0.15f));     // Down
        set(15, 0.005f,    zpe_from_mass(0.005f));    // Strange
        set(16, 0.0004f,   zpe_from_mass(0.0004f));   // Charm
        set(17, 0.00012f,  zpe_from_mass(0.00012f));  // Bottom
        set(18, 0.000003f, zpe_from_mass(0.000003f)); // Top
        // Antiquarks
        set(19, 0.2f,      zpe_from_mass(0.2f));      // Anti-up
        set(20, 0.15f,     zpe_from_mass(0.15f));     // Anti-down
        set(21, 0.005f,    zpe_from_mass(0.005f));    // Anti-strange
        set(22, 0.0004f,   zpe_from_mass(0.0004f));   // Anti-charm
        set(23, 0.00012f,  zpe_from_mass(0.00012f));  // Anti-bottom
        set(24, 0.000003f, zpe_from_mass(0.000003f)); // Anti-top
        // Bosons
        set(25, 100.0f,    0.0f);                      // Gluon (massless)
        set(26, 0.00006f,  zpe_from_mass(0.00006f));  // W+
        set(27, 0.00006f,  zpe_from_mass(0.00006f));  // W-
        set(28, 0.00005f,  zpe_from_mass(0.00005f));  // Z0
        set(29, 0.00004f,  zpe_from_mass(0.00004f));  // Higgs
        // BSM
        set(30, 100.0f,    0.0f);                      // Graviton (massless)
        set(31, 0.001f,    zpe_from_mass(0.001f));    // Dark Matter
        set(32, 100.0f,    0.0f);                      // Dark Energy (massless)
        // Hypothetical: Dark Matter Candidates
        set(33, 5.0f,            zpe_from_mass(5.0f));             // Axino
        set(34, 0.0000001f,      zpe_from_mass(0.0000001f));      // WIMPzilla
        set(35, 0.001f,          zpe_from_mass(0.001f));           // SIMP
        set(36, 100.0f,          0.0f);                             // Sterile Neutrino
        set(37, 100.0f,          0.0f);                             // Dark Photon
        set(38, 0.000005f,       zpe_from_mass(0.000005f));        // Q-Ball
        // Hypothetical: SUSY
        set(39, 0.000025f,       zpe_from_mass(0.000025f));        // Selectron
        set(40, 0.000017f,       zpe_from_mass(0.000017f));        // Smuon
        set(41, 0.000033f,       zpe_from_mass(0.000033f));        // Stau
        set(42, 0.0000033f,      zpe_from_mass(0.0000033f));       // Squark
        set(43, 0.0000025f,      zpe_from_mass(0.0000025f));       // Gluino
        set(44, 0.00005f,        zpe_from_mass(0.00005f));         // Photino
        set(45, 0.000017f,       zpe_from_mass(0.000017f));        // Wino
        set(46, 0.000017f,       zpe_from_mass(0.000017f));        // Zino
        set(47, 0.000025f,       zpe_from_mass(0.000025f));        // Higgsino
        set(48, 0.00005f,        zpe_from_mass(0.00005f));         // Neutralino
        set(49, 0.000025f,       zpe_from_mass(0.000025f));        // Sneutrino
        // Hypothetical: Force Carriers & Gravity
        set(50, 100.0f,          0.0f);                             // Gravitino
        set(51, 0.00004f,        zpe_from_mass(0.00004f));         // X Boson (GUT-scale, capped)
        set(52, 0.00004f,        zpe_from_mass(0.00004f));         // Y Boson (GUT-scale, capped)
        set(53, 0.00005f,        zpe_from_mass(0.00005f));         // Monopole (GUT-scale, capped)
        set(54, 0.000005f,       zpe_from_mass(0.000005f));        // Radion
        set(55, 0.0005f,         zpe_from_mass(0.0005f));          // Dilaton
        // Hypothetical: Theoretical Extremes
        set(56, 50.0f,           0.0f);                             // Tachyon
        set(57, 0.00003f,        zpe_from_mass(0.00003f));         // Preon (sub-quark, capped)
        set(58, 0.00004f,        zpe_from_mass(0.00004f));         // Inflaton (GUT-scale, capped)
        set(59, 100.0f,          0.0f);                             // Majoron
        set(60, 0.002f,          zpe_from_mass(0.002f));           // Odderon
        set(61, 0.003f,          zpe_from_mass(0.003f));           // Glueball
        set(62, 0.005f,          zpe_from_mass(0.005f));           // Skyrmion
        set(63, 100.0f,          0.0f);                             // X17
        set(64, 100.0f,          0.0f);                             // Chameleon
        // Hypothetical: New Class
        set(65, 0.00001f,        zpe_from_mass(0.00001f));         // Paraparticle
        set(66, 100.0f,          0.0f);                             // Dyn. Axion QP

        VkDeviceSize mzpe_size = mass_zpe.size() * sizeof(float);
        mass_zpe_buffer_ = ctx.create_buffer(mzpe_size, CPU_BUF_USAGE, HOST_PROPS);
        ctx.update_buffer(mass_zpe_buffer_, mass_zpe.data(), mzpe_size);
    }

    // GPU spatial grid buffers (binding 19-20, readonly by shader)
    {
        VkDeviceSize grid_start_size = (GPU_GRID_CELLS + 1) * sizeof(uint32_t);
        VkDeviceSize grid_idx_size   = MAX_GPU_PARTICLES * sizeof(uint32_t);
        grid_cell_start_buffer_ = ctx.create_buffer(grid_start_size, CPU_BUF_USAGE, HOST_PROPS);
        grid_indices_buffer_    = ctx.create_buffer(grid_idx_size,   CPU_BUF_USAGE, HOST_PROPS);
        // Zero-initialize (empty grid — all cells have start==0)
        std::vector<uint32_t> zero_start(GPU_GRID_CELLS + 1, 0);
        ctx.update_buffer(grid_cell_start_buffer_, zero_start.data(), grid_start_size);
    }

    // Pre-size reusable scratch buffer for per-frame force scaling
    effective_forces_.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES);

    allocate_and_write_descriptor_sets(ctx);

    tick = 0;
}

void ComputePipeline::clear_buffers(VulkanContext& ctx) {
    vkDeviceWaitIdle(ctx.device);

    // Free descriptor sets by resetting the pool
    if (desc_pool_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(ctx.device, desc_pool_, 0);
        desc_set_a_ = VK_NULL_HANDLE;
        desc_set_b_ = VK_NULL_HANDLE;
    }

    ctx.destroy_buffer(pos_buffer_a_);
    ctx.destroy_buffer(pos_buffer_b_);
    ctx.destroy_buffer(vel_buffer_a_);
    ctx.destroy_buffer(vel_buffer_b_);
    ctx.destroy_buffer(type_buffer_);
    ctx.destroy_buffer(force_buffer_);
    ctx.destroy_buffer(color_buffer_);
    ctx.destroy_buffer(behavior_buffer_);
    ctx.destroy_buffer(angle_buffer_a_);
    ctx.destroy_buffer(angle_buffer_b_);
    ctx.destroy_buffer(angular_vel_buffer_a_);
    ctx.destroy_buffer(angular_vel_buffer_b_);
    ctx.destroy_buffer(energy_buffer_a_);
    ctx.destroy_buffer(energy_buffer_b_);
    ctx.destroy_buffer(genome_buffer_);
    ctx.destroy_buffer(bond_buffer_);
    ctx.destroy_buffer(force_obj_buffer_);
    ctx.destroy_buffer(mass_zpe_buffer_);
    ctx.destroy_buffer(grid_cell_start_buffer_);
    ctx.destroy_buffer(grid_indices_buffer_);
    ctx.destroy_buffer(staging_upload_);
    ctx.destroy_buffer(staging_readback_);
    use_device_local_ = false;
}

// ── Write descriptor sets ─────────────────────────────────────────────────────

static void write_storage_buffer(std::vector<VkWriteDescriptorSet>& writes,
                                  std::vector<VkDescriptorBufferInfo>& buf_infos,
                                  VkDescriptorSet set, uint32_t binding,
                                  VkBuffer buf, VkDeviceSize size)
{
    buf_infos.push_back({ buf, 0, size });
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo     = &buf_infos.back();
    writes.push_back(w);
}

static void write_storage_image(std::vector<VkWriteDescriptorSet>& writes,
                                 std::vector<VkDescriptorImageInfo>& img_infos,
                                 VkDescriptorSet set, uint32_t binding,
                                 VkImageView view)
{
    img_infos.push_back({ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL });
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo      = &img_infos.back();
    writes.push_back(w);
}

void ComputePipeline::allocate_and_write_descriptor_sets(VulkanContext& ctx) {
    std::array<VkDescriptorSetLayout, 2> layouts = { desc_set_layout_, desc_set_layout_ };
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = desc_pool_;
    alloc.descriptorSetCount = 2;
    alloc.pSetLayouts        = layouts.data();

    std::array<VkDescriptorSet, 2> sets{};
    if (vkAllocateDescriptorSets(ctx.device, &alloc, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate compute descriptor sets");

    desc_set_a_ = sets[0];
    desc_set_b_ = sets[1];

    std::vector<VkWriteDescriptorSet>    writes;
    std::vector<VkDescriptorBufferInfo>  buf_infos;
    std::vector<VkDescriptorImageInfo>   img_infos;
    writes.reserve(44);
    buf_infos.reserve(42);
    img_infos.reserve(2);

    auto pos_sz  = pos_buffer_a_.size;
    auto vel_sz  = vel_buffer_a_.size;
    auto type_sz = type_buffer_.size;
    auto frc_sz  = force_buffer_.size;
    auto col_sz  = color_buffer_.size;
    auto beh_sz  = behavior_buffer_.size;
    auto ang_sz  = angle_buffer_a_.size;
    auto nrg_sz  = energy_buffer_a_.size;
    auto gnm_sz  = genome_buffer_.size;
    auto bnd_sz  = bond_buffer_.size;
    auto fo_sz   = force_obj_buffer_.size;
    auto mzpe_sz = mass_zpe_buffer_.size;
    auto gcs_sz  = grid_cell_start_buffer_.size;
    auto gix_sz  = grid_indices_buffer_.size;

    // ── Set A: in=a, out=b ────────────────────────────────────────────────────
    write_storage_buffer(writes, buf_infos, desc_set_a_,  0, pos_buffer_a_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  1, vel_buffer_a_.handle,         vel_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  2, type_buffer_.handle,          type_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  3, force_buffer_.handle,         frc_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  4, color_buffer_.handle,         col_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  5, pos_buffer_b_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  6, vel_buffer_b_.handle,         vel_sz);
    write_storage_image (writes, img_infos, desc_set_a_,  7, particle_texture.view);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  8, behavior_buffer_.handle,      beh_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  9, angle_buffer_a_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 10, angular_vel_buffer_a_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 11, angle_buffer_b_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 12, angular_vel_buffer_b_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 13, energy_buffer_a_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 14, energy_buffer_b_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 15, genome_buffer_.handle,        gnm_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 16, bond_buffer_.handle,          bnd_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 17, force_obj_buffer_.handle,    fo_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 18, mass_zpe_buffer_.handle,    mzpe_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 19, grid_cell_start_buffer_.handle, gcs_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 20, grid_indices_buffer_.handle,    gix_sz);

    // ── Set B: in=b, out=a ────────────────────────────────────────────────────
    write_storage_buffer(writes, buf_infos, desc_set_b_,  0, pos_buffer_b_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  1, vel_buffer_b_.handle,         vel_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  2, type_buffer_.handle,          type_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  3, force_buffer_.handle,         frc_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  4, color_buffer_.handle,         col_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  5, pos_buffer_a_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  6, vel_buffer_a_.handle,         vel_sz);
    write_storage_image (writes, img_infos, desc_set_b_,  7, particle_texture.view);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  8, behavior_buffer_.handle,      beh_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  9, angle_buffer_b_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 10, angular_vel_buffer_b_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 11, angle_buffer_a_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 12, angular_vel_buffer_a_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 13, energy_buffer_b_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 14, energy_buffer_a_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 15, genome_buffer_.handle,        gnm_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 16, bond_buffer_.handle,          bnd_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 17, force_obj_buffer_.handle,    fo_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 18, mass_zpe_buffer_.handle,    mzpe_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 19, grid_cell_start_buffer_.handle, gcs_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 20, grid_indices_buffer_.handle,    gix_sz);

    vkUpdateDescriptorSets(ctx.device,
                           static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

// ── Per-frame dynamic uploads ─────────────────────────────────────────────────

void ComputePipeline::upload_dynamic_data(VulkanContext& ctx, const Particles& particles) {
    if (force_buffer_.handle == VK_NULL_HANDLE) return;

    // Apply per-type trait scales before uploading forces.
    // The UI edits particles.forces (base values); GPU receives the scaled version.
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b)
        for (uint32_t a = 0; a < MAX_PARTICLE_TYPES; ++a) {
            uint32_t fi = a + b * MAX_PARTICLE_TYPES;
            effective_forces_[fi] = particles.forces[fi] * particles.trait_scales[a];
        }

    VkDeviceSize force_size    = particles.forces.size()     * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()     * sizeof(glm::vec4);
    VkDeviceSize type_size     = particles.types.size()      * sizeof(uint32_t);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES           * sizeof(uint32_t);

    ctx.update_buffer(force_buffer_,    effective_forces_.data(),      force_size);
    ctx.update_buffer(color_buffer_,    particles.colors.data(),       color_size);
    ctx.update_buffer(type_buffer_,     particles.types.data(),        type_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,      behavior_size);

    if (genome_buffer_.handle != VK_NULL_HANDLE && !particles.genomes.empty()) {
        VkDeviceSize genome_size = particles.genomes.size() * sizeof(float);
        ctx.update_buffer(genome_buffer_, particles.genomes.data(), genome_size);
    }

    if (bond_buffer_.handle != VK_NULL_HANDLE && particles.bond_partners_ptr != nullptr) {
        VkDeviceSize bond_size = static_cast<VkDeviceSize>(particles.bond_partners_count) * sizeof(uint32_t);
        ctx.update_buffer(bond_buffer_, particles.bond_partners_ptr, bond_size);
    }
}

void ComputePipeline::upload_force_objects(VulkanContext& ctx,
                                           const ForceObject* objects)
{
    if (force_obj_buffer_.handle == VK_NULL_HANDLE) return;
    ctx.update_buffer(force_obj_buffer_, objects,
                      MAX_FORCE_OBJECTS * sizeof(ForceObject));
}

void ComputePipeline::upload_gpu_grid(VulkanContext& ctx,
                                       const std::vector<uint32_t>& cell_start,
                                       const std::vector<uint32_t>& sorted_indices)
{
    if (grid_cell_start_buffer_.handle == VK_NULL_HANDLE) return;

    VkDeviceSize start_bytes = cell_start.size() * sizeof(uint32_t);
    ctx.update_buffer(grid_cell_start_buffer_, cell_start.data(),
                      std::min(start_bytes, grid_cell_start_buffer_.size));

    if (!sorted_indices.empty()) {
        VkDeviceSize idx_bytes = sorted_indices.size() * sizeof(uint32_t);
        ctx.update_buffer(grid_indices_buffer_, sorted_indices.data(),
                          std::min(idx_bytes, grid_indices_buffer_.size));
    }
}

void ComputePipeline::read_current_state(VulkanContext& ctx,
                                          std::vector<glm::vec2>& out_positions,
                                          std::vector<glm::vec2>& out_velocities,
                                          std::vector<float>&     out_energies)
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    // After tick is incremented in record(), the output buffer is:
    //   tick odd  → desc_set_a was used (input=a, output=b) → read *_buffer_b_
    //   tick even → desc_set_b was used (input=b, output=a) → read *_buffer_a_
    const Buffer& cur_pos = (tick % 2 == 1) ? pos_buffer_b_ : pos_buffer_a_;
    const Buffer& cur_vel = (tick % 2 == 1) ? vel_buffer_b_ : vel_buffer_a_;
    const Buffer& cur_nrg = (tick % 2 == 1) ? energy_buffer_b_ : energy_buffer_a_;

    uint32_t n = static_cast<uint32_t>(out_positions.size());
    VkDeviceSize pos_bytes = n * sizeof(glm::vec2);
    VkDeviceSize vel_bytes = n * sizeof(glm::vec2);
    VkDeviceSize nrg_bytes = static_cast<uint32_t>(out_energies.size()) * sizeof(float);

    if (use_device_local_) {
        // Copy from DEVICE_LOCAL → staging readback buffer, then map staging
        VkDeviceSize offset = 0;
        VkCommandBuffer cmd = ctx.begin_single_command();

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = offset;
        region.size = pos_bytes;
        vkCmdCopyBuffer(cmd, cur_pos.handle, staging_readback_.handle, 1, &region);
        offset += pos_bytes;

        region.srcOffset = 0;
        region.dstOffset = offset;
        region.size = vel_bytes;
        vkCmdCopyBuffer(cmd, cur_vel.handle, staging_readback_.handle, 1, &region);
        offset += vel_bytes;

        if (cur_nrg.handle != VK_NULL_HANDLE && !out_energies.empty()) {
            region.srcOffset = 0;
            region.dstOffset = offset;
            region.size = nrg_bytes;
            vkCmdCopyBuffer(cmd, cur_nrg.handle, staging_readback_.handle, 1, &region);
        }

        ctx.end_single_command(cmd);  // waits for completion

        // Map staging and read back
        void* mapped = nullptr;
        vkMapMemory(ctx.device, staging_readback_.memory, 0, staging_readback_.size, 0, &mapped);
        auto* base = static_cast<uint8_t*>(mapped);
        std::memcpy(out_positions.data(),  base,             pos_bytes);
        std::memcpy(out_velocities.data(), base + pos_bytes, vel_bytes);
        if (cur_nrg.handle != VK_NULL_HANDLE && !out_energies.empty())
            std::memcpy(out_energies.data(), base + pos_bytes + vel_bytes, nrg_bytes);
        vkUnmapMemory(ctx.device, staging_readback_.memory);
    } else {
        // HOST_VISIBLE path: direct map
        void* mapped = nullptr;
        vkMapMemory(ctx.device, cur_pos.memory, 0, pos_bytes, 0, &mapped);
        std::memcpy(out_positions.data(), mapped, pos_bytes);
        vkUnmapMemory(ctx.device, cur_pos.memory);

        vkMapMemory(ctx.device, cur_vel.memory, 0, vel_bytes, 0, &mapped);
        std::memcpy(out_velocities.data(), mapped, vel_bytes);
        vkUnmapMemory(ctx.device, cur_vel.memory);

        if (cur_nrg.handle != VK_NULL_HANDLE && !out_energies.empty()) {
            vkMapMemory(ctx.device, cur_nrg.memory, 0, nrg_bytes, 0, &mapped);
            std::memcpy(out_energies.data(), mapped, nrg_bytes);
            vkUnmapMemory(ctx.device, cur_nrg.memory);
        }
    }
}

// ── Write particle state into both ping-pong buffers ─────────────────────────

void ComputePipeline::write_particle_state(
    VulkanContext& ctx,
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<float>&     energies)
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    uint32_t     n         = static_cast<uint32_t>(positions.size());
    VkDeviceSize pos_bytes  = n * sizeof(glm::vec2);
    VkDeviceSize vel_bytes  = n * sizeof(glm::vec2);
    VkDeviceSize nrg_bytes  = n * sizeof(float);

    if (use_device_local_) {
        // Stage data into upload buffer, then copy to both ping-pong pairs
        VkDeviceSize total = pos_bytes + vel_bytes + (energies.empty() ? 0 : nrg_bytes);
        if (total > staging_upload_.size) return;  // safety

        void* mapped = nullptr;
        vkMapMemory(ctx.device, staging_upload_.memory, 0, total, 0, &mapped);
        auto* base = static_cast<uint8_t*>(mapped);
        std::memcpy(base,                         positions.data(),  pos_bytes);
        std::memcpy(base + pos_bytes,             velocities.data(), vel_bytes);
        if (!energies.empty())
            std::memcpy(base + pos_bytes + vel_bytes, energies.data(), nrg_bytes);
        vkUnmapMemory(ctx.device, staging_upload_.memory);

        // Copy staging → both A and B buffers
        auto copy_pair = [&](VkCommandBuffer cmd, const Buffer& dst, VkDeviceSize src_off, VkDeviceSize sz) {
            VkBufferCopy region{};
            region.srcOffset = src_off;
            region.dstOffset = 0;
            region.size = sz;
            vkCmdCopyBuffer(cmd, staging_upload_.handle, dst.handle, 1, &region);
        };

        VkCommandBuffer cmd = ctx.begin_single_command();
        copy_pair(cmd, pos_buffer_a_, 0, pos_bytes);
        copy_pair(cmd, pos_buffer_b_, 0, pos_bytes);
        copy_pair(cmd, vel_buffer_a_, pos_bytes, vel_bytes);
        copy_pair(cmd, vel_buffer_b_, pos_bytes, vel_bytes);
        if (!energies.empty()) {
            copy_pair(cmd, energy_buffer_a_, pos_bytes + vel_bytes, nrg_bytes);
            copy_pair(cmd, energy_buffer_b_, pos_bytes + vel_bytes, nrg_bytes);
        }
        ctx.end_single_command(cmd);
    } else {
        // HOST_VISIBLE path: direct map into both ping-pong pairs
        void* mapped = nullptr;
        auto write_buf = [&](const Buffer& buf, const void* data, VkDeviceSize sz) {
            if (buf.handle == VK_NULL_HANDLE || sz == 0) return;
            vkMapMemory(ctx.device, buf.memory, 0, sz, 0, &mapped);
            std::memcpy(mapped, data, static_cast<size_t>(sz));
            vkUnmapMemory(ctx.device, buf.memory);
        };

        write_buf(pos_buffer_a_, positions.data(),  pos_bytes);
        write_buf(pos_buffer_b_, positions.data(),  pos_bytes);
        write_buf(vel_buffer_a_, velocities.data(), vel_bytes);
        write_buf(vel_buffer_b_, velocities.data(), vel_bytes);

        if (!energies.empty()) {
            write_buf(energy_buffer_a_, energies.data(), nrg_bytes);
            write_buf(energy_buffer_b_, energies.data(), nrg_bytes);
        }
    }
}

// ── Record (called per frame while simulation is active) ──────────────────────

void ComputePipeline::record(VkCommandBuffer cmd,
                             const SimConfig& cfg,
                             float dt)
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    VkDescriptorSet active_set = (tick % 2 == 0) ? desc_set_a_ : desc_set_b_;
    uint32_t particle_count    = live_particle_count_;

    // Supersampling scale: render texture may be larger than REGION_W/H
    float rscale = static_cast<float>(render_w) / static_cast<float>(REGION_W);

    PushConstants pc{};
    // Physics step uses base world-space region; render steps use scaled texture size
    pc.region_size = {
        static_cast<float>(REGION_W) + cfg.radius * 2.0f,
        static_cast<float>(REGION_H) + cfg.radius * 2.0f
    };
    pc.camera_origin      = cfg.camera_origin;
    pc.particle_count     = particle_count;
    pc.particle_types     = MAX_PARTICLE_TYPES;
    pc.dt                 = dt;
    pc.camera_zoom        = cfg.current_camera_zoom;
    pc.radius             = cfg.radius;
    pc.dampening          = cfg.dampening;
    pc.repulsion_radius   = cfg.repulsion_radius;
    pc.interaction_radius = cfg.interaction_radius;
    pc.density_limit      = cfg.density_limit;
    pc.viscosity_strength   = cfg.viscosity_strength;
    pc.pressure_resistance  = cfg.pressure_resistance;
    pc.local_density_cap    = cfg.local_density_cap;
    pc.temperature          = cfg.temperature;
    pc.gravity_strength     = cfg.gravity_strength;
    pc.lorentz_strength     = cfg.effective_lorentz;
    pc.vacuum_energy        = cfg.vacuum_energy;
    pc.field_flags          = cfg.field_flags;
    pc.weak_coupling        = cfg.weak_coupling;
    pc.string_tension       = cfg.string_tension;
    pc.higgs_vev            = cfg.higgs_vev;
    pc.force_object_count   = cfg.force_object_count;
    pc.coulomb_strength     = cfg.coulomb_strength;
    pc.yukawa_strength      = cfg.yukawa_strength;
    pc.pauli_multiplier     = cfg.pauli_multiplier;
    pc.alpha_s_scale        = cfg.alpha_s_scale;
    pc.compton_strength     = cfg.compton_strength;
    pc.annihilation_strength = cfg.annihilation_strength;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &active_set, 0, nullptr);

    // ── Step 0: physics ───────────────────────────────────────────────────────
    pc.step = 0;
    dispatch(cmd, active_set, pc, particle_count);

    // Memory barrier: physics writes → render-to-texture reads
    VkMemoryBarrier mem_barrier{};
    mem_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &mem_barrier, 0, nullptr, 0, nullptr);

    // Switch to render-space coordinates for all texture operations
    pc.region_size = {
        static_cast<float>(render_w) + cfg.radius * rscale * 2.0f,
        static_cast<float>(render_h) + cfg.radius * rscale * 2.0f
    };
    pc.camera_zoom = cfg.current_camera_zoom * rscale;

    // ── Clear or fade render texture ────────────────────────────────────────────
    VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (cfg.show_trails) {
        // Trail mode: fade existing pixels via compute pass (step=3)
        PushConstants fade_pc = pc;
        fade_pc.step = 3;
        uint32_t pixel_count = render_w * render_h;
        uint32_t fade_groups = pixel_count / 256 + 1;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout_, 0, 1, &active_set, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(PushConstants), &fade_pc);
        vkCmdDispatch(cmd, fade_groups, 1, 1);

        // Barrier: fade writes → render reads
        VkMemoryBarrier fade_barrier{};
        fade_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        fade_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        fade_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &fade_barrier, 0, nullptr, 0, nullptr);
    } else {
        // Normal mode: clear to background
        VkClearColorValue clear_color{};
        clear_color.float32[0] = 0.01f;
        clear_color.float32[1] = 0.01f;
        clear_color.float32[2] = 0.04f;  // deep space blue-black
        clear_color.float32[3] = 1.0f;
        vkCmdClearColorImage(cmd,
            particle_texture.handle,
            VK_IMAGE_LAYOUT_GENERAL,
            &clear_color, 1, &range);
    }

    // Barrier: clear/fade → storage image write
    VkImageMemoryBarrier img_barrier{};
    img_barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.srcAccessMask       = cfg.show_trails ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    img_barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.image               = particle_texture.handle;
    img_barrier.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd,
        cfg.show_trails ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &img_barrier);

    // ── Step 5: gather-based field visualization (optional) ─────────────────────
    // field_flags bitfield: bit0=EM, 1=strong, 2=weak, 3=gravity, 4=Higgs, 12=DE
    // Bits 5+ (wave_mode, collision_radii, etc.) don't need field rendering
    // Also support legacy density_limit mode for chemistry sim
    uint32_t field_vis_bits = pc.field_flags & 0x101Fu;  // bits 0-4 + bit 12 (dark energy)
    if (field_vis_bits != 0u || (pc.density_limit >= 0.5f && pc.density_limit <= 3.5f)) {
        PushConstants field_pc = pc;
        field_pc.step = 5;
        uint32_t pixel_count = render_w * render_h;
        uint32_t field_groups = pixel_count / 256 + 1;
        vkCmdPushConstants(cmd, pipeline_layout_,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(PushConstants), &field_pc);
        vkCmdDispatch(cmd, field_groups, 1, 1);

        // Barrier: field writes → particle render reads
        VkMemoryBarrier field_barrier{};
        field_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        field_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        field_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &field_barrier, 0, nullptr, 0, nullptr);
    }

    // ── Step 1: render particles to texture ───────────────────────────────────
    pc.step = 1;
    dispatch(cmd, active_set, pc, particle_count);

    // ── Step 4: collision debug overlay (after all particles are rendered) ────
    if (pc.field_flags & (1u << 6)) {
        VkMemoryBarrier coll_barrier{};
        coll_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        coll_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        coll_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &coll_barrier, 0, nullptr, 0, nullptr);

        pc.step = 4;
        dispatch(cmd, active_set, pc, particle_count);
    }

    // Memory barrier: compute image write → fragment shader read
    img_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &img_barrier);

    tick++;
}

void ComputePipeline::dispatch(VkCommandBuffer cmd,
                               VkDescriptorSet dset,
                               const PushConstants& pc,
                               uint32_t particle_count)
{
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &dset, 0, nullptr);
    vkCmdPushConstants(cmd, pipeline_layout_,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(PushConstants), &pc);
    uint32_t groups = particle_count / GROUP_DENSITY + 1;
    vkCmdDispatch(cmd, groups, 1, 1);
}
