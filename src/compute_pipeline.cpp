#include "compute_pipeline.h"
#include <stdexcept>
#include <cstring>
#include <array>

// ── Init / Destroy ────────────────────────────────────────────────────────────

void ComputePipeline::init(VulkanContext& ctx, const std::string& shader_spv_path) {
    // Create the render texture (REGION_W × REGION_H, rgba32f)
    particle_texture = ctx.create_image(
        REGION_W, REGION_H,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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

// ── Descriptor set layout ─────────────────────────────────────────────────────
// Bindings 0-6: particle storage buffers
// Binding  7:   storage image (render texture)
// Binding  8:   behavior flags (shared, static per type)
// Bindings 9-12: polar angle/angular-velocity (double-buffered)

void ComputePipeline::create_descriptor_set_layout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 16> bindings{};

    // Bindings 0-6 and 8-15: storage buffers
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    for (uint32_t i = 8; i < 16; ++i) {
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
    // 2 sets × 15 storage buffers (7 orig + 1 behavior + 4 angle/avel + 2 energy + 1 genome) = 30
    // 2 sets × 1 storage image = 2
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 30 };
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
    const VkBufferUsageFlags    BUF_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags MEM_PROPS =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkDeviceSize pos_size      = particles.positions.size()  * sizeof(glm::vec2);
    VkDeviceSize vel_size      = particles.velocities.size() * sizeof(glm::vec2);
    VkDeviceSize type_size     = particles.types.size()       * sizeof(uint32_t);
    VkDeviceSize force_size    = particles.forces.size()      * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()      * sizeof(glm::vec4);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES            * sizeof(uint32_t);
    VkDeviceSize angle_size    = particles.angles.size()       * sizeof(float);

    pos_buffer_a_          = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);
    pos_buffer_b_          = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);
    vel_buffer_a_          = ctx.create_buffer(vel_size,      BUF_USAGE, MEM_PROPS);
    vel_buffer_b_          = ctx.create_buffer(vel_size,      BUF_USAGE, MEM_PROPS);
    type_buffer_           = ctx.create_buffer(type_size,     BUF_USAGE, MEM_PROPS);
    force_buffer_          = ctx.create_buffer(force_size,    BUF_USAGE, MEM_PROPS);
    color_buffer_          = ctx.create_buffer(color_size,    BUF_USAGE, MEM_PROPS);
    behavior_buffer_       = ctx.create_buffer(behavior_size, BUF_USAGE, MEM_PROPS);
    angle_buffer_a_        = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angle_buffer_b_        = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angular_vel_buffer_a_  = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angular_vel_buffer_b_  = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);

    // Upload initial data
    ctx.update_buffer(pos_buffer_a_,  particles.positions.data(),        pos_size);
    ctx.update_buffer(pos_buffer_b_,  particles.positions.data(),        pos_size);
    ctx.update_buffer(vel_buffer_a_,  particles.velocities.data(),       vel_size);
    ctx.update_buffer(vel_buffer_b_,  particles.velocities.data(),       vel_size);
    ctx.update_buffer(type_buffer_,   particles.types.data(),            type_size);
    ctx.update_buffer(force_buffer_,  particles.forces.data(),           force_size);
    ctx.update_buffer(color_buffer_,  particles.colors.data(),           color_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,        behavior_size);
    ctx.update_buffer(angle_buffer_a_,  particles.angles.data(),         angle_size);
    ctx.update_buffer(angle_buffer_b_,  particles.angles.data(),         angle_size);
    ctx.update_buffer(angular_vel_buffer_a_, particles.angular_velocities.data(), angle_size);
    ctx.update_buffer(angular_vel_buffer_b_, particles.angular_velocities.data(), angle_size);

    VkDeviceSize energy_size = particles.energies.size() * sizeof(float);
    energy_buffer_a_ = ctx.create_buffer(energy_size, BUF_USAGE, MEM_PROPS);
    energy_buffer_b_ = ctx.create_buffer(energy_size, BUF_USAGE, MEM_PROPS);
    ctx.update_buffer(energy_buffer_a_, particles.energies.data(), energy_size);
    ctx.update_buffer(energy_buffer_b_, particles.energies.data(), energy_size);

    VkDeviceSize genome_size = particles.genomes.size() * sizeof(float);
    genome_buffer_ = ctx.create_buffer(genome_size, BUF_USAGE, MEM_PROPS);
    ctx.update_buffer(genome_buffer_, particles.genomes.data(), genome_size);

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
    writes.reserve(32);
    buf_infos.reserve(30);
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

    vkUpdateDescriptorSets(ctx.device,
                           static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

// ── Per-frame dynamic uploads ─────────────────────────────────────────────────

void ComputePipeline::upload_dynamic_data(VulkanContext& ctx, const Particles& particles) {
    if (force_buffer_.handle == VK_NULL_HANDLE) return;

    // Apply per-type trait scales before uploading forces.
    // The UI edits particles.forces (base values); GPU receives the scaled version.
    float effective_forces[MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES];
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b)
        for (uint32_t a = 0; a < MAX_PARTICLE_TYPES; ++a) {
            uint32_t fi = a + b * MAX_PARTICLE_TYPES;
            effective_forces[fi] = particles.forces[fi] * particles.trait_scales[a];
        }

    VkDeviceSize force_size    = particles.forces.size()     * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()     * sizeof(glm::vec4);
    VkDeviceSize type_size     = particles.types.size()      * sizeof(uint32_t);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES           * sizeof(uint32_t);

    ctx.update_buffer(force_buffer_,    effective_forces,              force_size);
    ctx.update_buffer(color_buffer_,    particles.colors.data(),       color_size);
    ctx.update_buffer(type_buffer_,     particles.types.data(),        type_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,      behavior_size);

    if (genome_buffer_.handle != VK_NULL_HANDLE && !particles.genomes.empty()) {
        VkDeviceSize genome_size = particles.genomes.size() * sizeof(float);
        ctx.update_buffer(genome_buffer_, particles.genomes.data(), genome_size);
    }
}

