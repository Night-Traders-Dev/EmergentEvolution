// ── Cosmos Visual Tests ─────────────────────────────────────────────────────
// Renders individual celestial body types to PNG files for visual inspection.
// Uses headless Vulkan with an offscreen framebuffer → raytracer → readback.
// Output: test_renders/*.png  (one per body scenario)

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "common/vulkan_context.h"
#include "cosmos/rendering/cosmos_raytracer.h"
#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_app_internal.h"
#include "common/orbit_camera.h"
#include "third_party/stb_image_write.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <iostream>
#include <random>
#include <cstring>
#include <sys/stat.h>

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr uint32_t RENDER_W = 800;
static constexpr uint32_t RENDER_H = 600;
static constexpr VkFormat  COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

// ── Globals ─────────────────────────────────────────────────────────────────

static VulkanContext*    g_vk = nullptr;
static VkRenderPass      g_render_pass = VK_NULL_HANDLE;
static VkImage           g_color_image = VK_NULL_HANDLE;
static VkDeviceMemory    g_color_memory = VK_NULL_HANDLE;
static VkImageView       g_color_view = VK_NULL_HANDLE;
static VkFramebuffer     g_framebuffer = VK_NULL_HANDLE;
static CosmosRaytracer*  g_raytracer = nullptr;

static bool gpu_available() { return g_vk != nullptr && g_raytracer != nullptr; }

// ── Offscreen setup ─────────────────────────────────────────────────────────

static bool create_offscreen_resources() {
    VkDevice dev = g_vk->device;

    // ── Color image ─────────────────────────────────────────────────────
    VkImageCreateInfo img_ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_ci.imageType     = VK_IMAGE_TYPE_2D;
    img_ci.format        = COLOR_FORMAT;
    img_ci.extent        = {RENDER_W, RENDER_H, 1};
    img_ci.mipLevels     = 1;
    img_ci.arrayLayers   = 1;
    img_ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &img_ci, nullptr, &g_color_image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(dev, g_color_image, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_vk->physical_device, &mem_props);

    uint32_t mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_req.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type = i;
            break;
        }
    }
    if (mem_type == UINT32_MAX) return false;

    VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize  = mem_req.size;
    alloc_info.memoryTypeIndex = mem_type;
    if (vkAllocateMemory(dev, &alloc_info, nullptr, &g_color_memory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(dev, g_color_image, g_color_memory, 0);

    // ── Image view ──────────────────────────────────────────────────────
    VkImageViewCreateInfo view_ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_ci.image    = g_color_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format   = COLOR_FORMAT;
    view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &view_ci, nullptr, &g_color_view) != VK_SUCCESS)
        return false;

    // ── Render pass ─────────────────────────────────────────────────────
    VkAttachmentDescription color_att{};
    color_att.format         = COLOR_FORMAT;
    color_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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
    dep.srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &color_att;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies   = &dep;

    if (vkCreateRenderPass(dev, &rp_ci, nullptr, &g_render_pass) != VK_SUCCESS)
        return false;

    // ── Framebuffer ─────────────────────────────────────────────────────
    VkFramebufferCreateInfo fb_ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_ci.renderPass      = g_render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments    = &g_color_view;
    fb_ci.width           = RENDER_W;
    fb_ci.height          = RENDER_H;
    fb_ci.layers          = 1;

    if (vkCreateFramebuffer(dev, &fb_ci, nullptr, &g_framebuffer) != VK_SUCCESS)
        return false;

    return true;
}

static void destroy_offscreen_resources() {
    VkDevice dev = g_vk->device;
    if (g_framebuffer) vkDestroyFramebuffer(dev, g_framebuffer, nullptr);
    if (g_render_pass) vkDestroyRenderPass(dev, g_render_pass, nullptr);
    if (g_color_view)  vkDestroyImageView(dev, g_color_view, nullptr);
    if (g_color_image) vkDestroyImage(dev, g_color_image, nullptr);
    if (g_color_memory) vkFreeMemory(dev, g_color_memory, nullptr);
    g_framebuffer = VK_NULL_HANDLE;
    g_render_pass = VK_NULL_HANDLE;
    g_color_view  = VK_NULL_HANDLE;
    g_color_image = VK_NULL_HANDLE;
    g_color_memory = VK_NULL_HANDLE;
}

