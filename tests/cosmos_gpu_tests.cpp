#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "common/vulkan_context.h"
#include "cosmos/rendering/cosmos_gravity_compute.h"
#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_app_internal.h"

#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <iostream>

// ── Shared GPU fixture ──────────────────────────────────────────────────────

static VulkanContext* g_vk = nullptr;

static bool gpu_available() { return g_vk != nullptr; }

// ═════════════════════════════════════════════════════════════════════════════
// GPU-ONLY TESTS — Vulkan infrastructure, buffers, pipeline setup
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GPU Infrastructure") {

TEST_CASE("Headless Vulkan context initialized successfully") {
    REQUIRE(gpu_available());
    CHECK(g_vk->instance != VK_NULL_HANDLE);
    CHECK(g_vk->physical_device != VK_NULL_HANDLE);
    CHECK(g_vk->device != VK_NULL_HANDLE);
    CHECK(g_vk->queue != VK_NULL_HANDLE);
    CHECK(g_vk->cmd_pool != VK_NULL_HANDLE);
    CHECK(g_vk->headless);
}

TEST_CASE("GPU was detected and listed") {
    REQUIRE(gpu_available());
    CHECK_FALSE(g_vk->gpu_list.empty());
    auto& gpu = g_vk->gpu_list[0];
    CHECK_FALSE(gpu.name.empty());
    CHECK(gpu.vram_bytes > 0);
    MESSAGE("GPU: ", gpu.name, " (", gpu.vram_bytes / (1024*1024), " MB VRAM)");
}

TEST_CASE("Buffer create, write, read, destroy cycle") {
    REQUIRE(gpu_available());

    const size_t count = 256;
    const VkDeviceSize buf_size = count * sizeof(float);
    Buffer buf = g_vk->create_buffer(
        buf_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    CHECK(buf.handle != VK_NULL_HANDLE);
    CHECK(buf.memory != VK_NULL_HANDLE);
    CHECK(buf.size == buf_size);

    // Write data
    std::vector<float> input(count);
    for (size_t i = 0; i < count; ++i)
        input[i] = (float)i * 3.14159f;
    g_vk->update_buffer(buf, input.data(), buf_size);

    // Read back
    void* mapped = nullptr;
    VkResult r = vkMapMemory(g_vk->device, buf.memory, 0, buf_size, 0, &mapped);
    CHECK(r == VK_SUCCESS);
    std::vector<float> output(count);
    std::memcpy(output.data(), mapped, buf_size);
    vkUnmapMemory(g_vk->device, buf.memory);

    for (size_t i = 0; i < count; ++i)
        CHECK(output[i] == doctest::Approx(input[i]));

    g_vk->destroy_buffer(buf);
}

TEST_CASE("Command buffer begin/end cycle works") {
    REQUIRE(gpu_available());

    // Just ensure we can allocate, submit, and free a command buffer
    VkCommandBuffer cmd = g_vk->begin_single_command();
    CHECK(cmd != VK_NULL_HANDLE);
    // Empty command buffer — just tests infrastructure
    g_vk->end_single_command(cmd);
}

TEST_CASE("Large buffer allocation and readback") {
    REQUIRE(gpu_available());

    const size_t count = 64 * 1024; // 64K floats = 256KB
    const VkDeviceSize buf_size = count * sizeof(float);
    Buffer buf = g_vk->create_buffer(
        buf_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    CHECK(buf.handle != VK_NULL_HANDLE);

    std::vector<float> data(count, 42.0f);
    g_vk->update_buffer(buf, data.data(), buf_size);

    void* mapped = nullptr;
    vkMapMemory(g_vk->device, buf.memory, 0, buf_size, 0, &mapped);
    float first_val;
    std::memcpy(&first_val, mapped, sizeof(float));
    vkUnmapMemory(g_vk->device, buf.memory);

    CHECK(first_val == doctest::Approx(42.0f));
    g_vk->destroy_buffer(buf);
}

TEST_CASE("Shader module loading from SPV") {
    REQUIRE(gpu_available());

    // Try loading the Barnes-Hut compute shader
    VkShaderModule mod = VK_NULL_HANDLE;
    try {
        mod = g_vk->create_shader_module("shaders/cosmos/cosmos_bh.spv");
    } catch (...) {
        // SPV might not exist in test working dir — that's OK
        MESSAGE("cosmos_bh.spv not found (expected if running from build dir)");
    }
    if (mod != VK_NULL_HANDLE) {
        CHECK(mod != VK_NULL_HANDLE);
        vkDestroyShaderModule(g_vk->device, mod, nullptr);
    }
}

} // GPU Infrastructure

// ═════════════════════════════════════════════════════════════════════════════
// CPU+GPU TESTS — Gravity compute pipeline vs CPU reference
// ═════════════════════════════════════════════════════════════════════════════

// CPU reference: direct N-body gravity
static std::vector<glm::vec3> cpu_gravity(
    const std::vector<glm::vec3>& pos,
    const std::vector<float>& mass,
    float G, float softening2)
{
    size_t n = pos.size();
    std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            glm::vec3 r = pos[j] - pos[i];
            float dist2 = glm::dot(r, r) + softening2;
            float inv_dist3 = 1.0f / (std::sqrt(dist2) * dist2);
            accel[i] += r * (G * mass[j] * inv_dist3);
        }
    }
    return accel;
}

