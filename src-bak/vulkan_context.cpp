#include "vulkan_context.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>
#include <set>
#include <limits>
#include <algorithm>
#include <vector>

// ── Validation layers ─────────────────────────────────────────────────────────
#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

static const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};
static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#define VK_CHECK(call) \
    do { \
        VkResult _r = (call); \
        if (_r != VK_SUCCESS) \
            throw std::runtime_error(std::string("Vulkan error at line " \
                __FILE__ ":") + std::to_string(__LINE__) + " code=" + std::to_string(_r)); \
    } while (0)

// ── Init / Destroy ────────────────────────────────────────────────────────────

void VulkanContext::init(GLFWwindow* window) {
    create_instance();
    if (ENABLE_VALIDATION) setup_debug_messenger();
    create_surface(window);
    pick_physical_device();
    create_logical_device();
    create_command_pool();
    create_swapchain(window);
    create_swapchain_image_views();
}

void VulkanContext::destroy() {
    cleanup_swapchain();
    vkDestroyCommandPool(device, cmd_pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    if (ENABLE_VALIDATION && debug_messenger_ != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance, debug_messenger_, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
}

void VulkanContext::recreate_swapchain(GLFWwindow* window) {
    vkDeviceWaitIdle(device);
    cleanup_swapchain();
    create_swapchain(window);
    create_swapchain_image_views();
}

void VulkanContext::cleanup_swapchain() {
    for (auto v : swapchain_views) vkDestroyImageView(device, v, nullptr);
    swapchain_views.clear();
    swapchain_images.clear();
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

// ── Instance ──────────────────────────────────────────────────────────────────

void VulkanContext::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Particle Life";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName      = "None";
    app_info.apiVersion       = VK_API_VERSION_1_2;

    // GLFW required extensions
    uint32_t     glfw_count = 0;
    const char** glfw_exts  = glfwGetRequiredInstanceExtensions(&glfw_count);
    std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_count);
    if (ENABLE_VALIDATION)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    if (ENABLE_VALIDATION) {
        create_info.enabledLayerCount   = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        create_info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }

    VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));
}

// ── Debug messenger ───────────────────────────────────────────────────────────

void VulkanContext::setup_debug_messenger() {
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_callback;

    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn) fn(instance, &ci, nullptr, &debug_messenger_);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << "\n";
    return VK_FALSE;
}

// ── Surface ───────────────────────────────────────────────────────────────────

void VulkanContext::create_surface(GLFWwindow* window) {
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
}

// ── Physical device ───────────────────────────────────────────────────────────

QueueFamilyIndices VulkanContext::find_queue_families(VkPhysicalDevice dev) {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        bool gfx     = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        bool compute = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)  != 0;

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present_support);

        if (gfx && compute && !indices.graphics_compute.has_value())
            indices.graphics_compute = i;

        if (present_support && !indices.present.has_value())
            indices.present = i;

        if (indices.is_complete()) break;
    }
    return indices;
}

SwapchainSupportDetails VulkanContext::query_swapchain_support(VkPhysicalDevice dev) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &count, nullptr);
    if (count) { details.formats.resize(count); vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &count, details.formats.data()); }

    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &count, nullptr);
    if (count) { details.present_modes.resize(count); vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &count, details.present_modes.data()); }

    return details;
}

bool VulkanContext::is_device_suitable(VkPhysicalDevice dev) {
    auto indices = find_queue_families(dev);
    if (!indices.is_complete()) return false;

    // Check extension support
    uint32_t ext_count;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> avail(ext_count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, avail.data());

    std::set<std::string> required(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
    for (auto& e : avail) required.erase(e.extensionName);
    if (!required.empty()) return false;

    auto sc = query_swapchain_support(dev);
    return !sc.formats.empty() && !sc.present_modes.empty();
}

void VulkanContext::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    // Prefer discrete GPU
    for (auto dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (is_device_suitable(dev) &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physical_device = dev;
            return;
        }
    }
    // Fallback to any suitable device
    for (auto dev : devices) {
        if (is_device_suitable(dev)) { physical_device = dev; return; }
    }
    throw std::runtime_error("No suitable GPU found");
}

// ── Logical device ────────────────────────────────────────────────────────────

void VulkanContext::create_logical_device() {
    auto indices = find_queue_families(physical_device);
    queue_family = indices.graphics_compute.value();

    std::set<uint32_t> unique_families = { indices.graphics_compute.value(),
                                            indices.present.value() };
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    for (uint32_t fam : unique_families) {
        VkDeviceQueueCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = fam;
        ci.queueCount       = 1;
        ci.pQueuePriorities = &priority;
        queue_cis.push_back(ci);
    }

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos       = queue_cis.data();
    ci.pEnabledFeatures        = &features;
    ci.enabledExtensionCount   = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
    ci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
    if (ENABLE_VALIDATION) {
        ci.enabledLayerCount   = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }

    VK_CHECK(vkCreateDevice(physical_device, &ci, nullptr, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);
}

// ── Command pool ──────────────────────────────────────────────────────────────

void VulkanContext::create_command_pool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = queue_family;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmd_pool));
}

