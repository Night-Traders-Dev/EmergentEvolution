#pragma once

#include "vulkan_context.h"
#include "compute_pipeline.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

static constexpr int FRAMES_IN_FLIGHT = 2;

// ── Per-frame sync objects ────────────────────────────────────────────────────

struct FrameData {
    VkCommandBuffer cmd              = VK_NULL_HANDLE;
    VkSemaphore     image_available  = VK_NULL_HANDLE;
    VkSemaphore     render_finished  = VK_NULL_HANDLE;
    VkFence         in_flight        = VK_NULL_HANDLE;
};

// ── Renderer ──────────────────────────────────────────────────────────────────
// Owns the render pass, framebuffers, fullscreen-quad pipeline, and ImGui.

class Renderer {
public:
    bool swapchain_dirty = false;

    void init(VulkanContext& ctx,
              GLFWwindow*   window,
              ComputePipeline& compute);

    void destroy(VulkanContext& ctx);

    // Call after swapchain recreation to rebuild framebuffers
    void rebuild_framebuffers(VulkanContext& ctx);

    // Submit a complete frame: compute + fullscreen quad + ImGui
    // Returns false if swapchain needs recreation
    bool draw_frame(VulkanContext& ctx,
                    GLFWwindow*   window,
                    ComputePipeline& compute,
                    bool sim_active);

    // Called when framebuffer is resized
    void on_resize(VulkanContext& ctx, GLFWwindow* window, ComputePipeline& compute);

private:
    VkRenderPass              render_pass_          = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // Fullscreen-quad pipeline
    VkDescriptorSetLayout     quad_dset_layout_    = VK_NULL_HANDLE;
    VkPipelineLayout          quad_pipe_layout_    = VK_NULL_HANDLE;
    VkPipeline                quad_pipeline_       = VK_NULL_HANDLE;
    VkDescriptorPool          quad_desc_pool_      = VK_NULL_HANDLE;
    VkDescriptorSet           quad_desc_set_       = VK_NULL_HANDLE;

    // ImGui descriptor pool
    VkDescriptorPool          imgui_pool_          = VK_NULL_HANDLE;

    // Per-frame sync
    FrameData frames_[FRAMES_IN_FLIGHT]{};
    uint32_t  current_frame_ = 0;

    void create_render_pass(VulkanContext& ctx);
    void create_framebuffers(VulkanContext& ctx);
    void create_quad_pipeline(VulkanContext& ctx,
                              const std::string& vert_spv,
                              const std::string& frag_spv);
    void create_quad_descriptor_set(VulkanContext& ctx, ComputePipeline& compute);
    void create_sync_objects(VulkanContext& ctx);
    void init_imgui(VulkanContext& ctx, GLFWwindow* window);
    void destroy_framebuffers(VulkanContext& ctx);

    void record_command_buffer(VkCommandBuffer cmd,
                               uint32_t        image_index,
                               VulkanContext&  ctx,
                               ComputePipeline& compute,
                               bool            sim_active);
};
