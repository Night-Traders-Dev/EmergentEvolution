#pragma once
// ── Cosmos Mesh Renderer ────────────────────────────────────────────────────
// Vulkan graphics pipeline for rendering quad-sphere terrain meshes.
// Renders close-up celestial bodies with procedural terrain displacement.
// Uses push constants (128 bytes) for per-body MVP + coloring params.
// Designed for performance: single VBO/IBO, frustum culling, LOD gating.

#include "cosmos/cosmos_types.h"
#include "cosmos/terrain/cosmos_quadsphere.h"
#include "common/vulkan_context.h"
#include "common/orbit_camera.h"
#include <glm/glm.hpp>
#include <vector>

// Forward declaration — matches CosmosApp::TerrainMeshEntry
struct TerrainMeshEntry {
    QuadSphereMesh mesh;
    uint32_t       seed   = 0;
    float          radius = 0.0f;
    bool           valid  = false;
};

class CosmosMeshRenderer {
public:
    void init(VulkanContext& vk, VkRenderPass render_pass);
    void destroy(VulkanContext& vk);

    // Upload mesh data from terrain cache to GPU buffers.
    // Call when terrain cache changes (body added/removed, LOD update).
    void upload_meshes(VulkanContext& vk,
                       const std::vector<TerrainMeshEntry>& cache,
                       const std::vector<CelestialBody>& bodies);

    // Draw all uploaded terrain meshes within the active render pass.
    // Performs frustum culling and screen-size gating internally.
    void draw(VkCommandBuffer cmd,
              const std::vector<CelestialBody>& bodies,
              const OrbitCamera& camera,
              float screen_w, float screen_h,
              float sim_time);

    // Minimum screen-space diameter (pixels) before a body gets terrain mesh
    float min_screen_pixels = 12.0f;

private:
    VkPipeline       pipeline_    = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;

    Buffer vertex_buffer_;
    Buffer index_buffer_;

    // Push constant layout (128 bytes total, Vulkan guaranteed minimum)
    struct PushConstants {
        glm::mat4 mvp;             // 64 bytes
        glm::vec4 cam_pos_time;    // 16 bytes — xyz=camera pos (camera-relative), w=time
        glm::vec4 body_params;     // 16 bytes — x=render_class, y=temperature, z=ocean_level, w=radius
        glm::vec4 color_params;    // 16 bytes — rgb=base color, a=emissive
        glm::vec4 surface_params;  // 16 bytes — x=terrain_amp, y=ice_coverage, z=rock_frac, w=ocean_frac
    };
    static_assert(sizeof(PushConstants) == 128, "Push constants must be exactly 128 bytes");

    // Per-body draw entry — stored after upload_meshes()
    struct DrawEntry {
        uint32_t vertex_offset;
        uint32_t index_offset;
        uint32_t index_count;
        int      body_index;
    };
    std::vector<DrawEntry> draw_entries_;

    uint32_t total_vertices_ = 0;
    uint32_t total_indices_  = 0;

    // Budget: ~200K vertices (~9.6 MB), ~600K indices (~2.4 MB)
    static constexpr uint32_t MAX_VERTICES = 200000;
    static constexpr uint32_t MAX_INDICES  = 600000;
};
