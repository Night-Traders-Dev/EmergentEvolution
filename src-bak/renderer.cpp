#include "renderer.h"
#include "interface.h"
#include <stdexcept>
#include <array>
#include <string>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define VK_CHECK(call) \
    do { VkResult _r = (call); \
        if (_r != VK_SUCCESS) \
            throw std::runtime_error("Vulkan error: " #call); \
    } while (0)

// ── Init / Destroy ────────────────────────────────────────────────────────────

void Renderer::init(VulkanContext& ctx, GLFWwindow* window, ComputePipeline& compute) {
    create_render_pass(ctx);
    create_framebuffers(ctx);

    // Quad pipeline expects SPIRVs in executable's directory
    create_quad_pipeline(ctx, "shaders/fullscreen.vert.spv",
                               "shaders/fullscreen.frag.spv");
    create_quad_descriptor_set(ctx, compute);
    create_sync_objects(ctx);
    init_imgui(ctx, window);
}

void Renderer::destroy(VulkanContext& ctx) {
    vkDeviceWaitIdle(ctx.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imgui_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx.device, imgui_pool_, nullptr);

    for (auto& f : frames_) {
//        vkDestroyCommandBuffer(ctx.device, f.cmd, nullptr); // handled by pool cleanup
        vkDestroySemaphore(ctx.device, f.image_available, nullptr);
        vkDestroySemaphore(ctx.device, f.render_finished, nullptr);
        vkDestroyFence(ctx.device, f.in_flight, nullptr);
    }

    if (quad_desc_pool_   != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, quad_desc_pool_, nullptr);
    if (quad_pipeline_    != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, quad_pipeline_, nullptr);
    if (quad_pipe_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, quad_pipe_layout_, nullptr);
    if (quad_dset_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, quad_dset_layout_, nullptr);

    destroy_framebuffers(ctx);
    if (render_pass_ != VK_NULL_HANDLE) vkDestroyRenderPass(ctx.device, render_pass_, nullptr);
}

// ── Render pass ───────────────────────────────────────────────────────────────

void Renderer::create_render_pass(VulkanContext& ctx) {
    VkAttachmentDescription color_attach{};
    color_attach.format         = ctx.swapchain_format;
    color_attach.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attach.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &color_attach;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(ctx.device, &ci, nullptr, &render_pass_));
}

// ── Framebuffers ──────────────────────────────────────────────────────────────

void Renderer::create_framebuffers(VulkanContext& ctx) {
    framebuffers_.resize(ctx.swapchain_views.size());
    for (size_t i = 0; i < ctx.swapchain_views.size(); ++i) {
        VkImageView attachments[] = { ctx.swapchain_views[i] };
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = render_pass_;
        ci.attachmentCount = 1;
        ci.pAttachments    = attachments;
        ci.width           = ctx.swapchain_extent.width;
        ci.height          = ctx.swapchain_extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(ctx.device, &ci, nullptr, &framebuffers_[i]));
    }
}

void Renderer::destroy_framebuffers(VulkanContext& ctx) {
    for (auto fb : framebuffers_)
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
    framebuffers_.clear();
}

void Renderer::rebuild_framebuffers(VulkanContext& ctx) {
    destroy_framebuffers(ctx);
    create_framebuffers(ctx);
}

// ── Fullscreen-quad pipeline ──────────────────────────────────────────────────

