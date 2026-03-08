#include "cosmos/cosmos_app_internal.h"
#include <cmath>

// ── Hawking radiation ────────────────────────────────────────────────────────

void CosmosApp::process_hawking_radiation(float dt) {
    auto& bodies = state.bodies;
    const float scale = std::max(cfg.hawking_radiation_scale, 0.0f);
    if (scale <= 0.0f) return;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (!is_black_hole_type(b.type)) continue;
        if (b.mass <= 0.0f) continue;

        float abs_dt = std::abs(dt);
        // dm/dt = ℏc⁴/(15360π G² M²) — loss rate ∝ 1/M²
        // Use Hawking temperature to set emission power: P = σ A T⁴
        // where A = 16π (GM/c²)² ∝ M², T ∝ 1/M → P ∝ 1/M²
        float dm = scale * 1.0e-12f / (b.mass * b.mass) * abs_dt;
        dm = std::min(dm, b.mass * 0.5f);

        b.mass -= dm;
        b.mass_loss_rate += dm / std::max(abs_dt, 1.0e-9f);
        b.mass_loss_total += dm;
        // Temperature rises as mass drops: T_H ∝ 1/M
        b.hawking_temperature = 6.17e-8f / std::max(b.mass, 1.0e-15f);
        b.temperature = std::max(b.temperature, b.hawking_temperature);

        if (b.type == CTYPE_BH_PRIMORDIAL && b.mass < 1.0e-10f) {
            b.marked_for_removal = true;
            b.temperature = 1.0e12f;
        }
    }
}
