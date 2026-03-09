#pragma once
// ── Cosmos GPU Raytracer ────────────────────────────────────────────────────
// Fullscreen sphere raytracing via a Vulkan graphics pipeline.
// Renders 3D-lit spheres with shadow rays, star lighting, and uniform modes.

#include "cosmos/cosmos_types.h"
#include "cosmos/rendering/cosmos_nebula_compute.h"
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
                         float screen_w, float screen_h, float time,
                         const std::vector<bool>* skip_bodies = nullptr);

private:
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipe_layout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_    = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_   = VK_NULL_HANDLE;
    VkDescriptorSet       desc_set_    = VK_NULL_HANDLE;

    Buffer camera_ubo_;
    Buffer sphere_ssbo_;
    Buffer nebula_ssbo_;
    Buffer gravity_well_ssbo_;
    Image  nebula_volume_a_;
    Image  nebula_volume_b_;
    VkSampler nebula_volume_sampler_ = VK_NULL_HANDLE;
    bool   nebula_volume_flip_ = false;
    CosmosNebulaCompute nebula_compute_;
    static constexpr uint32_t NEBULA_VOLUME_DIM = 64;

    static constexpr int MAX_SPHERES = 2048;
    static constexpr int MAX_GRAVITY_WELLS = 64;

public:
    // Spawn preview ghost sphere (rendered semi-transparent by the shader).
    // Points to a fully initialized CelestialBody owned by CosmosApp.
    // The raytracer packs it into SphereGPU using the same code as real bodies,
    // but sets base_emit.a = -2.0 to signal ghost mode in the shader.
    struct PreviewData {
        const CelestialBody* body = nullptr;  // null = inactive
        bool active() const { return body != nullptr; }
    };
    PreviewData preview;
};
