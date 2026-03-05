#pragma once

#include "common/vulkan_context.h"
#include "cosmos/cosmos_types.h"
#include <glm/glm.hpp>
#include <vector>

struct alignas(16) CosmosBhGpuNode {
    glm::vec4 center_half{0.0f}; // xyz=center, w=half
    glm::vec4 com_mass{0.0f};    // xyz=center of mass, w=mass
    glm::vec4 spin_leaf{0.0f};   // x=spin_y, y=leaf_count, z=is_leaf
    glm::ivec4 child0{-1};       // child[0..3]
    glm::ivec4 child1{-1};       // child[4..7]
    glm::ivec4 leaf{-1};         // leaf source indices[0..3]
};

class CosmosGravityCompute {
public:
    void init(VulkanContext& vk);
    void destroy(VulkanContext& vk);
    bool is_ready() const { return pipeline_ != VK_NULL_HANDLE; }

    // source_data layout: x=mass, y=spin_y, z=active(0/1), w=marked_for_removal(0/1)
    bool compute_barnes_hut(VulkanContext& vk,
                            const std::vector<glm::vec3>& pos,
                            const std::vector<glm::vec3>& vel,
                            const std::vector<glm::vec4>& source_data,
                            const std::vector<CosmosBhGpuNode>& nodes,
                            float theta,
                            const CosmosConfig& cfg,
                            std::vector<glm::vec3>& out_accel);

private:
    struct alignas(16) PushConstants {
        uint32_t body_count = 0;
        uint32_t node_count = 0;
        float theta = 0.72f;
        float G = 1.0f;
        float soft2 = 25.0f;
        float c2 = 90000.0f;
        float gr_precession_scale = 1.0f;
        float gr_time_dilation = 1.0f;
        float gr_frame_dragging = 1.0f;
        uint32_t gr_enabled = 1u;
        float pad0 = 0.0f;
        float pad1 = 0.0f;
    };

    void ensure_capacity(VulkanContext& vk, size_t body_count, size_t node_count);
    void write_descriptor_set(VulkanContext& vk);
    static size_t next_capacity(size_t current, size_t required);

    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_ = VK_NULL_HANDLE;

    Buffer pos_buffer_{};
    Buffer vel_buffer_{};
    Buffer source_buffer_{};
    Buffer node_buffer_{};
    Buffer out_accel_buffer_{};

    size_t body_capacity_ = 0;
    size_t node_capacity_ = 0;

    std::vector<glm::vec4> pos_scratch_;
    std::vector<glm::vec4> vel_scratch_;
    std::vector<glm::vec4> out_scratch_;
};