void Renderer::create_quad_pipeline(VulkanContext& ctx,
                                    const std::string& vert_spv,
                                    const std::string& frag_spv)
{
    // Descriptor set layout: binding 0 = combined image sampler
    VkDescriptorSetLayoutBinding binding{};
    binding.binding            = 0;
    binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount    = 1;
    binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci{};
    dsl_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 1;
    dsl_ci.pBindings    = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &dsl_ci, nullptr, &quad_dset_layout_));

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts    = &quad_dset_layout_;
    VK_CHECK(vkCreatePipelineLayout(ctx.device, &pl_ci, nullptr, &quad_pipe_layout_));

    // Shaders
    VkShaderModule vert = ctx.create_shader_module(vert_spv);
    VkShaderModule frag = ctx.create_shader_module(frag_spv);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // No vertex input (fullscreen triangle generated in vertex shader)
    VkPipelineVertexInputStateCreateInfo vert_input{};
    vert_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_asm{};
    input_asm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width    = static_cast<float>(ctx.swapchain_extent.width);
    viewport.height   = static_cast<float>(ctx.swapchain_extent.height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = ctx.swapchain_extent;

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.pViewports    = &viewport;
    vp_state.scissorCount  = 1;
    vp_state.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode    = VK_CULL_MODE_NONE;
    rast.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rast.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attach{};
    blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_attach;

    // Dynamic state for viewport + scissor (so resize works without pipeline rebuild)
    std::array<VkDynamicState, 2> dyn_states = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_state{};
    dyn_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_state.dynamicStateCount = static_cast<uint32_t>(dyn_states.size());
    dyn_state.pDynamicStates    = dyn_states.data();

    VkGraphicsPipelineCreateInfo pipe_ci{};
    pipe_ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipe_ci.stageCount          = 2;
    pipe_ci.pStages             = stages;
    pipe_ci.pVertexInputState   = &vert_input;
    pipe_ci.pInputAssemblyState = &input_asm;
    pipe_ci.pViewportState      = &vp_state;
    pipe_ci.pRasterizationState = &rast;
    pipe_ci.pMultisampleState   = &ms;
    pipe_ci.pColorBlendState    = &blend;
    pipe_ci.pDynamicState       = &dyn_state;
    pipe_ci.layout              = quad_pipe_layout_;
    pipe_ci.renderPass          = render_pass_;
    pipe_ci.subpass             = 0;

    VK_CHECK(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1,
                                       &pipe_ci, nullptr, &quad_pipeline_));

    vkDestroyShaderModule(ctx.device, vert, nullptr);
    vkDestroyShaderModule(ctx.device, frag, nullptr);
}

void Renderer::create_quad_descriptor_set(VulkanContext& ctx, ComputePipeline& compute) {
    // Pool for one combined image sampler
    VkDescriptorPoolSize pool_size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets       = 1;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;
    VK_CHECK(vkCreateDescriptorPool(ctx.device, &pool_ci, nullptr, &quad_desc_pool_));

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = quad_desc_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &quad_dset_layout_;
    VK_CHECK(vkAllocateDescriptorSets(ctx.device, &alloc, &quad_desc_set_));

    VkDescriptorImageInfo img_info{};
    img_info.sampler     = compute.sampler;
    img_info.imageView   = compute.particle_texture.view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = quad_desc_set_;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

// ── Sync objects ──────────────────────────────────────────────────────────────

void Renderer::create_sync_objects(VulkanContext& ctx) {
    // Allocate command buffers
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = ctx.cmd_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = FRAMES_IN_FLIGHT;
    VkCommandBuffer cmds[FRAMES_IN_FLIGHT];
    VK_CHECK(vkAllocateCommandBuffers(ctx.device, &alloc, cmds));

    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fen_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fen_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        frames_[i].cmd = cmds[i];
        VK_CHECK(vkCreateSemaphore(ctx.device, &sem_ci, nullptr, &frames_[i].image_available));
        VK_CHECK(vkCreateSemaphore(ctx.device, &sem_ci, nullptr, &frames_[i].render_finished));
        VK_CHECK(vkCreateFence(ctx.device, &fen_ci, nullptr, &frames_[i].in_flight));
    }
}

// ── ImGui ─────────────────────────────────────────────────────────────────────