void ComputePipeline::read_current_state(VulkanContext& ctx,
                                          std::vector<glm::vec2>& out_positions,
                                          std::vector<glm::vec2>& out_velocities,
                                          std::vector<float>&     out_energies) const
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
    VkDeviceSize nrg_bytes = static_cast<uint32_t>(out_energies.size()) * sizeof(float);

    void* mapped = nullptr;
    vkMapMemory(ctx.device, cur_pos.memory, 0, pos_bytes, 0, &mapped);
    std::memcpy(out_positions.data(), mapped, pos_bytes);
    vkUnmapMemory(ctx.device, cur_pos.memory);

    vkMapMemory(ctx.device, cur_vel.memory, 0, pos_bytes, 0, &mapped);
    std::memcpy(out_velocities.data(), mapped, pos_bytes);
    vkUnmapMemory(ctx.device, cur_vel.memory);

    if (cur_nrg.handle != VK_NULL_HANDLE && !out_energies.empty()) {
        vkMapMemory(ctx.device, cur_nrg.memory, 0, nrg_bytes, 0, &mapped);
        std::memcpy(out_energies.data(), mapped, nrg_bytes);
        vkUnmapMemory(ctx.device, cur_nrg.memory);
    }
}

// ── Record (called per frame while simulation is active) ──────────────────────

void ComputePipeline::record(VkCommandBuffer cmd,
                             const SimConfig& cfg,
                             float dt)
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    VkDescriptorSet active_set = (tick % 2 == 0) ? desc_set_a_ : desc_set_b_;
    uint32_t particle_count    = static_cast<uint32_t>(cfg.particle_count);

    PushConstants pc{};
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

    // ── Clear render texture ──────────────────────────────────────────────────
    VkClearColorValue clear_color{};
    clear_color.float32[0] = 0.0f;
    clear_color.float32[1] = 0.0f;
    clear_color.float32[2] = 0.0f;
    clear_color.float32[3] = 1.0f;
    VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd,
        particle_texture.handle,
        VK_IMAGE_LAYOUT_GENERAL,
        &clear_color, 1, &range);

    // Barrier: clear → storage image write
    VkImageMemoryBarrier img_barrier{};
    img_barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    img_barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.image               = particle_texture.handle;
    img_barrier.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &img_barrier);

    // ── Step 1: render particles to texture ───────────────────────────────────
    pc.step = 1;
    dispatch(cmd, active_set, pc, particle_count);

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
