#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include <vulkan/vulkan.h>
#include <string>

// Mirrors pipeline.gd.
// Owns the compute pipeline, double-buffered particle storage buffers,
// the particle render texture, and the descriptor sets.

class ComputePipeline {
public:
    // The storage image written by the compute shader.
    // Renderer reads this to display the simulation.
    Image         particle_texture{};
    VkImageView   particle_texture_view = VK_NULL_HANDLE; // same as particle_texture.view
    VkSampler     sampler               = VK_NULL_HANDLE;

    int tick = 0;

    // Render texture dimensions (may differ from REGION_W/H with supersampling)
    uint32_t render_w = REGION_W;
    uint32_t render_h = REGION_H;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    void init(VulkanContext& ctx, const std::string& shader_spv_path);
    void destroy(VulkanContext& ctx);

    // Recreate render texture at new dimensions (for supersampling).
    // Queue must be idle. Updates compute + renderer descriptor sets.
    void resize_render_texture(VulkanContext& ctx, uint32_t w, uint32_t h);

    // Create / destroy particle buffers (called on reset)
    void create_buffers(VulkanContext& ctx, const Particles& particles);
    void clear_buffers(VulkanContext& ctx);

    // Record the two compute dispatches into cmd for one simulation frame.
    // step=0: physics,  step=1: render-to-texture
    void record(VkCommandBuffer cmd,
                const SimConfig& cfg,
                float dt);

    // Upload force + color arrays (called each frame before record())
    void upload_dynamic_data(VulkanContext& ctx,
                             const Particles& particles);

    // Upload force objects (called each frame before record())
    void upload_force_objects(VulkanContext& ctx,
                              const ForceObject* objects);

    bool is_ready() const { return pos_buffer_a_.handle != VK_NULL_HANDLE; }

    // Expose force object buffer for overlay pipeline rendering
    VkBuffer get_force_object_buffer() const { return force_obj_buffer_.handle; }

    // Upload CPU-built spatial grid for GPU neighbor lookup
    void upload_gpu_grid(VulkanContext& ctx,
                         const std::vector<uint32_t>& cell_start,
                         const std::vector<uint32_t>& sorted_indices);

    // Read current particle positions, velocities, and energies back to CPU.
    // Safe to call after end_single_command() (queue is idle).
    void read_current_state(VulkanContext& ctx,
                            std::vector<glm::vec2>& out_positions,
                            std::vector<glm::vec2>& out_velocities,
                            std::vector<float>&     out_energies);

    // Write modified positions, velocities, and energies into both ping-pong buffers.
    // Used for particle injection (spawn). Queue must be idle when called.
    void write_particle_state(VulkanContext& ctx,
                              const std::vector<glm::vec2>& positions,
                              const std::vector<glm::vec2>& velocities,
                              const std::vector<float>&     energies);

    // Write modified angles and angular velocities into both ping-pong buffers.
    // Used after type sort permutation. Queue must be idle when called.
    void write_angle_state(VulkanContext& ctx,
                           const std::vector<float>& angles,
                           const std::vector<float>& angular_velocities);

private:
    VkPipeline            pipeline_             = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_      = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_      = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_            = VK_NULL_HANDLE;

    // Double-buffered particle position + velocity buffers
    Buffer pos_buffer_a_{};
    Buffer pos_buffer_b_{};
    Buffer vel_buffer_a_{};
    Buffer vel_buffer_b_{};
    Buffer type_buffer_{};
    Buffer force_buffer_{};
    Buffer color_buffer_{};

    // Behavior flags (shared, static per reset)
    Buffer behavior_buffer_{};

    // Double-buffered polar orientation (angle + angular velocity)
    Buffer angle_buffer_a_{};
    Buffer angle_buffer_b_{};
    Buffer angular_vel_buffer_a_{};
    Buffer angular_vel_buffer_b_{};

    // Double-buffered per-particle energy (bindings 13 & 14)
    Buffer energy_buffer_a_{};
    Buffer energy_buffer_b_{};

    // Per-particle genome (binding 15, readonly, single buffer — CPU-managed)
    Buffer genome_buffer_{};

    // Bond partners (binding 16, readonly, CPU-managed by BondManager)
    // Layout: bond_partners[i * MAX_BONDS_PER_PARTICLE + slot] = partner index
    Buffer bond_buffer_{};

    // Force objects (binding 17, readonly, CPU-managed, not double-buffered)
    Buffer force_obj_buffer_{};

    // Per-type mass inverse + ZPE lookup (binding 18, readonly, static per reset)
    // Layout: [type*2+0] = mass_inv, [type*2+1] = zpe
    Buffer mass_zpe_buffer_{};

    // GPU spatial grid (binding 19-20, readonly, CPU-built each frame)
    Buffer grid_cell_start_buffer_{};  // uint32_t[GPU_GRID_CELLS+1] prefix-sum
    Buffer grid_indices_buffer_{};     // uint32_t[MAX_GPU_PARTICLES] sorted indices
    static constexpr uint32_t GPU_GRID_CELLS   = 103 * 58;  // 5974
    static constexpr uint32_t MAX_GPU_PARTICLES = 200000;

    // Bloom post-processing ping-pong images (bindings 21-22) at half resolution
    Image bloom_image_a_{};
    Image bloom_image_b_{};
    uint32_t bloom_w_ = REGION_W / 2;
    uint32_t bloom_h_ = REGION_H / 2;

    // Actual particle count the buffers were allocated for (safe dispatch cap)
    uint32_t live_particle_count_ = 0;

    // DEVICE_LOCAL optimization: staging buffers for discrete GPUs
    bool   use_device_local_  = false;   // true when particle buffers are in VRAM
    Buffer staging_upload_{};            // HOST_VISIBLE for CPU→GPU transfers
    Buffer staging_readback_{};          // HOST_VISIBLE for GPU→CPU transfers

    // Reusable scratch buffer for scaled forces (avoids 18.5KB stack alloc per frame)
    std::vector<float> effective_forces_;

    // Descriptor sets: set_a uses (a→in, b→out), set_b uses (b→in, a→out)
    VkDescriptorSet desc_set_a_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_b_ = VK_NULL_HANDLE;

    void create_descriptor_set_layout(VkDevice device);
    void create_pipeline_layout(VkDevice device);
    void create_compute_pipeline(VulkanContext& ctx,
                                 const std::string& shader_spv_path);
    void create_descriptor_pool(VkDevice device);
    void allocate_and_write_descriptor_sets(VulkanContext& ctx);

    void dispatch(VkCommandBuffer cmd,
                  VkDescriptorSet dset,
                  const PushConstants& pc,
                  uint32_t particle_count);
};
