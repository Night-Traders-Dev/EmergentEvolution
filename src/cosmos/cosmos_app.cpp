#include "cosmos/cosmos_app_internal.h"
#include "cosmos/data/cosmos_astro_data.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "third_party/stb_image_write.h"
#include <ctime>

#if !defined(_WIN32)
#include <unistd.h>
#endif

// ── Lifecycle ────────────────────────────────────────────────────────────────

void CosmosApp::init(GLFWwindow* window, ProgressCB progress_cb) {
    auto report = [&](float frac, const char* label) {
        if (progress_cb) progress_cb(frac, label);
    };

    cosmos_install_crash_handlers();

    report(0.0f, "Initializing Vulkan...");
    vk.init(window);
    glfwPollEvents(); // keep window responsive during init

    report(0.15f, "Creating renderer...");
    renderer.init(vk, window, true); // enable depth buffer for terrain mesh rendering
    glfwPollEvents();

    report(0.30f, "Compiling shaders...");
    raytracer_.init(vk, renderer.render_pass());
    mesh_renderer_.init(vk, renderer.render_pass());
    glfwPollEvents();

    report(0.60f, "Initializing physics...");
    gravity_compute_.init(vk);
    glfwPollEvents();

    report(0.70f, "Loading astronomical data...");
    AstroData::init("external/SSCore/SSData");
    glfwPollEvents();

    report(0.80f, "Initializing terrain generator...");
    terrain_.init();
    glfwPollEvents();

    report(0.90f, "Loading settings...");
    state.clear();
    cfg.body_count = 0;

    camera.target = {0, 0, 0};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    camera.azimuth = 0.0f;

    load_persistent_settings();

    report(1.0f, "Ready");
    debug_logf("init complete diagnostics=%d pause_on_invalid=%d bh=%d gpu_bh=%d verlet=%d",
               diagnostics_enabled_ ? 1 : 0, diagnostics_pause_on_invalid_ ? 1 : 0,
               cfg.barnes_hut ? 1 : 0, cfg.gpu_barnes_hut ? 1 : 0, cfg.velocity_verlet ? 1 : 0);
}

// ── Screen → world spawn position ────────────────────────────────────────────

glm::vec3 CosmosApp::screen_to_spawn_pos(double mx, double my, int fb_w, int fb_h) const {
    // Guard against NaN camera state — return origin so spawn validation rejects it gracefully
    if (!std::isfinite(camera.target.x) || !std::isfinite(camera.target.y) ||
        !std::isfinite(camera.target.z) || !std::isfinite(camera.distance) ||
        fb_w <= 0 || fb_h <= 0)
        return glm::vec3(std::numeric_limits<float>::quiet_NaN());

    float W = (float)fb_w, H = (float)fb_h;
    float aspect = W / H;
    glm::dmat4 inv_vp = glm::inverse(camera.proj_matrix_d(aspect) * camera.view_matrix_d());
    float ndc_x = ((float)mx / W) * 2.0f - 1.0f;
    float ndc_y = 1.0f - ((float)my / H) * 2.0f;
    glm::dvec4 near_clip = inv_vp * glm::dvec4(ndc_x, ndc_y, -1.0, 1.0);
    glm::dvec4 far_clip  = inv_vp * glm::dvec4(ndc_x, ndc_y,  1.0, 1.0);
    glm::dvec3 near_pt = glm::dvec3(near_clip) / near_clip.w;
    glm::dvec3 far_pt  = glm::dvec3(far_clip) / far_clip.w;
    glm::dvec3 ray_dir = glm::normalize(far_pt - near_pt);
    glm::dvec3 eye = camera.eye_position_d();
    glm::dvec3 plane_normal = glm::normalize(glm::dvec3(camera.target) - eye);
    double denom = glm::dot(ray_dir, plane_normal);
    glm::vec3 pos = camera.target;
    if (std::abs(denom) > 1e-9) {
        double t = glm::dot(glm::dvec3(camera.target) - near_pt, plane_normal) / denom;
        if (t > 0.0)
            pos = glm::vec3(near_pt + ray_dir * t);
    }
    return pos;
}

// ── Body picking (screen-space hit test) ─────────────────────────────────────

