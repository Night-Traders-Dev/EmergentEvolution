#pragma once

#include "common/vulkan_context.h"
#include <glm/glm.hpp>
#include <vector>

struct alignas(16) NebulaComputeInput {
    glm::vec4 seed_class_temp{0.0f}; // x=seed, y=render_class, z=subtype, w=temperature
    glm::vec4 atmosphere{0.0f};      // x=cloud_cov, y=pressure, z=haze_density, w=rayleigh
    glm::vec4 activity{0.0f};        // x=flow, y=cloud_detail, z=collapse, w=mie
    glm::vec4 composition{0.0f};     // x=rock, y=ice, z=metal, w=dust
};

struct alignas(16) NebulaComputeOutput {
    glm::vec4 flow{0.0f};    // xyz=advection flow, w=turbulence
    glm::vec4 optical{0.0f}; // x=density scale, y=absorption, z=scatter anisotropy, w=emission gain
};

class CosmosNebulaCompute {
public:
    void init(VulkanContext& vk);
    void destroy(VulkanContext& vk);
    bool is_ready() const { return pipeline_ != VK_NULL_HANDLE; }

    bool compute_to_buffer(VulkanContext& vk,
                           const std::vector<NebulaComputeInput>& in,
                           float sim_time,
                           const Buffer& dst_buffer,
                           size_t dst_capacity_elems);

private:
    struct alignas(16) PushConstants {
        uint32_t body_count = 0;
        float sim_time = 0.0f;
        float dt_hint = 0.0f;
        float pad = 0.0f;
    };

    void ensure_capacity(VulkanContext& vk, size_t required);
    void write_descriptor_set(VulkanContext& vk);
    static size_t next_capacity(size_t current, size_t required);

    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_ = VK_NULL_HANDLE;

    Buffer input_buffer_{};
    Buffer output_buffer_{};
    size_t capacity_ = 0;
};
