#include "common/simple_renderer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <algorithm>

// ── Lifecycle ────────────────────────────────────────────────────────────────

void SimpleRenderer::init(VulkanContext& vk, GLFWwindow* window, bool with_depth) {
    depth_enabled_ = with_depth;
    create_render_pass(vk);
    if (depth_enabled_) create_depth_resources(vk);
    create_framebuffers(vk);

    // Command pool + buffers
    VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = vk.queue_family;
    vkCreateCommandPool(vk.device, &pool_ci, nullptr, &cmd_pool_);

    cmd_bufs_.resize(MAX_FRAMES);
    VkCommandBufferAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_ci.commandPool        = cmd_pool_;
    alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = MAX_FRAMES;
    vkAllocateCommandBuffers(vk.device, &alloc_ci, cmd_bufs_.data());

    // Sync objects
    VkSemaphoreCreateInfo sem_ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fen_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fen_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES; i++) {
        vkCreateSemaphore(vk.device, &sem_ci, nullptr, &img_available_[i]);
        vkCreateSemaphore(vk.device, &sem_ci, nullptr, &render_done_[i]);
        vkCreateFence(vk.device,     &fen_ci, nullptr, &in_flight_[i]);
    }

    // Descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    };
    VkDescriptorPoolCreateInfo dp_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp_ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp_ci.maxSets       = 100;
    dp_ci.poolSizeCount = 1;
    dp_ci.pPoolSizes    = pool_sizes;
    vkCreateDescriptorPool(vk.device, &dp_ci, nullptr, &imgui_pool_);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo vk_info{};
    vk_info.Instance       = vk.instance;
    vk_info.PhysicalDevice = vk.physical_device;
    vk_info.Device         = vk.device;
    vk_info.QueueFamily    = vk.queue_family;
    vk_info.Queue          = vk.queue;
    vk_info.DescriptorPool = imgui_pool_;
    vk_info.MinImageCount  = static_cast<uint32_t>(vk.swapchain_images.size());
    vk_info.ImageCount     = static_cast<uint32_t>(vk.swapchain_images.size());
    vk_info.RenderPass     = render_pass_;
    ImGui_ImplVulkan_Init(&vk_info);

    // Upload fonts
    ImGui_ImplVulkan_CreateFontsTexture();
}

