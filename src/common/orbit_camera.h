#pragma once
// ── OrbitCamera — shared 3D orbit camera ────────────────────────────────────

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

struct OrbitCamera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance  = 600.0f;
    float azimuth   = 0.0f;     // radians, horizontal rotation
    float elevation = 0.5f;     // radians, vertical angle
    float fov       = 45.0f;    // degrees
    float near_clip = 0.1f;
    float far_clip  = 10000.0f;

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
};