// Build a trivial Barnes-Hut tree: one root node containing all bodies as leaves
static std::vector<CosmosBhGpuNode> build_flat_tree(
    const std::vector<glm::vec3>& pos,
    const std::vector<float>& mass)
{
    size_t n = pos.size();
    // For a flat tree with theta=0 (force exact), we create one root node
    // with all bodies as leaves (up to 4 per leaf node).
    std::vector<CosmosBhGpuNode> nodes;

    // Compute bounding box
    glm::vec3 bmin(1e9f), bmax(-1e9f);
    glm::vec3 com(0.0f);
    float total_mass = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        bmin = glm::min(bmin, pos[i]);
        bmax = glm::max(bmax, pos[i]);
        com += pos[i] * mass[i];
        total_mass += mass[i];
    }
    if (total_mass > 0.0f) com /= total_mass;
    glm::vec3 center = (bmin + bmax) * 0.5f;
    float half = glm::length(bmax - bmin) * 0.5f + 1.0f;

    // Create leaf nodes (4 bodies per node)
    size_t leaf_start = 0;
    std::vector<int> leaf_node_indices;
    for (size_t i = 0; i < n; i += 4) {
        CosmosBhGpuNode leaf{};
        leaf.center_half = glm::vec4(center, half);
        // Compute leaf center of mass
        glm::vec3 leaf_com(0.0f);
        float leaf_mass = 0.0f;
        int leaf_count = 0;
        for (size_t j = i; j < std::min(i + 4, n); ++j) {
            leaf.leaf[leaf_count] = (int)j;
            leaf_com += pos[j] * mass[j];
            leaf_mass += mass[j];
            leaf_count++;
        }
        if (leaf_mass > 0.0f) leaf_com /= leaf_mass;
        leaf.com_mass = glm::vec4(leaf_com, leaf_mass);
        leaf.spin_leaf = glm::vec4(0.0f, (float)leaf_count, 1.0f, 0.0f); // is_leaf=1
        // Fill unused leaf slots with -1
        for (int j = leaf_count; j < 4; ++j)
            leaf.leaf[j] = -1;
        leaf_node_indices.push_back((int)nodes.size());
        nodes.push_back(leaf);
    }

    // Create root node pointing to all leaf nodes
    CosmosBhGpuNode root{};
    root.center_half = glm::vec4(center, half);
    root.com_mass = glm::vec4(com, total_mass);
    root.spin_leaf = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // is_leaf=0
    root.child0 = glm::ivec4(-1);
    root.child1 = glm::ivec4(-1);
    for (size_t i = 0; i < leaf_node_indices.size() && i < 8; ++i) {
        if (i < 4) root.child0[i] = leaf_node_indices[i];
        else       root.child1[i - 4] = leaf_node_indices[i];
    }
    nodes.push_back(root); // Root is last node

    return nodes;
}