// ── Readback + save PNG ─────────────────────────────────────────────────────

static bool save_framebuffer_png(const char* filename) {
    VkDevice dev = g_vk->device;
    VkDeviceSize size = (VkDeviceSize)RENDER_W * RENDER_H * 4;

    Buffer staging = g_vk->create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBuffer cmd = g_vk->begin_single_command();

    // Image is already in TRANSFER_SRC_OPTIMAL from render pass finalLayout
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {RENDER_W, RENDER_H, 1};
    vkCmdCopyImageToBuffer(cmd, g_color_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle, 1, &region);

    g_vk->end_single_command(cmd);

    void* mapped = nullptr;
    vkMapMemory(dev, staging.memory, 0, size, 0, &mapped);

    std::vector<uint8_t> pixels(RENDER_W * RENDER_H * 4);
    std::memcpy(pixels.data(), mapped, size);
    vkUnmapMemory(dev, staging.memory);
    g_vk->destroy_buffer(staging);

    // Set alpha to 255
    for (uint32_t i = 0; i < RENDER_W * RENDER_H; ++i)
        pixels[i * 4 + 3] = 255;

    int ok = stbi_write_png(filename, (int)RENDER_W, (int)RENDER_H, 4,
                            pixels.data(), (int)(RENDER_W * 4));
    return ok != 0;
}

// ── Render a single body ────────────────────────────────────────────────────

struct BodyScenario {
    const char* name;       // filename-safe name
    uint32_t    type;       // CTYPE_*
    float       mass;       // solar masses
    float       temperature;
    float       radius;     // sim units
    int         forced_surface; // -1 = auto
    uint32_t    seed;
    int         bg_preset;  // skybox preset
    // Optional overrides
    float       angular_vel = 0.001f;
    float       fuel = 1.0f;
    uint32_t    stellar_stage = SSTAGE_MAIN_SEQUENCE;
    // Ring system (0 = no rings)
    float       ring_inner = 0.0f;   // multiplier of radius
    float       ring_outer = 0.0f;   // multiplier of radius
    float       ring_density = 0.0f;
    float       ring_ice_frac = 0.5f;
    float       ring_tilt = 0.1f;
};

