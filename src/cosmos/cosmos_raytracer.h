#pragma once
// ── Cosmos GPU Raytracer ────────────────────────────────────────────────────
// Fullscreen sphere raytracing via a Vulkan graphics pipeline.
// Renders 3D-lit spheres with shadow rays, star lighting, and uniform modes.

#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_nebula_compute.h"
#include "common/vulkan_context.h"

class CosmosRaytracer {
public:
    void init(VulkanContext& vk, VkRenderPass render_pass);
    void destroy(VulkanContext& vk);

    // Upload sphere data + camera uniforms, then draw fullscreen triangle.
    // Must be called while a render pass is active on `cmd`.
    void update_and_draw(VulkanContext& vk, VkCommandBuffer cmd,
                         const CosmosState& state, const OrbitCamera& camera,
                         const CosmosConfig& cfg,
                         float screen_w, float screen_h, float time);

private:
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipe_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_    = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       desc_set_    = VK_NULL_HANDLE;

    Buffer camera_ubo_;
    Buffer sphere_ssbo_;
    Buffer nebula_ssbo_;
    Image  nebula_volume_a_;
    Image  nebula_volume_b_;
    VkSampler nebula_volume_sampler_ = VK_NULL_HANDLE;
    bool   nebula_volume_flip_ = false;
    CosmosNebulaCompute nebula_compute_;
    static constexpr uint32_t NEBULA_VOLUME_DIM = 64;

    static constexpr int MAX_SPHERES = 2048;
};
