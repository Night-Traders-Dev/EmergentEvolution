#pragma once
// ── SimpleRenderer ──────────────────────────────────────────────────────────
// Minimal Vulkan swap-chain + ImGui rendering.  No compute pipeline, no
// fullscreen-quad — just presents an ImGui frame each tick.
// Used by the launcher and expansion skeletons.
// Optionally supports a depth buffer (needed for 3D mesh rendering).

#include "common/vulkan_context.h"
#include <vector>

struct GLFWwindow;

struct SimpleRenderer {
    // ── Lifecycle ────────────────────────────────────────────────────────────
    void init(VulkanContext& vk, GLFWwindow* window, bool with_depth = false);
    void destroy(VulkanContext& vk);

    // Call between begin/end to issue ImGui draw commands.
    bool begin_frame(VulkanContext& vk, GLFWwindow* window);   // false = skip frame
    void end_frame(VulkanContext& vk);

    bool swapchain_dirty = false;

    // Public accessors for custom rendering passes (e.g. cosmos raytracer)
    VkRenderPass    render_pass()  const { return render_pass_; }
    VkCommandBuffer current_cmd()  const { return cmd_bufs_[frame_idx_]; }
    uint32_t        current_image_index() const { return image_idx_; }
    bool            has_depth()    const { return depth_enabled_; }

private:
    void create_render_pass(VulkanContext& vk);
    void create_framebuffers(VulkanContext& vk);
    void destroy_framebuffers(VulkanContext& vk);
    void create_depth_resources(VulkanContext& vk);
    void destroy_depth_resources(VulkanContext& vk);
    void rebuild_swapchain(VulkanContext& vk, GLFWwindow* window);

    VkRenderPass               render_pass_  = VK_NULL_HANDLE;
    VkDescriptorPool           imgui_pool_   = VK_NULL_HANDLE;
    VkCommandPool              cmd_pool_     = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_bufs_;
    std::vector<VkFramebuffer> framebuffers_;

    // Depth buffer (optional, enabled via init with_depth=true)
    bool           depth_enabled_ = false;
    VkImage        depth_image_   = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_  = VK_NULL_HANDLE;
    VkImageView    depth_view_    = VK_NULL_HANDLE;
    static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

    // Per-frame sync
    static constexpr int MAX_FRAMES = 2;
    VkSemaphore img_available_[MAX_FRAMES]{};
    VkSemaphore render_done_[MAX_FRAMES]{};
    VkFence     in_flight_[MAX_FRAMES]{};
    uint32_t    frame_idx_  = 0;
    uint32_t    image_idx_  = 0;
};