static void render_scenario(const BodyScenario& sc) {
    CosmosState state;
    CosmosConfig cfg;
    cfg.G = 1.0f;
    cfg.cosmos_quality = 2;
    cfg.cosmos_background_preset = sc.bg_preset;
    cfg.cosmos_background_starfield = true;

    // Create the body
    CelestialBody b{};
    b.type = sc.type;
    b.mass = sc.mass;
    b.temperature = sc.temperature;
    b.radius = sc.radius;
    b.seed = sc.seed;
    b.angular_vel = sc.angular_vel;
    b.fuel = sc.fuel;
    b.stellar_stage = sc.stellar_stage;
    b.atmosphere_retention = 1.0f;
    b.forced_surface = sc.forced_surface;
    b.pos = glm::vec3(0.0f);
    b.vel = glm::vec3(0.0f);

    // Set up material phase
    if (is_star_type(b.type)) {
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    }

    // Randomize if planet/moon
    std::mt19937 rng(b.seed);
    if (b.type == CTYPE_PLANET) {
        randomize_planet_properties(b, state, cfg, rng);
        // Re-apply overrides after randomization
        b.mass = sc.mass;
        b.temperature = sc.temperature;
        b.radius = sc.radius;
        b.forced_surface = sc.forced_surface;
    } else if (b.type == CTYPE_MOON) {
        randomize_moon_properties(b, state, rng);
        b.mass = sc.mass;
        b.temperature = sc.temperature;
        b.radius = sc.radius;
        b.forced_surface = sc.forced_surface;
    } else if (b.type == CTYPE_ASTEROID) {
        randomize_small_body_properties(b, rng, false);
        b.radius = sc.radius;
    } else if (b.type == CTYPE_COMET) {
        randomize_small_body_properties(b, rng, true);
        b.radius = sc.radius;
    } else if (is_star_type(b.type)) {
        randomize_star_properties(b, rng, b.type);
        b.mass = sc.mass;
        b.temperature = sc.temperature;
        b.radius = sc.radius;
        b.stellar_stage = sc.stellar_stage;
        b.fuel = sc.fuel;
        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                    b.stellar_stage, b.fuel);
    } else if (is_galaxy_type(b.type)) {
        b.material_phase = PHASE_GAS;
        b.phase_intensity = 0.3f;
    }

    // Apply ring system if specified
    if (sc.ring_density > 0.0f && sc.ring_outer > 0.0f) {
        set_ring_system(b,
            b.radius * sc.ring_inner,
            b.radius * sc.ring_outer,
            sc.ring_density, sc.ring_ice_frac, sc.ring_tilt);
    }

    refresh_body_render_state(b, &state);
    state.bodies.push_back(b);

    // Camera: position to frame the body nicely
    // Stars need much more distance (emissive glow fills screen at close range)
    OrbitCamera camera;
    camera.target = glm::vec3(0.0f);
    if (is_star_type(sc.type)) {
        // Hotter/more luminous stars have larger corona glow — need more distance
        // Also scale by luminosity proxy (radius² × T⁴ ∝ L)
        float star_dist_mult = 20.0f;
        float lum_proxy = (sc.radius * sc.radius) * (sc.temperature / 5778.0f);
        if (lum_proxy > 50000.0f) star_dist_mult = 400.0f;          // extreme luminosity
        else if (lum_proxy > 10000.0f) star_dist_mult = 200.0f;     // very luminous
        else if (sc.temperature > 10000.0f) star_dist_mult = 150.0f; // hot but small
        else if (sc.temperature > 5000.0f) star_dist_mult = 30.0f;   // G type
        if (sc.stellar_stage == SSTAGE_WHITE_DWARF) star_dist_mult = 30.0f; // tiny, low glow
        camera.distance = sc.radius * star_dist_mult;  // stars: far enough to see corona + surface
    } else if (sc.type == CTYPE_BLACK_HOLE) {
        camera.distance = sc.radius * 6.0f;   // black holes: show accretion disk + lensing
    } else if (is_galaxy_type(sc.type)) {
        camera.distance = sc.radius * 8.0f;   // galaxies: show full extent + halo
    } else if (sc.ring_density > 0.0f && sc.ring_outer > 1.0f) {
        // Ringed bodies: frame to show full ring extent, higher elevation
        camera.distance = sc.radius * sc.ring_outer * 1.8f;
    } else {
        camera.distance = sc.radius * 3.0f;   // planets/moons/asteroids: fill more of the frame
    }
    camera.azimuth = 0.3f;
    // Higher elevation for ringed bodies and galaxies to show structure
    camera.elevation = (sc.ring_density > 0.0f) ? 0.65f :
                       is_galaxy_type(sc.type) ? 0.85f : 0.35f;
    camera.fov = 45.0f;

    // Allocate command buffer
    VkCommandBufferAllocateInfo alloc_ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_ci.commandPool        = g_vk->cmd_pool;
    alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_vk->device, &alloc_ci, &cmd);

    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Begin render pass
    VkRenderPassBeginInfo rp_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_begin.renderPass  = g_render_pass;
    rp_begin.framebuffer = g_framebuffer;
    rp_begin.renderArea  = {{0, 0}, {RENDER_W, RENDER_H}};
    VkClearValue clear_val{};
    clear_val.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues    = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Draw via raytracer
    g_raytracer->update_and_draw(*g_vk, cmd, state, camera, cfg,
                                  (float)RENDER_W, (float)RENDER_H, 0.0f);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(g_vk->queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_vk->queue);

    vkFreeCommandBuffers(g_vk->device, g_vk->cmd_pool, 1, &cmd);

    // Save PNG
    char path[512];
    snprintf(path, sizeof(path), "test_renders/%s.png", sc.name);
    bool saved = save_framebuffer_png(path);
    if (saved)
        std::cout << "  [OK] " << path << "\n";
    else
        std::cerr << "  [FAIL] Could not save " << path << "\n";
    CHECK(saved);
}

// ═════════════════════════════════════════════════════════════════════════════
// TEST SCENARIOS
// ═════════════════════════════════════════════════════════════════════════════

