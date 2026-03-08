#pragma once
// ── OrbitCamera — Universe Sandbox–style 3D camera ─────────────────────────

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

struct OrbitCamera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance  = 600.0f;
    float azimuth   = 0.0f;     // radians, horizontal rotation
    float elevation = 0.5f;     // radians, vertical angle
    float fov       = 45.0f;    // degrees
    float near_clip = 0.1f;
    float far_clip  = 10000.0f;

    // Focus tracking — smoothly follow a world-space target
    bool      focus_active = false;
    int       focus_body   = -1;         // index of tracked body (-1 = none)
    glm::vec3 focus_target{0.0f};        // smoothed goal position
    float     focus_lerp_speed = 6.0f;   // how fast camera catches up
    float     focus_body_radius = 1.0f;  // radius of focused body (for min zoom)

    // Smooth zoom (logarithmic)
    float target_distance = 600.0f;
    float zoom_speed      = 8.0f;

    // Orbit inertia
    float orbit_vel_az  = 0.0f;
    float orbit_vel_el  = 0.0f;
    float inertia_decay = 5.0f;  // how fast inertia decays (higher = faster stop)
    bool  orbiting      = false; // true while user is actively dragging

    glm::dvec3 eye_position_d() const {
        double cos_el = std::cos((double)elevation);
        return glm::dvec3(target) + glm::dvec3(
            (double)distance * cos_el * std::sin((double)azimuth),
            (double)distance * std::sin((double)elevation),
            (double)distance * cos_el * std::cos((double)azimuth)
        );
    }

    glm::vec3 eye_position() const {
        return glm::vec3(eye_position_d());
    }

    glm::dmat4 view_matrix_d() const {
        return glm::lookAt(eye_position_d(), glm::dvec3(target), glm::dvec3(0.0, 1.0, 0.0));
    }

    glm::mat4 view_matrix() const {
        return glm::mat4(view_matrix_d());
    }

    glm::dmat4 proj_matrix_d(float aspect) const {
        double dynamic_far = std::max((double)far_clip, (double)distance * 4.0);
        double dynamic_near = std::max((double)near_clip, dynamic_far * 1.0e-6);
        dynamic_near = std::max(dynamic_near, std::min((double)distance * 1.0e-4, dynamic_far * 0.25));
        if (distance < 500.0f)
            dynamic_near = std::min(dynamic_near, 0.1);
        dynamic_near = std::min(dynamic_near, dynamic_far * 0.25);
        return glm::perspective(glm::radians((double)fov), (double)aspect, dynamic_near, dynamic_far);
    }

    glm::mat4 proj_matrix(float aspect) const {
        return glm::mat4(proj_matrix_d(aspect));
    }

    glm::vec3 forward_direction() const {
        return glm::normalize(target - eye_position());
    }

    glm::vec3 right_direction() const {
        return glm::normalize(glm::cross(forward_direction(), glm::vec3(0, 1, 0)));
    }

    glm::vec3 up_direction() const {
        return glm::normalize(glm::cross(right_direction(), forward_direction()));
    }

    // Universe Sandbox-style orbit: drag rotates around target
    void orbit(float dx, float dy, float sensitivity = 0.005f) {
        float daz = -dx * sensitivity;
        float del = dy * sensitivity;
        azimuth   += daz;
        elevation += del;
        elevation = std::clamp(elevation, -1.55f, 1.55f);

        // Feed inertia
        orbit_vel_az = daz;
        orbit_vel_el = del;
    }

    // Pan: shift target in the screen plane
    void pan(float dx, float dy) {
        // Scale pan speed logarithmically with distance for consistent feel
        float scale = distance * 0.0015f;
        glm::vec3 r = right_direction();
        glm::vec3 u = up_direction();
        target += r * (-dx * scale) + u * (dy * scale);
        // Panning breaks focus
        if (focus_active) {
            focus_active = false;
            focus_body = -1;
        }
    }

    // Smooth logarithmic zoom — feels consistent at all scales
    void zoom(float delta) {
        // Logarithmic: each scroll step multiplies distance by a constant ratio
        float factor = std::pow(0.88f, delta);  // ~12% per notch
        target_distance *= factor;
        float min_dist = 1.0f;
        if (focus_active)
            min_dist = std::max(1.0f, focus_body_radius * 1.5f);
        target_distance = std::max(target_distance, min_dist);
    }

    // Zoom toward a world-space point (scroll zooms toward cursor position)
    void zoom_toward(float delta, glm::vec3 world_point) {
        float old_dist = target_distance;
        zoom(delta);
        float new_dist = target_distance;

        // Shift target toward the world point proportional to zoom change
        float shift = 1.0f - (new_dist / old_dist);
        glm::vec3 offset = world_point - target;
        target += offset * shift * 0.5f;

        // Zooming toward a point breaks focus
        if (focus_active && glm::length(offset) > focus_body_radius * 2.0f) {
            focus_active = false;
            focus_body = -1;
        }
    }

    // Focus on a position: snap camera to body, then track it
    void focus_on(glm::vec3 pos, int body_idx = -1, float body_radius = 1.0f) {
        focus_active = true;
        focus_body = body_idx;
        focus_target = pos;
        target = pos;  // snap immediately — don't rely on slow lerp
        focus_body_radius = std::max(body_radius, 0.1f);
    }

    // Stop following a body but keep the camera where it is
    void release_focus() {
        focus_active = false;
        focus_body = -1;
    }

    // Signal that the user started/stopped orbiting (for inertia)
    void begin_orbit() { orbiting = true; }
    void end_orbit() { orbiting = false; }

    // Call every frame to animate smooth zoom + focus tracking + inertia
    void update(float dt) {
        // NaN recovery: if target or distance became invalid, reset to safe defaults
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) {
            target = glm::vec3(0.0f);
            focus_target = glm::vec3(0.0f);
            release_focus();
        }
        if (!std::isfinite(distance) || distance <= 0.0f) {
            distance = 600.0f;
            target_distance = 600.0f;
        }

        // Smooth zoom (exponential interpolation for consistent feel)
        float zoom_t = 1.0f - std::exp(-zoom_speed * dt);
        distance += (target_distance - distance) * zoom_t;

        // Focus tracking: smoothly move target toward focus_target
        if (focus_active) {
            if (std::isfinite(focus_target.x) && std::isfinite(focus_target.y) && std::isfinite(focus_target.z)) {
                float t = 1.0f - std::exp(-focus_lerp_speed * dt);
                target = glm::mix(target, focus_target, t);
            } else {
                release_focus();
            }
        }

        // Orbit inertia: apply residual velocity when not actively dragging
        if (!orbiting && (std::abs(orbit_vel_az) > 1e-5f || std::abs(orbit_vel_el) > 1e-5f)) {
            azimuth   += orbit_vel_az;
            elevation += orbit_vel_el;
            elevation = std::clamp(elevation, -1.55f, 1.55f);

            // Exponential decay
            float decay = std::exp(-inertia_decay * dt);
            orbit_vel_az *= decay;
            orbit_vel_el *= decay;
        }
    }

    // Update focus_target with a body's current position (call each frame)
    void track_body(glm::vec3 body_pos) {
        if (focus_active &&
            std::isfinite(body_pos.x) && std::isfinite(body_pos.y) && std::isfinite(body_pos.z))
            focus_target = body_pos;
    }

    // Cast a ray from screen coordinates into world space
    // Returns a point on the camera target plane (for zoom-toward-cursor)
    glm::vec3 screen_to_world_on_target_plane(float screen_x, float screen_y,
                                                float screen_w, float screen_h) const {
        // Normalized device coords
        float ndc_x = (2.0f * screen_x / screen_w) - 1.0f;
        float ndc_y = 1.0f - (2.0f * screen_y / screen_h);

        float aspect = screen_w / screen_h;
        float half_h = std::tan(glm::radians(fov * 0.5f));
        float half_w = half_h * aspect;

        glm::vec3 eye = eye_position();
        glm::vec3 fwd = forward_direction();
        glm::vec3 r = right_direction();
        glm::vec3 u = up_direction();

        glm::vec3 ray_dir = glm::normalize(fwd + r * (ndc_x * half_w) + u * (ndc_y * half_h));

        // Intersect with the plane through target, facing camera
        glm::vec3 plane_normal = glm::normalize(target - eye);
        float denom = glm::dot(ray_dir, plane_normal);
        if (std::abs(denom) < 1e-6f) return target;

        float t = glm::dot(target - eye, plane_normal) / denom;
        if (t < 0.0f) return target;
        return eye + ray_dir * t;
    }
};