void SimpleRenderer::destroy(VulkanContext& vk) {
    vkDeviceWaitIdle(vk.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    destroy_framebuffers(vk);
    if (depth_enabled_) destroy_depth_resources(vk);

    for (int i = 0; i < MAX_FRAMES; i++) {
        vkDestroySemaphore(vk.device, img_available_[i], nullptr);
        vkDestroySemaphore(vk.device, render_done_[i], nullptr);
        vkDestroyFence(vk.device, in_flight_[i], nullptr);
    }
    vkDestroyCommandPool(vk.device, cmd_pool_, nullptr);
    vkDestroyDescriptorPool(vk.device, imgui_pool_, nullptr);
    vkDestroyRenderPass(vk.device, render_pass_, nullptr);
}

// ── Frame ────────────────────────────────────────────────────────────────────

bool SimpleRenderer::begin_frame(VulkanContext& vk, GLFWwindow* window) {
    if (swapchain_dirty) {
        rebuild_swapchain(vk, window);
        swapchain_dirty = false;
    }

    vkWaitForFences(vk.device, 1, &in_flight_[frame_idx_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        vk.device, vk.swapchain, UINT64_MAX,
        img_available_[frame_idx_], VK_NULL_HANDLE, &image_idx_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild_swapchain(vk, window);
        return false;
    }

    vkResetFences(vk.device, 1, &in_flight_[frame_idx_]);
    vkResetCommandBuffer(cmd_bufs_[frame_idx_], 0);

    VkCommandBufferBeginInfo begin_ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd_bufs_[frame_idx_], &begin_ci);

    VkClearValue clears[2]{};
    clears[0].color = {{0.06f, 0.06f, 0.10f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_ci{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_ci.renderPass        = render_pass_;
    rp_ci.framebuffer       = framebuffers_[image_idx_];
    rp_ci.renderArea.extent = vk.swapchain_extent;
    rp_ci.clearValueCount   = depth_enabled_ ? 2u : 1u;
    rp_ci.pClearValues      = clears;
    vkCmdBeginRenderPass(cmd_bufs_[frame_idx_], &rp_ci, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    return true;
}

void SimpleRenderer::end_frame(VulkanContext& vk) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_bufs_[frame_idx_]);

    vkCmdEndRenderPass(cmd_bufs_[frame_idx_]);
    vkEndCommandBuffer(cmd_bufs_[frame_idx_]);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &img_available_[frame_idx_];
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd_bufs_[frame_idx_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &render_done_[frame_idx_];
    vkQueueSubmit(vk.queue, 1, &submit, in_flight_[frame_idx_]);

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &render_done_[frame_idx_];
    present.swapchainCount     = 1;
    present.pSwapchains        = &vk.swapchain;
    present.pImageIndices      = &image_idx_;

    VkResult result = vkQueuePresentKHR(vk.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        swapchain_dirty = true;

    frame_idx_ = (frame_idx_ + 1) % MAX_FRAMES;
}

// ── Internal ─────────────────────────────────────────────────────────────────

void SimpleRenderer::create_render_pass(VulkanContext& vk) {
    VkAttachmentDescription attachments[2]{};

    // Color attachment (always present)
    attachments[0].format         = vk.swapchain_format;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment (optional)
    attachments[1].format         = DEPTH_FORMAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;
    subpass.pDepthStencilAttachment = depth_enabled_ ? &depth_ref : nullptr;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        (depth_enabled_ ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0u);

    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = depth_enabled_ ? 2u : 1u;
    ci.pAttachments    = attachments;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;
    vkCreateRenderPass(vk.device, &ci, nullptr, &render_pass_);
}

void SimpleRenderer::create_depth_resources(VulkanContext& vk) {
    VkImageCreateInfo img_ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_ci.imageType     = VK_IMAGE_TYPE_2D;
    img_ci.format        = DEPTH_FORMAT;
    img_ci.extent.width  = vk.swapchain_extent.width;
    img_ci.extent.height = vk.swapchain_extent.height;
    img_ci.extent.depth  = 1;
    img_ci.mipLevels     = 1;
    img_ci.arrayLayers   = 1;
    img_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(vk.device, &img_ci, nullptr, &depth_image_);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(vk.device, depth_image_, &mem_req);

    VkMemoryAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_ci.allocationSize  = mem_req.size;
    alloc_ci.memoryTypeIndex = vk.find_memory_type(mem_req.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(vk.device, &alloc_ci, nullptr, &depth_memory_);
    vkBindImageMemory(vk.device, depth_image_, depth_memory_, 0);

    VkImageViewCreateInfo view_ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_ci.image                           = depth_image_;
    view_ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format                          = DEPTH_FORMAT;
    view_ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_ci.subresourceRange.baseMipLevel   = 0;
    view_ci.subresourceRange.levelCount     = 1;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount     = 1;
    vkCreateImageView(vk.device, &view_ci, nullptr, &depth_view_);
}

void SimpleRenderer::destroy_depth_resources(VulkanContext& vk) {
    if (depth_view_)   { vkDestroyImageView(vk.device, depth_view_, nullptr); depth_view_ = VK_NULL_HANDLE; }
    if (depth_image_)  { vkDestroyImage(vk.device, depth_image_, nullptr); depth_image_ = VK_NULL_HANDLE; }
    if (depth_memory_) { vkFreeMemory(vk.device, depth_memory_, nullptr); depth_memory_ = VK_NULL_HANDLE; }
}

void SimpleRenderer::create_framebuffers(VulkanContext& vk) {
    framebuffers_.resize(vk.swapchain_views.size());
    for (size_t i = 0; i < vk.swapchain_views.size(); i++) {
        VkImageView views[2] = { vk.swapchain_views[i], depth_view_ };
        VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass      = render_pass_;
        ci.attachmentCount = depth_enabled_ ? 2u : 1u;
        ci.pAttachments    = views;
        ci.width           = vk.swapchain_extent.width;
        ci.height          = vk.swapchain_extent.height;
        ci.layers          = 1;
        vkCreateFramebuffer(vk.device, &ci, nullptr, &framebuffers_[i]);
    }
}

void SimpleRenderer::destroy_framebuffers(VulkanContext& vk) {
    for (auto fb : framebuffers_)
        vkDestroyFramebuffer(vk.device, fb, nullptr);
    framebuffers_.clear();
}

void SimpleRenderer::rebuild_swapchain(VulkanContext& vk, GLFWwindow* window) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vk.device);
    destroy_framebuffers(vk);
    if (depth_enabled_) destroy_depth_resources(vk);
    vk.recreate_swapchain(window);
    if (depth_enabled_) create_depth_resources(vk);
    create_framebuffers(vk);
}