static const BodyScenario SCENARIOS[] = {
    // ── Stars ───────────────────────────────────────────────────────────
    {"star_g_type_sun",     CTYPE_STAR_G,  1.0f,   5778.0f, 80.0f, -1, 42,    0, 0.001f, 0.85f, SSTAGE_MAIN_SEQUENCE},
    {"star_m_type_red",     CTYPE_STAR_M,  0.3f,   3200.0f, 50.0f, -1, 1337,  0, 0.001f, 0.95f, SSTAGE_MAIN_SEQUENCE},
    {"star_o_type_blue",    CTYPE_STAR_O,  30.0f, 35000.0f, 120.0f,-1, 7777,  0, 0.001f, 0.90f, SSTAGE_MAIN_SEQUENCE},
    {"star_red_giant",      CTYPE_STAR_K,  1.5f,   3800.0f, 80.0f, -1, 555,   0, 0.0005f,0.15f, SSTAGE_RED_GIANT},
    {"star_white_dwarf",    CTYPE_STAR_A,  0.8f,  25000.0f, 12.0f, -1, 9999,  0, 0.01f,  0.0f,  SSTAGE_WHITE_DWARF},

    // ── Planets — rocky types ───────────────────────────────────────────
    {"planet_earth_like",   CTYPE_PLANET,  3.0e-6f, 288.0f, 40.0f, 3, 100,   0},
    {"planet_mars_like",    CTYPE_PLANET,  1.0e-6f, 210.0f, 35.0f, 0, 200,   0},
    {"planet_venus_hot",    CTYPE_PLANET,  2.5e-6f, 735.0f, 38.0f, 0, 300,   0},
    {"planet_frozen_ice",   CTYPE_PLANET,  1.5e-6f, 100.0f, 36.0f, 2, 400,   0},
    {"planet_lava_world",   CTYPE_PLANET,  4.0e-6f,1200.0f, 42.0f, 0, 500,   0},
    {"planet_ocean_world",  CTYPE_PLANET,  5.0e-6f, 300.0f, 45.0f, 1, 600,   0},

    // ── Planets — gas giants ────────────────────────────────────────────
    {"planet_gas_giant_1",  CTYPE_PLANET,  3.0e-4f, 165.0f,120.0f, 4, 700,   0},
    {"planet_gas_giant_2",  CTYPE_PLANET,  1.0e-3f, 120.0f,150.0f, 4, 800,   0},
    {"planet_ice_giant",    CTYPE_PLANET,  5.0e-5f,  72.0f, 80.0f, 4, 900,   0},

    // ── Moons ───────────────────────────────────────────────────────────
    {"moon_rocky_grey",     CTYPE_MOON,    1.0e-8f, 220.0f, 18.0f, 0, 1001,  0},
    {"moon_icy_europa",     CTYPE_MOON,    8.0e-9f, 100.0f, 16.0f, 2, 1002,  0},
    {"moon_volcanic_io",    CTYPE_MOON,    1.5e-8f, 400.0f, 17.0f, 0, 1003,  0},
    {"moon_titan_like",     CTYPE_MOON,    2.0e-8f, 94.0f,  20.0f,-1, 1004,  0},
    {"moon_dark_carb",      CTYPE_MOON,    5.0e-9f, 170.0f, 14.0f, 0, 1005,  0},

    // ── Small bodies ────────────────────────────────────────────────────
    {"asteroid_rocky",      CTYPE_ASTEROID,1.0e-12f,200.0f, 10.0f,-1, 2001,  0},
    {"comet_icy",           CTYPE_COMET,   5.0e-13f,180.0f,  8.0f,-1, 2002,  0},

    // ── Black hole ──────────────────────────────────────────────────────
    {"black_hole",          CTYPE_BLACK_HOLE, 10.0f, 0.0f, 60.0f, -1, 3001, 1},

    // ── Ringed planets ──────────────────────────────────────────────────
    //  Saturn-like: prominent icy rings                                           ring_in ring_out density ice  tilt
    {"ringed_saturn_like",  CTYPE_PLANET,  9.54e-4f, 134.0f,100.0f, 4, 4001, 0, 0.001f, 1.0f, SSTAGE_MAIN_SEQUENCE, 1.53f, 4.0f, 0.55f, 0.92f, 0.47f},
    //  Uranus-like: thin dark rings, extreme tilt
    {"ringed_ice_giant",    CTYPE_PLANET,  5.0e-5f,   76.0f, 65.0f, 4, 4002, 0, 0.001f, 1.0f, SSTAGE_MAIN_SEQUENCE, 1.64f, 2.0f, 0.08f, 0.35f, 1.30f},
    //  Rocky planet with faint dusty ring
    {"ringed_rocky",        CTYPE_PLANET,  2.0e-6f,  220.0f, 32.0f, 0, 4003, 0, 0.001f, 1.0f, SSTAGE_MAIN_SEQUENCE, 1.40f, 2.8f, 0.25f, 0.15f, 0.20f},

    // ── Galaxies ────────────────────────────────────────────────────────
    {"galaxy_spiral",       CTYPE_GALAXY_SPIRAL,     1.0e10f, 3.0f, 200.0f, -1, 5001, 0},
    {"galaxy_elliptical",   CTYPE_GALAXY_ELLIPTICAL,  5.0e11f, 3.0f, 250.0f, -1, 5002, 0},
    {"galaxy_irregular",    CTYPE_GALAXY_IRREGULAR,   1.0e9f,  3.0f, 150.0f, -1, 5003, 0},
    {"galaxy_lenticular",   CTYPE_GALAXY_LENTICULAR,  2.0e11f, 3.0f, 220.0f, -1, 5004, 0},
    {"galaxy_dwarf",        CTYPE_GALAXY_DWARF,       1.0e8f,  3.0f, 100.0f, -1, 5005, 0},

    // ── Skybox variety (use tiny asteroid so corona/glow doesn't wash out background) ──
    {"skybox_default",      CTYPE_ASTEROID,1.0e-15f,100.0f, 0.01f,-1, 42,   0},
    {"skybox_nebula",       CTYPE_ASTEROID,1.0e-15f,100.0f, 0.01f,-1, 42,   2},
    {"skybox_warm_dust",    CTYPE_ASTEROID,1.0e-15f,100.0f, 0.01f,-1, 42,   3},
    {"skybox_deep_black",   CTYPE_ASTEROID,1.0e-15f,100.0f, 0.01f,-1, 42,   1},
};