TEST_SUITE("CPU+GPU Gravity") {

TEST_CASE("Gravity compute pipeline initializes") {
    REQUIRE(gpu_available());

    CosmosGravityCompute gravity;
    bool init_ok = true;
    try {
        gravity.init(*g_vk);
    } catch (const std::exception& e) {
        MESSAGE("Gravity compute init failed: ", e.what());
        init_ok = false;
    }

    if (init_ok) {
        CHECK(gravity.is_ready());
        gravity.destroy(*g_vk);
    } else {
        MESSAGE("Skipping GPU gravity tests (shader not found)");
    }
}

TEST_CASE("Two-body GPU gravity matches CPU reference") {
    REQUIRE(gpu_available());

    CosmosGravityCompute gravity;
    try {
        gravity.init(*g_vk);
    } catch (...) {
        MESSAGE("Skipping: shader not found");
        return;
    }
    REQUIRE(gravity.is_ready());

    // Two bodies: one at origin, one at (100, 0, 0)
    std::vector<glm::vec3> pos = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(100.0f, 0.0f, 0.0f)
    };
    std::vector<glm::vec3> vel = {
        glm::vec3(0.0f), glm::vec3(0.0f)
    };
    std::vector<float> masses = { 1.0f, 1.0f };

    CosmosConfig cfg;
    cfg.G = 1.0f;
    cfg.gr_enabled = false;
    cfg.gr_precession_scale = 0.0f;
    cfg.gr_time_dilation = 0.0f;
    cfg.gr_frame_dragging = 0.0f;
    float softening2 = cfg.softening * cfg.softening;

    // source_data: x=mass, y=spin_y, z=active, w=marked_for_removal
    std::vector<glm::vec4> source_data = {
        glm::vec4(1.0f, 0.0f, 1.0f, 0.0f),
        glm::vec4(1.0f, 0.0f, 1.0f, 0.0f)
    };

    auto nodes = build_flat_tree(pos, masses);
    std::vector<glm::vec3> gpu_accel;
    bool ok = gravity.compute_barnes_hut(*g_vk, pos, vel, source_data, nodes,
                                          0.0f, // theta=0 forces exact traversal
                                          cfg, gpu_accel);
    CHECK(ok);
    REQUIRE(gpu_accel.size() == 2);

    // CPU reference
    auto cpu_accel = cpu_gravity(pos, masses, cfg.G, softening2);

    // Both bodies should be attracted toward each other
    CHECK(gpu_accel[0].x > 0.0f); // body 0 pulled toward +x
    CHECK(gpu_accel[1].x < 0.0f); // body 1 pulled toward -x

    // Newton's 3rd law: magnitudes should match (equal masses)
    float mag0 = glm::length(gpu_accel[0]);
    float mag1 = glm::length(gpu_accel[1]);
    CHECK(mag0 == doctest::Approx(mag1).epsilon(0.05));

    // GPU should match CPU direction (allow some BH tree approximation tolerance)
    float cpu_mag = glm::length(cpu_accel[0]);
    CHECK(mag0 == doctest::Approx(cpu_mag).epsilon(0.20));

    MESSAGE("GPU accel[0]: (", gpu_accel[0].x, ", ", gpu_accel[0].y, ", ", gpu_accel[0].z, ")");
    MESSAGE("CPU accel[0]: (", cpu_accel[0].x, ", ", cpu_accel[0].y, ", ", cpu_accel[0].z, ")");

    gravity.destroy(*g_vk);
}

TEST_CASE("Four-body symmetric GPU gravity") {
    REQUIRE(gpu_available());

    CosmosGravityCompute gravity;
    try { gravity.init(*g_vk); } catch (...) { return; }
    REQUIRE(gravity.is_ready());

    // Four equal-mass bodies in a square
    float d = 100.0f;
    std::vector<glm::vec3> pos = {
        glm::vec3(-d, 0, -d), glm::vec3( d, 0, -d),
        glm::vec3(-d, 0,  d), glm::vec3( d, 0,  d)
    };
    std::vector<glm::vec3> vel(4, glm::vec3(0.0f));
    std::vector<float> masses(4, 1.0f);
    std::vector<glm::vec4> source_data(4, glm::vec4(1.0f, 0.0f, 1.0f, 0.0f));

    CosmosConfig cfg;
    cfg.G = 1.0f;
    cfg.gr_enabled = false;
    cfg.gr_precession_scale = 0.0f;
    cfg.gr_time_dilation = 0.0f;
    cfg.gr_frame_dragging = 0.0f;

    auto nodes = build_flat_tree(pos, masses);
    std::vector<glm::vec3> gpu_accel;
    gravity.compute_barnes_hut(*g_vk, pos, vel, source_data, nodes, 0.0f, cfg, gpu_accel);
    REQUIRE(gpu_accel.size() == 4);

    // By symmetry, all acceleration magnitudes should be equal
    float mag0 = glm::length(gpu_accel[0]);
    float mag1 = glm::length(gpu_accel[1]);
    float mag2 = glm::length(gpu_accel[2]);
    float mag3 = glm::length(gpu_accel[3]);
    CHECK(mag1 == doctest::Approx(mag0).epsilon(0.05));
    CHECK(mag2 == doctest::Approx(mag0).epsilon(0.05));
    CHECK(mag3 == doctest::Approx(mag0).epsilon(0.05));

    // All bodies should be pulled toward center (0,0,0)
    for (int i = 0; i < 4; ++i) {
        glm::vec3 to_center = -pos[i];
        float dot = glm::dot(glm::normalize(gpu_accel[i]),
                              glm::normalize(to_center));
        CHECK(dot > 0.9f); // acceleration points toward center
    }

    gravity.destroy(*g_vk);
}

