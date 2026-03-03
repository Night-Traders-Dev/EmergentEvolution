#pragma once
// ── Cosmic Sandbox — Application ────────────────────────────────────────────

#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_raytracer.h"
#include "common/simple_renderer.h"

struct GLFWwindow;

class CosmosApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();

    VulkanContext   vk;
    SimpleRenderer  renderer;
    CosmosConfig    cfg;
    CosmosState     state;
    OrbitCamera     camera;
    bool            paused = false;

    // Input state (public for GLFW callbacks)
    bool   mouse_dragging = false;
    double last_mouse_x = 0, last_mouse_y = 0;
    int    selected_body = -1;
    int    spawn_type = CTYPE_PLANET;
    float  spawn_mass = 1.0f;

private:
    void render_ui();
    void render_overlay();   // trails + selection (DrawList overlay on top of raytraced scene)
    void step_physics(float dt);

    // 3D projection helpers (for overlay drawing only)
    struct Projected { float sx, sy, depth; bool visible; };
    Projected project(const glm::vec3& world_pos, const glm::mat4& vp,
                      float screen_w, float screen_h) const;
    float screen_radius(float world_radius, float depth, float fov_rad,
                        float screen_h) const;

    CosmosRaytracer raytracer_;
    float sim_time_ = 0.0f;
};
