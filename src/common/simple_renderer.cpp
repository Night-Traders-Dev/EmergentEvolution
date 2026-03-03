#include "common/simple_renderer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <algorithm>

// ── Lifecycle ────────────────────────────────────────────────────────────────

void SimpleRenderer::init(VulkanContext& vk, GLFWwindow* window) {
    create_render_pass(vk);
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

    VkClearValue clear{};
    clear.color = {{0.06f, 0.06f, 0.10f, 1.0f}};

    VkRenderPassBeginInfo rp_ci{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_ci.renderPass        = render_pass_;
    rp_ci.framebuffer       = framebuffers_[image_idx_];
    rp_ci.renderArea.extent = vk.swapchain_extent;
    rp_ci.clearValueCount   = 1;
    rp_ci.pClearValues      = &clear;
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
    VkAttachmentDescription color{};
    color.format         = vk.swapchain_format;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments    = &color;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;
    vkCreateRenderPass(vk.device, &ci, nullptr, &render_pass_);
}

void SimpleRenderer::create_framebuffers(VulkanContext& vk) {
    framebuffers_.resize(vk.swapchain_views.size());
    for (size_t i = 0; i < vk.swapchain_views.size(); i++) {
        VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass      = render_pass_;
        ci.attachmentCount = 1;
        ci.pAttachments    = &vk.swapchain_views[i];
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
    vk.recreate_swapchain(window);
    create_framebuffers(vk);
}