int CosmosApp::pick_body(float mx, float my, float W, float H, bool skip_fragments) const {
    float aspect = W / H;
    glm::dmat4 vp = camera.proj_matrix_d(aspect) * camera.view_matrix_d();
    float fov_rad = glm::radians(camera.fov);

    int best = -1;
    float best_dist = 30.0f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        if (b.marked_for_removal) continue;
        // In spawn mode, skip small fragments/dust so they don't block spawning
        if (skip_fragments && (b.non_attracting || fragment_like_body(b) || b.type == CTYPE_DUST))
            continue;
        glm::dvec4 clip = vp * glm::dvec4(b.pos, 1.0);
        if (clip.w <= 0.0) continue;
        glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
        float sx = (float)((ndc.x * 0.5 + 0.5) * (double)W);
        float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)H);

        float dx = sx - mx;
        float dy = sy - my;
        float d = std::sqrt(dx * dx + dy * dy);
        float sr = (float)(((double)b.radius / clip.w) * ((double)H / (2.0 * std::tan((double)fov_rad * 0.5))));
        float pick_r = std::max(sr, 12.0f);
        if (d < pick_r && d < best_dist) {
            best_dist = d;
            best = (int)i;
        }
    }
    return best;
}

void CosmosApp::capture_screenshot(GLFWwindow* window) {
    vkDeviceWaitIdle(vk.device);
    uint32_t w = vk.swapchain_extent.width;
    uint32_t h = vk.swapchain_extent.height;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    Buffer staging = vk.create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBuffer cmd = vk.begin_single_command();

    // Transition swapchain image to TRANSFER_SRC
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vk.swapchain_images[renderer.current_image_index()];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, vk.swapchain_images[renderer.current_image_index()],
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle, 1, &region);

    // Transition back to PRESENT_SRC
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vk.end_single_command(cmd);

    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, size, 0, &mapped);

    // Convert BGRA to RGBA if needed
    std::vector<uint8_t> pixels(w * h * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(mapped);
    bool is_bgra = (vk.swapchain_format == VK_FORMAT_B8G8R8A8_UNORM ||
                    vk.swapchain_format == VK_FORMAT_B8G8R8A8_SRGB);
    for (uint32_t i = 0; i < w * h; ++i) {
        if (is_bgra) {
            pixels[i * 4 + 0] = src[i * 4 + 2]; // R
            pixels[i * 4 + 1] = src[i * 4 + 1]; // G
            pixels[i * 4 + 2] = src[i * 4 + 0]; // B
        } else {
            pixels[i * 4 + 0] = src[i * 4 + 0];
            pixels[i * 4 + 1] = src[i * 4 + 1];
            pixels[i * 4 + 2] = src[i * 4 + 2];
        }
        pixels[i * 4 + 3] = 255;
    }
    vkUnmapMemory(vk.device, staging.memory);
    vk.destroy_buffer(staging);

    // Generate filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    char filename[256];
    snprintf(filename, sizeof(filename), "cosmos_screenshot_%04d%02d%02d_%02d%02d%02d.png",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    stbi_write_png(filename, (int)w, (int)h, 4, pixels.data(), (int)(w * 4));

    last_save_status_ = std::string("Screenshot saved: ") + filename;
    save_status_timer_ = 3.0f;
}

void CosmosApp::destroy() {
    save_persistent_settings();
    AstroData::shutdown();
    vkDeviceWaitIdle(vk.device);
    gravity_compute_.destroy(vk);
    mesh_renderer_.destroy(vk);
    raytracer_.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

void CosmosApp::load_preset(int index) {
    if (index < 0 || index >= COSMOS_PRESET_COUNT) return;
    state.clear();
    terrain_cache_.clear();
    terrain_meshes_dirty_ = true;

    // Reset config to clean defaults (preserves display-only settings)
    const int preserved_nebula_render_mode = cfg.nebula_render_mode;
    cfg = CosmosConfig{};
    cfg.nebula_render_mode = preserved_nebula_render_mode;
    cfg.body_count = 0;
    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);

    selected_body = -1;
    inspector_visible_ = false;
    sim_time_ = 0.0f;
    cfg.sim_time_accumulated = 0.0;
    displayed_time_rate_ = 0.0;
    adaptive_substeps_last_ = 1;
    adaptive_substeps_required_ = 1;
    adaptive_substeps_saturated_ = false;
    adaptive_substep_refining_ = false;
    escaped_mass_total_ = 0.0;
    radiated_energy_total_ = 0.0;
    escaped_energy_total_ = 0.0;
    escaped_momentum_total_ = glm::dvec3(0.0);
    camera = OrbitCamera{};

    COSMOS_PRESETS[index].build(state, cfg, camera);

    // Finalize all bodies
    for (auto& b : state.bodies) refresh_body_render_state(b, &state);
    state.trails.resize(state.bodies.size());
    cfg.body_count = static_cast<uint32_t>(state.count());
    update_body_tracking_cache();
    apply_dust_debug_mode();
    paused = false;
}

void CosmosApp::reset_simulation() {
    load_preset(0);
}

void CosmosApp::reset_bottom_bar_menu_defaults() {
    const int preserved_nebula_render_mode = cfg.nebula_render_mode;
    const uint32_t preserved_body_count = static_cast<uint32_t>(state.count());
    const double preserved_sim_time = cfg.sim_time_accumulated;

    cfg = CosmosConfig{};
    cfg.nebula_render_mode = preserved_nebula_render_mode;
    cfg.body_count = preserved_body_count;
    cfg.sim_time_accumulated = preserved_sim_time;
    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);

    camera = OrbitCamera{};
    bottom_bar_autohide_ = true;
    diagnostics_enabled_ = true;
    diagnostics_pause_on_invalid_ = true;
    displayed_time_rate_ = paused ? 0.0 : (double)cfg.dt_scale * (reverse_time_ ? -1.0 : 1.0);
    adaptive_substeps_last_ = 1;
    adaptive_substeps_required_ = 1;
    adaptive_substeps_saturated_ = false;
    adaptive_substep_refining_ = false;
    escaped_mass_total_ = 0.0;
    radiated_energy_total_ = 0.0;
    escaped_energy_total_ = 0.0;
    escaped_momentum_total_ = glm::dvec3(0.0);

    apply_dust_debug_mode();
    update_body_tracking_cache();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    // WASD camera panning (breaks focus tracking)
    if (!show_splash && !show_pause_menu && !ImGui::GetIO().WantTextInput) {
        float move_speed = camera.distance * 0.5f * dt;
        glm::vec3 fwd = camera.forward_direction();
        glm::vec3 right = camera.right_direction();
        fwd.y = 0; fwd = glm::normalize(fwd);
        right.y = 0; right = glm::normalize(right);

        bool moved = false;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { camera.target += fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { camera.target -= fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { camera.target += right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { camera.target -= right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) { camera.target.y += move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) { camera.target.y -= move_speed; moved = true; }
        if (moved) camera.release_focus();
    }

    // Track focused body (update position each frame so camera follows)
    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        const auto& fb = state.bodies[camera.focus_body];
        if (fb.marked_for_removal ||
            !std::isfinite(fb.pos.x) || !std::isfinite(fb.pos.y) || !std::isfinite(fb.pos.z)) {
            camera.release_focus();
        } else {
            camera.track_body(fb.pos);
        }
    } else if (camera.focus_active && camera.focus_body >= 0) {
        // Body was removed or index out of bounds
        camera.release_focus();
    }

    // Smooth camera animation (zoom + focus lerp)
    camera.update(dt);

    if (!paused && !show_splash) {
        try {
            int substeps = std::clamp(cfg.physics_substeps, 1, 16);
            float step_dt = dt / (float)substeps;
            for (int substep = 0; substep < substeps; ++substep)
                step_physics(step_dt);
            sim_time_ += dt;
        } catch (const std::exception& e) {
            debug_logf("step_physics exception: %s", e.what());
            paused = true;
        } catch (...) {
            debug_logf("step_physics exception: unknown");
            paused = true;
        }
    }

    // ── Spawn preview ghost body ────────────────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.preview.body = nullptr;
    if (spawn_menu_visible_ && !show_splash && !show_pause_menu &&
        !mouse_dragging && !mouse_panning && !io.WantCaptureMouse) {
        // Rebuild preview body when spawn type, mass, or draft settings change
        uint32_t dh = draft_settings_hash();
        if (!preview_body_valid_ || preview_last_type_ != spawn_type ||
            preview_last_mass_ != spawn_mass || preview_last_draft_hash_ != dh) {
            build_preview_body();
        }

        if (spawn_dragging_) {
            // During spawn drag: body stays at click XZ, Y offset from drag
            glm::vec3 drag_pos = spawn_drag_base_pos_;
            drag_pos.y += spawn_drag_y_offset_;
            spawn_preview_pos_ = drag_pos;
            preview_body_.pos = drag_pos;
            raytracer_.preview.body = &preview_body_;
        } else {
            int fb_w, fb_h;
            glfwGetFramebufferSize(window, &fb_w, &fb_h);
            spawn_preview_pos_ = screen_to_spawn_pos(last_mouse_x, last_mouse_y, fb_w, fb_h);
            bool spawn_pos_valid = std::isfinite(spawn_preview_pos_.x) &&
                                   std::isfinite(spawn_preview_pos_.y) &&
                                   std::isfinite(spawn_preview_pos_.z);

            // Only show preview when no significant body is under cursor
            // (skip fragments/dust so they don't block the spawn preview)
            int hover_body = pick_body((float)last_mouse_x, (float)last_mouse_y,
                                        (float)fb_w, (float)fb_h, true);
            if (hover_body < 0 && spawn_pos_valid) {
                preview_body_.pos = spawn_preview_pos_;
                raytracer_.preview.body = &preview_body_;
            }
        }
    }

    // Dynamic grid sizing — scale grid squares with camera zoom (like Universe Sandbox)
    if (cfg.cosmos_space_fabric) {
        float fov_rad = glm::radians(camera.fov);
        float visible_width = 2.0f * camera.distance * std::tan(fov_rad * 0.5f);
        float ideal = visible_width / 12.0f; // ~12 squares across screen
        // Snap to nearest 1-2-5 step
        float log10_ideal = std::log10(std::max(ideal, 1.0e-6f));
        float decade = std::floor(log10_ideal);
        float frac = log10_ideal - decade;
        float base = std::pow(10.0f, decade);
        float snapped;
        if (frac < 0.15f)       snapped = base;
        else if (frac < 0.50f)  snapped = base * 2.0f;
        else if (frac < 0.85f)  snapped = base * 5.0f;
        else                    snapped = base * 10.0f;
        cfg.cosmos_space_fabric_grid_size = std::max(snapped, 1.0f);
    }

    // GPU raytraced scene (draws within the active render pass)
    try {
        raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                   io.DisplaySize.x, io.DisplaySize.y, sim_time_);
    } catch (const std::exception& e) {
        debug_logf("raytracer exception: %s", e.what());
        paused = true;
    } catch (...) {
        debug_logf("raytracer exception: unknown");
        paused = true;
    }

    // Terrain mesh rendering (after raytracer, with depth testing)
    try {
        rebuild_terrain_cache();
        upload_terrain_meshes();
        mesh_renderer_.draw(renderer.current_cmd(), state.bodies, camera,
                            io.DisplaySize.x, io.DisplaySize.y, sim_time_);
    } catch (...) {
        // Terrain rendering is non-critical — continue without it
    }

    // DrawList overlays (trails, selection, focus indicator)
    if (!show_splash && !show_pause_menu)
        render_overlay();

    // ImGui UI panels
    render_ui();

    renderer.end_frame(vk);

    if (screenshot_requested_) {
        screenshot_requested_ = false;
        capture_screenshot(window);
    }
}

// ── Terrain mesh cache ──────────────────────────────────────────────────────

void CosmosApp::rebuild_terrain_cache() {
    // Resize cache if body count changed
    if (terrain_cache_.size() != state.bodies.size()) {
        terrain_cache_.resize(state.bodies.size());
        terrain_meshes_dirty_ = true;
    }

    for (size_t i = 0; i < state.bodies.size(); ++i) {
        const auto& body = state.bodies[i];
        auto& entry = terrain_cache_[i];

        // Skip non-renderable types (dust, nebula fragments, black holes)
        if (body.type == CTYPE_DUST || body.type == CTYPE_NEBULA ||
            is_black_hole_type(body.type)) {
            if (entry.valid) terrain_meshes_dirty_ = true;
            entry.valid = false;
            continue;
        }

        // Only regenerate if seed or radius changed
        if (entry.valid && entry.seed == body.seed &&
            std::abs(entry.radius - body.radius) < 0.01f) {
            continue;
        }

        // Generate mesh centered at origin (body-relative) with fixed resolution
        // LOD is handled by screen-size gating in the draw call
        TerrainParams params = TerrainParams::from_body(body);
        // Clamp terrain amplitude for mesh geometry — visual properties can have
        // terrain_amp up to 1.0 which creates wild deformations. Real terrain is
        // tiny relative to body radius (Earth: ~0.14%). Cap at 5% for meshes.
        params.terrain_amp = std::min(params.terrain_amp, 0.05f);
        params.ridge_amp   = std::min(params.ridge_amp, 0.02f);
        entry.mesh = terrain_.generate_terrain_mesh(32, params);
        entry.seed = body.seed;
        entry.radius = body.radius;
        entry.valid = true;
        terrain_meshes_dirty_ = true;
    }
}

void CosmosApp::upload_terrain_meshes() {
    if (!terrain_meshes_dirty_) return;
    terrain_meshes_dirty_ = false;
    mesh_renderer_.upload_meshes(vk, terrain_cache_, state.bodies);
}