static constexpr int SCENARIO_COUNT = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

// ═════════════════════════════════════════════════════════════════════════════
// TESTS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Visual Rendering") {

TEST_CASE("Render all body scenarios to PNG") {
    REQUIRE(gpu_available());

    std::cout << "\n  Rendering " << SCENARIO_COUNT << " scenarios...\n";
    for (int i = 0; i < SCENARIO_COUNT; ++i) {
        SUBCASE(SCENARIOS[i].name) {
            render_scenario(SCENARIOS[i]);
        }
    }
}

} // Visual Rendering

// ═════════════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);

    // Create output directory
    mkdir("test_renders", 0755);

    // Initialize headless Vulkan
    VulkanContext vk;
    try {
        vk.init_headless();
        g_vk = &vk;
        std::cout << "[Visual Test] Vulkan headless init OK — GPU: "
                  << (vk.gpu_list.empty() ? "unknown" : vk.gpu_list[0].name) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[Visual Test] Vulkan init failed: " << e.what() << "\n";
        return 1;
    }

    // Create offscreen resources
    if (!create_offscreen_resources()) {
        std::cerr << "[Visual Test] Failed to create offscreen framebuffer\n";
        vk.destroy();
        return 1;
    }

    // Initialize raytracer
    CosmosRaytracer raytracer;
    try {
        raytracer.init(vk, g_render_pass);
        g_raytracer = &raytracer;
        std::cout << "[Visual Test] Raytracer initialized OK\n";
    } catch (const std::exception& e) {
        std::cerr << "[Visual Test] Raytracer init failed: " << e.what() << "\n";
        destroy_offscreen_resources();
        vk.destroy();
        return 1;
    }

    int result = ctx.run();

    // Cleanup
    vkDeviceWaitIdle(vk.device);
    raytracer.destroy(vk);
    g_raytracer = nullptr;
    destroy_offscreen_resources();
    vk.destroy();
    g_vk = nullptr;

    if (result == 0)
        std::cout << "\n[Visual Test] All renders saved to test_renders/\n";

    return result;
}