void Renderer::init_imgui(VulkanContext& ctx, GLFWwindow* window) {
    // Descriptor pool for ImGui (generous size)
    std::array<VkDescriptorPoolSize, 11> pool_sizes = {{
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
    }};
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets       = 1000;
    pool_ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_ci.pPoolSizes    = pool_sizes.data();
    VK_CHECK(vkCreateDescriptorPool(ctx.device, &pool_ci, nullptr, &imgui_pool_));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark theme matching the original Godot look
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding  = 6.0f;
    style.FrameRounding   = 4.0f;
    style.GrabRounding    = 4.0f;
    style.WindowPadding   = { 12.0f, 12.0f };
    style.FramePadding    = { 6.0f, 4.0f };
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.78f);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance        = ctx.instance;
    info.PhysicalDevice  = ctx.physical_device;
    info.Device          = ctx.device;
    info.QueueFamily     = ctx.queue_family;
    info.Queue           = ctx.queue;
    info.DescriptorPool  = imgui_pool_;
    info.RenderPass      = render_pass_;
    info.MinImageCount   = static_cast<uint32_t>(ctx.swapchain_images.size());
    info.ImageCount      = static_cast<uint32_t>(ctx.swapchain_images.size());
    info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&info);

    // Upload fonts via a one-time command buffer
    VkCommandBuffer cmd = ctx.begin_single_command();
    ImGui_ImplVulkan_CreateFontsTexture();
    ctx.end_single_command(cmd);
}

// ── Main draw frame ───────────────────────────────────────────────────────────

bool Renderer::draw_frame(VulkanContext& ctx,
                          GLFWwindow*   window,
                          ComputePipeline& compute,
                          bool sim_active)
{
    FrameData& frame = frames_[current_frame_];

    vkWaitForFences(ctx.device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(ctx.device, ctx.swapchain, UINT64_MAX,
                                            frame.image_available, VK_NULL_HANDLE,
                                            &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) { swapchain_dirty = true; return false; }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    vkResetFences(ctx.device, 1, &frame.in_flight);

    // Record
    VkCommandBuffer cmd = frame.cmd;
    vkResetCommandBuffer(cmd, 0);
    record_command_buffer(cmd, image_index, ctx, compute, sim_active);

    // Submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &frame.image_available;
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &frame.render_finished;
    VK_CHECK(vkQueueSubmit(ctx.queue, 1, &submit, frame.in_flight));

    // Present
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &frame.render_finished;
    present.swapchainCount     = 1;
    present.pSwapchains        = &ctx.swapchain;
    present.pImageIndices      = &image_index;
    result = vkQueuePresentKHR(ctx.queue, &present);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        swapchain_dirty = true;
    else if (result != VK_SUCCESS)
        throw std::runtime_error("vkQueuePresentKHR failed");

    current_frame_ = (current_frame_ + 1) % FRAMES_IN_FLIGHT;
    return true;
}

void Renderer::on_resize(VulkanContext& ctx, GLFWwindow* window, ComputePipeline& compute) {
    vkDeviceWaitIdle(ctx.device);
    ctx.recreate_swapchain(window);
    rebuild_framebuffers(ctx);
    swapchain_dirty = false;
}

// ── Command buffer recording ──────────────────────────────────────────────────

void Renderer::record_command_buffer(VkCommandBuffer cmd,
                                     uint32_t        image_index,
                                     VulkanContext&  ctx,
                                     ComputePipeline& compute,
                                     bool            sim_active)
{
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    // NOTE: Compute dispatches are submitted separately in Simulation::tick()
    // via a one-time command buffer (vk.begin_single_command / end_single_command)
    // before this function is called. The pipeline barrier at the end of
    // ComputePipeline::record() ensures memory ordering (compute write →
    // fragment read), and vkQueueWaitIdle in end_single_command provides
    // full GPU synchronisation before we begin the render pass.

    // --- Render pass (fullscreen quad + ImGui) ---
    VkClearValue clear_val{};
    clear_val.color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = render_pass_;
    rp_begin.framebuffer       = framebuffers_[image_index];
    rp_begin.renderArea.extent = ctx.swapchain_extent;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_val;
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamic viewport / scissor
    VkViewport viewport{};
    viewport.width    = static_cast<float>(ctx.swapchain_extent.width);
    viewport.height   = static_cast<float>(ctx.swapchain_extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = ctx.swapchain_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Fullscreen quad
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, quad_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            quad_pipe_layout_, 0, 1, &quad_desc_set_, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0); // 3 vertices → fullscreen triangle

    // ImGui
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}