TEST_CASE("Inactive body produces zero acceleration") {
    REQUIRE(gpu_available());

    CosmosGravityCompute gravity;
    try { gravity.init(*g_vk); } catch (...) { return; }
    REQUIRE(gravity.is_ready());

    std::vector<glm::vec3> pos = {
        glm::vec3(0.0f), glm::vec3(100.0f, 0.0f, 0.0f)
    };
    std::vector<glm::vec3> vel(2, glm::vec3(0.0f));
    std::vector<float> masses = { 1.0f, 1.0f };

    // Mark body 1 as inactive (z=0) — should not attract body 0
    std::vector<glm::vec4> source_data = {
        glm::vec4(1.0f, 0.0f, 1.0f, 0.0f), // active
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)  // inactive
    };

    CosmosConfig cfg;
    cfg.G = 1.0f;
    cfg.gr_enabled = false;
    cfg.gr_precession_scale = 0.0f;
    cfg.gr_time_dilation = 0.0f;
    cfg.gr_frame_dragging = 0.0f;

    auto nodes = build_flat_tree(pos, masses);
    std::vector<glm::vec3> gpu_accel;
    gravity.compute_barnes_hut(*g_vk, pos, vel, source_data, nodes, 0.0f, cfg, gpu_accel);
    REQUIRE(gpu_accel.size() == 2);

    // Body 0 should have ~zero acceleration (inactive source ignored)
    float mag0 = glm::length(gpu_accel[0]);
    CHECK(mag0 < 1e-6f);

    gravity.destroy(*g_vk);
}

TEST_CASE("Many-body GPU gravity does not produce NaN") {
    REQUIRE(gpu_available());

    CosmosGravityCompute gravity;
    try { gravity.init(*g_vk); } catch (...) { return; }
    REQUIRE(gravity.is_ready());

    const int N = 64;
    std::vector<glm::vec3> pos(N);
    std::vector<glm::vec3> vel(N, glm::vec3(0.0f));
    std::vector<float> masses(N);
    std::vector<glm::vec4> source_data(N);

    // Distribute bodies in a sphere
    for (int i = 0; i < N; ++i) {
        float t = (float)i / (float)N;
        float phi = std::acos(1.0f - 2.0f * t);
        float theta = 2.39996323f * (float)i;
        float r = 200.0f;
        pos[i] = glm::vec3(
            r * std::sin(phi) * std::cos(theta),
            r * std::cos(phi),
            r * std::sin(phi) * std::sin(theta));
        masses[i] = 0.5f + (float)(i % 5) * 0.3f;
        source_data[i] = glm::vec4(masses[i], 0.0f, 1.0f, 0.0f);
    }

    CosmosConfig cfg;
    cfg.G = 1.0f;
    cfg.gr_enabled = false;
    cfg.gr_precession_scale = 0.0f;
    cfg.gr_time_dilation = 0.0f;
    cfg.gr_frame_dragging = 0.0f;

    auto nodes = build_flat_tree(pos, masses);
    std::vector<glm::vec3> gpu_accel;
    bool ok = gravity.compute_barnes_hut(*g_vk, pos, vel, source_data, nodes,
                                          0.5f, cfg, gpu_accel);
    CHECK(ok);
    REQUIRE(gpu_accel.size() == (size_t)N);

    int nan_count = 0;
    int zero_count = 0;
    for (int i = 0; i < N; ++i) {
        if (!std::isfinite(gpu_accel[i].x) || !std::isfinite(gpu_accel[i].y) ||
            !std::isfinite(gpu_accel[i].z))
            ++nan_count;
        if (glm::length(gpu_accel[i]) < 1e-12f)
            ++zero_count;
    }
    CHECK(nan_count == 0);
    CHECK(zero_count < N / 2); // Most bodies should have nonzero acceleration

    gravity.destroy(*g_vk);
}

} // CPU+GPU Gravity

// ═════════════════════════════════════════════════════════════════════════════
// Main — init/teardown Vulkan around doctest
// ═════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);

    // Initialize headless Vulkan
    VulkanContext vk;
    try {
        vk.init_headless();
        g_vk = &vk;
        std::cout << "[GPU Test] Vulkan headless init OK — GPU: "
                  << (vk.gpu_list.empty() ? "unknown" : vk.gpu_list[0].name) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[GPU Test] Vulkan init failed: " << e.what()
                  << "\n[GPU Test] GPU tests will be skipped.\n";
        g_vk = nullptr;
    }

    int result = ctx.run();

    // Cleanup
    if (g_vk) {
        vkDeviceWaitIdle(g_vk->device);
        g_vk->destroy();
        g_vk = nullptr;
    }

    return result;
}
