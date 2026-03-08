#include "cosmos/cosmos_nebula_compute.h"

#include <algorithm>
#include <stdexcept>

namespace {

template <typename T>
inline void reset_handle(T& h) {
    h = VK_NULL_HANDLE;
}

} // namespace

size_t CosmosNebulaCompute::next_capacity(size_t current, size_t required) {
    size_t cap = std::max<size_t>(current, 256);
    while (cap < required)
        cap *= 2;
    return cap;
}

void CosmosNebulaCompute::init(VulkanContext& vk) {
    if (pipeline_ != VK_NULL_HANDLE)
        return;

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl_ci.bindingCount = 3;
    dsl_ci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(vk.device, &dsl_ci, nullptr, &desc_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create nebula volume compute descriptor set layout");

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pl_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &desc_set_layout_;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(vk.device, &pl_ci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create nebula volume compute pipeline layout");

    VkShaderModule cs_mod = vk.create_shader_module("shaders/cosmos/cosmos_nebula.spv");

    VkPipelineShaderStageCreateInfo stage_ci{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = cs_mod;
    stage_ci.pName = "main";

    VkComputePipelineCreateInfo cp_ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp_ci.stage = stage_ci;
    cp_ci.layout = pipeline_layout_;
    if (vkCreateComputePipelines(vk.device, vk.pipeline_cache, 1, &cp_ci, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(vk.device, cs_mod, nullptr);
        throw std::runtime_error("Failed to create nebula volume compute pipeline");
    }
    vkDestroyShaderModule(vk.device, cs_mod, nullptr);

    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[1].descriptorCount = 2;

    VkDescriptorPoolCreateInfo dp_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp_ci.maxSets = 1;
    dp_ci.poolSizeCount = 2;
    dp_ci.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(vk.device, &dp_ci, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create nebula volume compute descriptor pool");

    VkDescriptorSetAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_ci.descriptorPool = desc_pool_;
    alloc_ci.descriptorSetCount = 1;
    alloc_ci.pSetLayouts = &desc_set_layout_;
    if (vkAllocateDescriptorSets(vk.device, &alloc_ci, &desc_set_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate nebula volume compute descriptor set");
}

void CosmosNebulaCompute::destroy(VulkanContext& vk) {
    vk.destroy_buffer(input_buffer_);
    capacity_ = 0;

    if (desc_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(vk.device, desc_pool_, nullptr);
    if (pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(vk.device, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(vk.device, pipeline_layout_, nullptr);
    if (desc_set_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(vk.device, desc_set_layout_, nullptr);

    reset_handle(desc_pool_);
    reset_handle(pipeline_);
    reset_handle(pipeline_layout_);
    reset_handle(desc_set_layout_);
    reset_handle(desc_set_);
}

void CosmosNebulaCompute::write_descriptor_set(VulkanContext& vk,
                                               VkImageView src_volume_view,
                                               VkImageView dst_volume_view) {
    VkDescriptorBufferInfo input_info{};
    input_info.buffer = input_buffer_.handle;
    input_info.offset = 0;
    input_info.range = input_buffer_.size;

    VkDescriptorImageInfo src_img{};
    src_img.imageView = src_volume_view;
    src_img.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo dst_img{};
    dst_img.imageView = dst_volume_view;
    dst_img.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &input_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = desc_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &src_img;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = desc_set_;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &dst_img;

    vkUpdateDescriptorSets(vk.device, 3, writes, 0, nullptr);
}

void CosmosNebulaCompute::ensure_capacity(VulkanContext& vk, size_t required) {
    if (required <= capacity_)
        return;

    capacity_ = next_capacity(capacity_, required);

    vk.destroy_buffer(input_buffer_);

    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags host_mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    input_buffer_ = vk.create_buffer(capacity_ * sizeof(NebulaComputeInput), usage, host_mem);
}

bool CosmosNebulaCompute::compute_volume(VulkanContext& vk,
                                         const std::vector<NebulaComputeInput>& in,
                                         float sim_time,
                                         float dt_hint,
                                         VkImageView src_volume_view,
                                         VkImageView dst_volume_view,
                                         uint32_t volume_dim) {
    if (!is_ready())
        return false;
    if (src_volume_view == VK_NULL_HANDLE || dst_volume_view == VK_NULL_HANDLE || volume_dim == 0)
        return false;

    const size_t body_count = in.size();
    if (body_count == 0)
        return true;

    ensure_capacity(vk, body_count);
    vk.update_buffer(input_buffer_, in.data(), body_count * sizeof(NebulaComputeInput));
    write_descriptor_set(vk, src_volume_view, dst_volume_view);

    VkCommandBuffer cmd = vk.begin_single_command();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                            0, 1, &desc_set_, 0, nullptr);

    PushConstants pc{};
    pc.body_count = static_cast<uint32_t>(body_count);
    pc.sim_time = sim_time;
    pc.dt_hint = dt_hint;
    pc.volume_dim = volume_dim;

    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    const uint32_t groups = std::max(1u, (volume_dim + 3u) / 4u);
    vkCmdDispatch(cmd, groups, groups, groups);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vk.end_single_command(cmd);
    return true;
}