// ── Swapchain ─────────────────────────────────────────────────────────────────

VkSurfaceFormatKHR VulkanContext::choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR VulkanContext::choose_present_mode(
    const std::vector<VkPresentModeKHR>& modes)
{
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::choose_extent(
    GLFWwindow* window,
    const VkSurfaceCapabilitiesKHR& caps)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    VkExtent2D extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    extent.width  = std::clamp(extent.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

void VulkanContext::create_swapchain(GLFWwindow* window) {
    auto support = query_swapchain_support(physical_device);
    auto format  = choose_surface_format(support.formats);
    auto mode    = choose_present_mode(support.present_modes);
    auto extent  = choose_extent(window, support.capabilities);

    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0)
        image_count = std::min(image_count, support.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface;
    ci.minImageCount    = image_count;
    ci.imageFormat      = format.format;
    ci.imageColorSpace  = format.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = support.capabilities.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = mode;
    ci.clipped          = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));

    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());

    swapchain_format = format.format;
    swapchain_extent = extent;
}

void VulkanContext::create_swapchain_image_views() {
    swapchain_views.resize(swapchain_images.size());
    for (size_t i = 0; i < swapchain_images.size(); ++i)
        swapchain_views[i] = create_image_view(
            swapchain_images[i], swapchain_format, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ── Buffer helpers ────────────────────────────────────────────────────────────

uint32_t VulkanContext::find_memory_type(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type");
}

Buffer VulkanContext::create_buffer(VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags props)
{
    Buffer buf;
    buf.size = size;

    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &ci, nullptr, &buf.handle));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf.handle, &req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &buf.memory));

    vkBindBufferMemory(device, buf.handle, buf.memory, 0);
    return buf;
}

void VulkanContext::update_buffer(Buffer& buf, const void* data, VkDeviceSize size) {
    void* mapped;
    vkMapMemory(device, buf.memory, 0, size, 0, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, buf.memory);
}

void VulkanContext::destroy_buffer(Buffer& buf) {
    if (buf.handle != VK_NULL_HANDLE) vkDestroyBuffer(device, buf.handle, nullptr);
    if (buf.memory != VK_NULL_HANDLE) vkFreeMemory(device, buf.memory, nullptr);
    buf = {};
}

// ── Image helpers ─────────────────────────────────────────────────────────────

Image VulkanContext::create_image(uint32_t w, uint32_t h,
                                  VkFormat format,
                                  VkImageUsageFlags usage,
                                  VkMemoryPropertyFlags props)
{
    Image img;

    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = format;
    ci.extent        = { w, h, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &ci, nullptr, &img.handle));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, img.handle, &req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &img.memory));

    vkBindImageMemory(device, img.handle, img.memory, 0);
    return img;
}

VkImageView VulkanContext::create_image_view(VkImage image, VkFormat format,
                                             VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo ci{};
    ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image                           = image;
    ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ci.format                          = format;
    ci.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY };
    ci.subresourceRange.aspectMask     = aspect;
    ci.subresourceRange.baseMipLevel   = 0;
    ci.subresourceRange.levelCount     = 1;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount     = 1;
    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &ci, nullptr, &view));
    return view;
}

void VulkanContext::transition_image_layout(VkImage image,
                                            VkImageLayout old_layout,
                                            VkImageLayout new_layout)
{
    VkCommandBuffer cmd = begin_single_command();

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = old_layout;
    barrier.newLayout           = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkPipelineStageFlags src_stage{}, dst_stage{};
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    end_single_command(cmd);
}

void VulkanContext::destroy_image(Image& img) {
    if (img.view   != VK_NULL_HANDLE) vkDestroyImageView(device, img.view, nullptr);
    if (img.handle != VK_NULL_HANDLE) vkDestroyImage(device, img.handle, nullptr);
    if (img.memory != VK_NULL_HANDLE) vkFreeMemory(device, img.memory, nullptr);
    img = {};
}

// ── Command helpers ───────────────────────────────────────────────────────────

VkCommandBuffer VulkanContext::begin_single_command() {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = cmd_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

void VulkanContext::end_single_command(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmd_pool, 1, &cmd);
}

// ── Shader module ─────────────────────────────────────────────────────────────

VkShaderModule VulkanContext::create_shader_module(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader: " + path);

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> code(size);
    file.read(code.data(), size);

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

// ── Sampler ───────────────────────────────────────────────────────────────────

VkSampler VulkanContext::create_sampler_nearest() {
    VkSamplerCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter    = VK_FILTER_NEAREST;
    ci.minFilter    = VK_FILTER_NEAREST;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(device, &ci, nullptr, &sampler));
    return sampler;
}
