#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <cstdint>

// ── Queue family indices ──────────────────────────────────────────────────────

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_compute; // single family for both
    std::optional<uint32_t> present;

    bool is_complete() const {
        return graphics_compute.has_value() && present.has_value();
    }
};

// ── Swapchain support details ─────────────────────────────────────────────────

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

// ── Buffer + memory bundle ────────────────────────────────────────────────────

struct Buffer {
    VkBuffer       handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
};

// ── Image + view + memory bundle ─────────────────────────────────────────────

struct Image {
    VkImage        handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
};

// ── Main context class ────────────────────────────────────────────────────────

class VulkanContext {
public:
    // Public handles needed by pipelines
    VkInstance               instance        = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device = VK_NULL_HANDLE;
    VkDevice                 device          = VK_NULL_HANDLE;
    VkQueue                  queue           = VK_NULL_HANDLE; // graphics + compute + present
    uint32_t                 queue_family    = 0;
    VkSurfaceKHR             surface         = VK_NULL_HANDLE;
    VkCommandPool            cmd_pool        = VK_NULL_HANDLE;

    VkSwapchainKHR           swapchain       = VK_NULL_HANDLE;
    VkFormat                 swapchain_format{};
    VkExtent2D               swapchain_extent{};
    std::vector<VkImage>     swapchain_images;
    std::vector<VkImageView> swapchain_views;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init(GLFWwindow* window);
    void recreate_swapchain(GLFWwindow* window);
    void destroy();

    // ── Buffer helpers ────────────────────────────────────────────────────────
    Buffer create_buffer(VkDeviceSize          size,
                         VkBufferUsageFlags    usage,
                         VkMemoryPropertyFlags props);

    void update_buffer(Buffer& buf, const void* data, VkDeviceSize size);

    void destroy_buffer(Buffer& buf);

    // ── Image helpers ─────────────────────────────────────────────────────────
    Image create_image(uint32_t              w,
                       uint32_t              h,
                       VkFormat              format,
                       VkImageUsageFlags     usage,
                       VkMemoryPropertyFlags props);

    VkImageView create_image_view(VkImage      image,
                                  VkFormat     format,
                                  VkImageAspectFlags aspect);

    void transition_image_layout(VkImage       image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout);

    void destroy_image(Image& img);

    // ── Command helpers ───────────────────────────────────────────────────────
    VkCommandBuffer begin_single_command();
    void            end_single_command(VkCommandBuffer cmd);

    // ── Shader helpers ────────────────────────────────────────────────────────
    VkShaderModule create_shader_module(const std::string& path);

    // ── Sampler helpers ───────────────────────────────────────────────────────
    VkSampler create_sampler_nearest();

    // ── Misc ──────────────────────────────────────────────────────────────────
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props);

private:
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

    void create_instance();
    void setup_debug_messenger();
    void create_surface(GLFWwindow* window);
    void pick_physical_device();
    void create_logical_device();
    void create_command_pool();
    void create_swapchain(GLFWwindow* window);
    void create_swapchain_image_views();
    void cleanup_swapchain();

    QueueFamilyIndices      find_queue_families(VkPhysicalDevice dev);
    SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice dev);
    VkSurfaceFormatKHR      choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR        choose_present_mode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D              choose_extent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& caps);
    bool                    is_device_suitable(VkPhysicalDevice dev);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*,
        void*);
};
