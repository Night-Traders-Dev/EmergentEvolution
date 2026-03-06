#include "cosmos/cosmos_gravity_compute.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

template <typename T>
inline void reset_handle(T& h) {
    h = VK_NULL_HANDLE;
}

} // namespace

size_t CosmosGravityCompute::next_capacity(size_t current, size_t required) {
    size_t cap = std::max<size_t>(current, 256);
    while (cap < required)
        cap *= 2;
    return cap;
}

void CosmosGravityCompute::init(VulkanContext& vk) {
    if (pipeline_ != VK_NULL_HANDLE)
        return;

    VkDescriptorSetLayoutBinding bindings[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dsl_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl_ci.bindingCount = 5;
    dsl_ci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(vk.device, &dsl_ci, nullptr, &desc_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cosmos gravity compute descriptor set layout");

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
        throw std::runtime_error("Failed to create cosmos gravity compute pipeline layout");

    VkShaderModule cs_mod = vk.create_shader_module("shaders/cosmos_bh.spv");

    VkPipelineShaderStageCreateInfo stage_ci{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = cs_mod;
    stage_ci.pName = "main";

    VkComputePipelineCreateInfo cp_ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp_ci.stage = stage_ci;
    cp_ci.layout = pipeline_layout_;
    if (vkCreateComputePipelines(vk.device, vk.pipeline_cache, 1, &cp_ci, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(vk.device, cs_mod, nullptr);
        throw std::runtime_error("Failed to create cosmos gravity compute pipeline");
    }
    vkDestroyShaderModule(vk.device, cs_mod, nullptr);

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 5;

    VkDescriptorPoolCreateInfo dp_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp_ci.maxSets = 1;
    dp_ci.poolSizeCount = 1;
    dp_ci.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(vk.device, &dp_ci, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cosmos gravity descriptor pool");

    VkDescriptorSetAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_ci.descriptorPool = desc_pool_;
    alloc_ci.descriptorSetCount = 1;
    alloc_ci.pSetLayouts = &desc_set_layout_;
    if (vkAllocateDescriptorSets(vk.device, &alloc_ci, &desc_set_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate cosmos gravity descriptor set");
}

void CosmosGravityCompute::destroy(VulkanContext& vk) {
    vk.destroy_buffer(pos_buffer_);
    vk.destroy_buffer(vel_buffer_);
    vk.destroy_buffer(source_buffer_);
    vk.destroy_buffer(node_buffer_);
    vk.destroy_buffer(out_accel_buffer_);
    body_capacity_ = 0;
    node_capacity_ = 0;
    pos_scratch_.clear();
    vel_scratch_.clear();
    out_scratch_.clear();

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

void CosmosGravityCompute::write_descriptor_set(VulkanContext& vk) {
    VkDescriptorBufferInfo infos[5]{};
    infos[0] = {pos_buffer_.handle, 0, pos_buffer_.size};
    infos[1] = {vel_buffer_.handle, 0, vel_buffer_.size};
    infos[2] = {source_buffer_.handle, 0, source_buffer_.size};
    infos[3] = {node_buffer_.handle, 0, node_buffer_.size};
    infos[4] = {out_accel_buffer_.handle, 0, out_accel_buffer_.size};

    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = desc_set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(vk.device, 5, writes, 0, nullptr);
}

void CosmosGravityCompute::ensure_capacity(VulkanContext& vk, size_t body_count, size_t node_count) {
    bool rebuild = false;

    if (body_count > body_capacity_) {
        body_capacity_ = next_capacity(body_capacity_, body_count);
        rebuild = true;
    }
    if (node_count > node_capacity_) {
        node_capacity_ = next_capacity(node_capacity_, node_count);
        rebuild = true;
    }
    if (!rebuild)
        return;

    vk.destroy_buffer(pos_buffer_);
    vk.destroy_buffer(vel_buffer_);
    vk.destroy_buffer(source_buffer_);
    vk.destroy_buffer(node_buffer_);
    vk.destroy_buffer(out_accel_buffer_);

    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags host_mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pos_buffer_ = vk.create_buffer(body_capacity_ * sizeof(glm::vec4), usage, host_mem);
    vel_buffer_ = vk.create_buffer(body_capacity_ * sizeof(glm::vec4), usage, host_mem);
    source_buffer_ = vk.create_buffer(body_capacity_ * sizeof(glm::vec4), usage, host_mem);
    node_buffer_ = vk.create_buffer(node_capacity_ * sizeof(CosmosBhGpuNode), usage, host_mem);
    out_accel_buffer_ = vk.create_buffer(body_capacity_ * sizeof(glm::vec4), usage, host_mem);

    pos_scratch_.resize(body_capacity_);
    vel_scratch_.resize(body_capacity_);
    out_scratch_.resize(body_capacity_);

    write_descriptor_set(vk);
}

bool CosmosGravityCompute::compute_barnes_hut(VulkanContext& vk,
                                              const std::vector<glm::vec3>& pos,
                                              const std::vector<glm::vec3>& vel,
                                              const std::vector<glm::vec4>& source_data,
                                              const std::vector<CosmosBhGpuNode>& nodes,
                                              float theta,
                                              const CosmosConfig& cfg,
                                              std::vector<glm::vec3>& out_accel) {
    if (!is_ready())
        return false;
    const size_t body_count = pos.size();
    if (body_count == 0 || vel.size() != body_count || source_data.size() != body_count || nodes.empty()) {
        out_accel.assign(body_count, glm::vec3(0.0f));
        return false;
    }

    ensure_capacity(vk, body_count, nodes.size());

    for (size_t i = 0; i < body_count; ++i) {
        pos_scratch_[i] = glm::vec4(pos[i], 0.0f);
        vel_scratch_[i] = glm::vec4(vel[i], 0.0f);
    }

    vk.update_buffer(pos_buffer_, pos_scratch_.data(), body_count * sizeof(glm::vec4));
    vk.update_buffer(vel_buffer_, vel_scratch_.data(), body_count * sizeof(glm::vec4));
    vk.update_buffer(source_buffer_, source_data.data(), body_count * sizeof(glm::vec4));
    vk.update_buffer(node_buffer_, nodes.data(), nodes.size() * sizeof(CosmosBhGpuNode));

    VkCommandBuffer cmd = vk.begin_single_command();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                            0, 1, &desc_set_, 0, nullptr);

    PushConstants pc{};
    pc.body_count = static_cast<uint32_t>(body_count);
    pc.node_count = static_cast<uint32_t>(nodes.size());
    pc.theta = theta;
    pc.G = cfg.G;
    pc.soft2 = cfg.softening * cfg.softening;
    pc.c2 = cfg.speed_of_light * cfg.speed_of_light;
    pc.gr_precession_scale = cfg.gr_precession_scale;
    pc.gr_time_dilation = cfg.gr_time_dilation;
    pc.gr_frame_dragging = cfg.gr_frame_dragging;
    pc.gr_enabled = cfg.gr_enabled ? 1u : 0u;

    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    const uint32_t groups = static_cast<uint32_t>((body_count + 127u) / 128u);
    vkCmdDispatch(cmd, std::max(groups, 1u), 1, 1);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vk.end_single_command(cmd);

    void* mapped = nullptr;
    if (vkMapMemory(vk.device, out_accel_buffer_.memory, 0,
                    body_count * sizeof(glm::vec4), 0, &mapped) != VK_SUCCESS) {
        out_accel.assign(body_count, glm::vec3(0.0f));
        return false;
    }
    std::memcpy(out_scratch_.data(), mapped, body_count * sizeof(glm::vec4));
    vkUnmapMemory(vk.device, out_accel_buffer_.memory);

    out_accel.resize(body_count);
    for (size_t i = 0; i < body_count; ++i)
        out_accel[i] = glm::vec3(out_scratch_[i]);
    return true;
}

