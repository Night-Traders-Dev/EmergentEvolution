#pragma once
// ── Biochem GPU Raytracer ──────────────────────────────────────────────────
// Fullscreen sphere raytracing via a Vulkan graphics pipeline.
// Renders 3D biological entities with organic lighting and subsurface scatter.

#include "biochem/biochem_types.h"
#include "common/vulkan_context.h"

class BiochemRaytracer {
public:
    void init(VulkanContext& vk, VkRenderPass render_pass);
    void destroy(VulkanContext& vk);

    // Upload sphere data + camera uniforms, then draw fullscreen triangle.
    // Must be called while a render pass is active on `cmd`.
    void update_and_draw(VulkanContext& vk, VkCommandBuffer cmd,
                         const BiochemState& state,
                         const OrbitCamera& camera,
                         const BiochemConfig& cfg,
                         float screen_w, float screen_h, float time);

private:
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipe_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_    = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       desc_set_    = VK_NULL_HANDLE;

    Buffer camera_ubo_;
    Buffer sphere_ssbo_;

    static constexpr int MAX_SPHERES = 1024;
};
