#include "decay_manager.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ── Decay table ────────────────────────────────────────────────────────────────
// Stable elements are default-constructed (unstable=false).
// Half-lives are in simulation-seconds; scaled so decay is visually interesting
// within a few minutes of simulation time.

const DecayParams DecayManager::DECAY_TABLE[MAX_PARTICLE_TYPES] = {
    // 0  H   — stable
    // 1  C   — stable
    // 2  N   — stable
    // 3  O   — stable
    // 4  P   — stable
    // 5  S   — stable
    // 6  Na  — stable
    // 7  Cl  — stable
    // 8  Fe  — stable (alpha-process endpoint region)
    // 9  Ni  — electron capture / beta+ → Fe + positron
    DecayParams{},       // 0 H
    DecayParams{},       // 1 C
    DecayParams{},       // 2 N
    DecayParams{},       // 3 O
    DecayParams{},       // 4 P
    DecayParams{},       // 5 S
    DecayParams{},       // 6 Na
    DecayParams{},       // 7 Cl
    DecayParams{},       // 8 Fe — stable
    { true, 50.f, 8u,  POSITRON_TYPE, 0.20f, false }, // 9  Ni → Fe + e+
    DecayParams{},       // 10 Si — stable
    DecayParams{},       // 11 Ca — stable
    DecayParams{},       // 12 Ti — stable
    { true, 40.f, 11u, ELECTRON_TYPE, 0.25f, true  }, // 13 Sr → Ca + e- + γ
    DecayParams{},       // 14 Au — stable (noble)
    DecayParams{},       // 15 Pb — stable (r-process endpoint)
    { true, 25.f,  8u, ELECTRON_TYPE, 0.30f, true  }, // 16 Eu → Fe + e- + γ
    { true, 30.f, 15u, ALPHA_TYPE,    0.45f, true  }, // 17 U  → Pb + α + γ
    DecayParams{},       // 18 photon — stable (drains energy on its own)
    DecayParams{},       // 19 alpha — stable as free particle
    DecayParams{},       // 20 e-   — stable
    DecayParams{},       // 21 e+   — handled separately (annihilation, not half-life)
    DecayParams{},       // 22 ν    — stable
    { true, 0.5f, ELECTRON_TYPE, NEUTRINO_TYPE, 0.15f, false }, // 23 μ → e- + ν
    DecayParams{},       // 24 reserved
    DecayParams{},       // 25 reserved
};

// ── reset ─────────────────────────────────────────────────────────────────────

void DecayManager::reset() {
    total_decays = 0;
    muon_ttl_.clear();
}

// ── random_dir ────────────────────────────────────────────────────────────────

glm::vec2 DecayManager::random_dir() {
    float angle = uniform_(rng_) * 6.28318f;
    return { std::cos(angle), std::sin(angle) };
}

// ── update ────────────────────────────────────────────────────────────────────

std::vector<DecayEvent> DecayManager::update(
        const std::vector<uint32_t>&  types,
        const std::vector<glm::vec2>& positions,
        const std::vector<float>&     energies,
        float dt,
        uint32_t count)
{
    (void)energies;  // reserved for energy-threshold decay in future
    std::vector<DecayEvent> events;
    events.reserve(8);

    count = std::min(count, static_cast<uint32_t>(types.size()));

    // Resize muon TTL tracker to current particle count
    if (muon_ttl_.size() < count)
        muon_ttl_.resize(count, 0.f);

    // ── Pass 1: standard probabilistic / TTL decay ────────────────────────────
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t t = types[i];
        if (t >= MAX_PARTICLE_TYPES) continue;

        const DecayParams& dp = DECAY_TABLE[t];
        if (!dp.unstable) {
            muon_ttl_[i] = 0.f;  // reset TTL for non-muon types
            continue;
        }

        // Muon uses accumulated TTL instead of probabilistic half-life
        if (t == MUON_TYPE) {
            muon_ttl_[i] += dt;
            float half_life = dp.half_life_sec;
            float lambda    = 0.693147f / half_life;
            float prob      = 1.f - std::exp(-lambda * dt);
            if (uniform_(rng_) >= prob) continue;
        } else {
            float lambda = 0.693147f / dp.half_life_sec;
            float prob   = 1.f - std::exp(-lambda * dt);
            if (uniform_(rng_) >= prob) continue;
        }

        glm::vec2 dir = random_dir();
        DecayEvent ev;
        ev.parent_idx    = i;
        ev.daughter_type = dp.daughter_type;
        ev.product_type  = dp.product_type;
        ev.parent_pos    = (i < positions.size()) ? positions[i] : glm::vec2{0.f,0.f};
        ev.product_vel   = dir * dp.q_energy * 220.f;
        ev.q_energy      = dp.q_energy;
        ev.emits_gamma   = dp.emits_gamma;
        events.push_back(ev);
    }

    // ── Pass 2: positron–electron annihilation ────────────────────────────────
    // When a positron is within 40px of a free electron, both annihilate into
    // two photons (represented as a decay event with no daughter type).
    // We use a simplified O(n×n_positrons) search; positrons are rare.
    constexpr float ANNIHILATION_RADIUS_SQ = 40.f * 40.f;

    for (uint32_t i = 0; i < count; ++i) {
        if (types[i] != POSITRON_TYPE) continue;
        glm::vec2 pi = (i < positions.size()) ? positions[i] : glm::vec2{0.f};

        for (uint32_t j = 0; j < count; ++j) {
            if (types[j] != ELECTRON_TYPE) continue;
            glm::vec2 pj = (j < positions.size()) ? positions[j] : glm::vec2{0.f};
            glm::vec2 d  = pi - pj;
            if (d.x*d.x + d.y*d.y > ANNIHILATION_RADIUS_SQ) continue;

            // Annihilate both → photons (photon injection handled in Simulation)
            // Positron → PHOTON_TYPE with outward velocity
            DecayEvent ev_pos;
            ev_pos.parent_idx    = i;
            ev_pos.daughter_type = PHOTON_TYPE;
            ev_pos.product_type  = 0xFFFFFFFFu;  // no extra product
            ev_pos.parent_pos    = pi;
            ev_pos.product_vel   = { 0.f, 0.f };
            ev_pos.q_energy      = 0.8f;
            ev_pos.emits_gamma   = false;  // the photon IS the daughter
            events.push_back(ev_pos);

            // Electron → PHOTON_TYPE going opposite direction
            DecayEvent ev_elec;
            ev_elec.parent_idx    = j;
            ev_elec.daughter_type = PHOTON_TYPE;
            ev_elec.product_type  = 0xFFFFFFFFu;
            ev_elec.parent_pos    = pj;
            ev_elec.product_vel   = { 0.f, 0.f };
            ev_elec.q_energy      = 0.8f;
            ev_elec.emits_gamma   = false;
            events.push_back(ev_elec);

            break;  // each positron annihilates with at most one electron per tick
        }
    }

    total_decays += static_cast<uint32_t>(events.size());
    return events;
}
