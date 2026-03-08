#include "cosmos/cosmos_app_internal.h"
#include <cmath>

// ── Evaporation ─────────────────────────────────────────────────────────────

void CosmosApp::process_evaporation(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;

        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float vapor_threshold = 650.0f;
            if (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                b.cached_props.planet_class == PCLASS_ICE_GIANT) {
                vapor_threshold = 900.0f;
            }

            if (b.temperature > vapor_threshold) {
                float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
                float escape = body_escape_velocity(b, cfg.G);
                float escape_resist = 0.18f + escape * 0.42f;
                float class_scale = (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                                     b.cached_props.planet_class == PCLASS_ICE_GIANT) ? 0.45f : 1.0f;
                float loss = cfg.evaporation_rate * heat_factor * dt * 0.060f * class_scale /
                             std::max(escape_resist, 0.12f);
                loss = std::min(loss, b.mass * 0.003f); // max 0.3% per step (was 3%)
                if (loss > 0.0f) {
                    float pre_loss_mass = b.mass;
                    b.mass -= loss;
                    b.radius = std::max(b.radius * 0.99985f, 0.08f);
                    b.atmosphere_retention = std::max(0.0f, b.atmosphere_retention - loss / std::max(pre_loss_mass, 1.0e-12f));
                    b.props_valid = false;
                    b.visuals_valid = false;
                    register_mass_loss(b, loss, dt);
                }
            }
            continue;
        }

        if (b.type != CTYPE_ASTEROID && b.type != CTYPE_COMET &&
            b.type != CTYPE_NEBULA && b.type != CTYPE_DUST) continue;

        float vapor_threshold = (b.type == CTYPE_DUST) ? 520.0f :
                                ((b.type == CTYPE_COMET) ? 220.0f : 700.0f);
        if (b.temperature <= vapor_threshold) continue;

        float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
        float loss = cfg.evaporation_rate * heat_factor * dt *
            (b.type == CTYPE_NEBULA ? 0.12f : (b.type == CTYPE_COMET ? 0.07f : (b.type == CTYPE_DUST ? 0.018f : 0.05f)));
        loss = std::min(loss, b.mass * (b.type == CTYPE_DUST ? 0.005f : 0.01f));
        if (loss <= 0.0f) continue;

        b.mass -= loss;
        b.radius = std::max(b.radius * (b.type == CTYPE_DUST ? 0.99985f : 0.9996f),
                            b.type == CTYPE_DUST ? 0.06f : 0.05f);
        register_mass_loss(b, loss, dt);

        if (b.mass < (b.type == CTYPE_DUST ? 8.0e-12f : 5.0e-11f)) {
            b.marked_for_removal = true;
        }
    }
}
