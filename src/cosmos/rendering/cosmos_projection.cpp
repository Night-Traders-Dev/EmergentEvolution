#include "cosmos/cosmos_app_internal.h"

// ── 3D Projection (for overlay) ─────────────────────────────────────────────

CosmosApp::Projected CosmosApp::project(const glm::vec3& world_pos,
                                         const glm::dmat4& vp,
                                         float screen_w, float screen_h) const {
    glm::dvec4 clip = vp * glm::dvec4(world_pos, 1.0);
    if (clip.w <= 0.0)
        return {0, 0, 0, false};

    glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    float sx = (float)((ndc.x * 0.5 + 0.5) * (double)screen_w);
    float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)screen_h);

    bool visible = (ndc.x >= -1.2f && ndc.x <= 1.2f &&
                    ndc.y >= -1.2f && ndc.y <= 1.2f &&
                    ndc.z >= 0.0f && ndc.z <= 1.0f);
    return {sx, sy, (float)clip.w, visible};
}

float CosmosApp::screen_radius(float world_radius, float depth,
                                float fov_rad, float screen_h) const {
    if (depth <= 0.0f) return 0.0f;
    return (world_radius / depth) * (screen_h / (2.0f * std::tan(fov_rad * 0.5f)));
}
