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

    // Smooth zoom
    float target_distance = 600.0f;
    float zoom_speed      = 6.0f;

    glm::vec3 eye_position() const {
        float cos_el = std::cos(elevation);
        return target + glm::vec3(
            distance * cos_el * std::sin(azimuth),
            distance * std::sin(elevation),
            distance * cos_el * std::cos(azimuth)
        );
    }

    glm::mat4 view_matrix() const {
        return glm::lookAt(eye_position(), target, glm::vec3(0, 1, 0));
    }

    glm::mat4 proj_matrix(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, near_clip, far_clip);
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
        azimuth   -= dx * sensitivity;
        elevation += dy * sensitivity;
        elevation = std::clamp(elevation, -1.5f, 1.5f);
    }

    // Pan: shift target in the screen plane
    void pan(float dx, float dy) {
        float scale = distance * 0.001f;
        glm::vec3 r = right_direction();
        glm::vec3 u = up_direction();
        target += r * (-dx * scale) + u * (dy * scale);
        // Panning breaks focus
        if (focus_active) {
            focus_active = false;
            focus_body = -1;
        }
    }

    // Smooth zoom toward/away from target
    void zoom(float delta) {
        float factor = (delta > 0) ? 0.9f : 1.1f;
        target_distance *= factor;
        target_distance = std::clamp(target_distance, 5.0f, 8000.0f);
    }

    // Focus on a position: smooth camera pan to body
    void focus_on(glm::vec3 pos, int body_idx = -1) {
        focus_active = true;
        focus_body = body_idx;
        focus_target = pos;
    }

    // Stop following a body but keep the camera where it is
    void release_focus() {
        focus_active = false;
        focus_body = -1;
    }

    // Call every frame to animate smooth zoom + focus tracking
    void update(float dt) {
        // Smooth zoom
        distance += (target_distance - distance) * std::min(1.0f, zoom_speed * dt);

        // Focus tracking: smoothly move target toward focus_target
        if (focus_active) {
            float t = std::min(1.0f, focus_lerp_speed * dt);
            target = glm::mix(target, focus_target, t);
        }
    }

    // Update focus_target with a body's current position (call each frame)
    void track_body(glm::vec3 body_pos) {
        if (focus_active)
            focus_target = body_pos;
    }
};
