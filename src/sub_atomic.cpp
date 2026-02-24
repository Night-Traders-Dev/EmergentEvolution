#include "sub_atomic.h"
#include "imgui.h"
#include <random>
#include <algorithm>
#include <cstring>
#include <cstdio>

// ── Static tables ─────────────────────────────────────────────────────────────

const SubAtomicSim::NucleonComp SubAtomicSim::NUCLEUS[ATOM_COUNT] = {
    { 1,  0},  // H
    { 6,  6},  // C
    { 7,  7},  // N
    { 8,  8},  // O
    {15, 16},  // P
    {16, 16},  // S
    {11, 12},  // Na
    {17, 18},  // Cl
    // ── R-process / supernova elements ────────────────────────────────────────
    {26, 30},  // Fe  — Iron-56     (visual cap applied in init_from_atom)
    {28, 30},  // Ni  — Nickel-58
    {14, 14},  // Si  — Silicon-28
    {20, 20},  // Ca  — Calcium-40
    {22, 26},  // Ti  — Titanium-48
    {38, 50},  // Sr  — Strontium-88
    {79,118},  // Au  — Gold-197    (visual cap applied)
    {82,125},  // Pb  — Lead-207    (visual cap applied)
    {63, 89},  // Eu  — Europium-152 (visual cap applied)
    {92,146},  // U   — Uranium-238  (visual cap applied)
};

// Electrons per shell (up to 4 shells), real/simplified electron configurations
const int SubAtomicSim::SHELLS[ATOM_COUNT][4] = {
    {1, 0, 0, 0},  // H:  1
    {2, 4, 0, 0},  // C:  2,4
    {2, 5, 0, 0},  // N:  2,5
    {2, 6, 0, 0},  // O:  2,6
    {2, 8, 5, 0},  // P:  2,8,5
    {2, 8, 6, 0},  // S:  2,8,6
    {2, 8, 1, 0},  // Na: 2,8,1
    {2, 8, 7, 0},  // Cl: 2,8,7
    // ── R-process / supernova elements (4-shell simplified, visually capped) ──
    {2, 8,14, 2},  // Fe: 2,8,14,2  (26e)
    {2, 8,16, 2},  // Ni: 2,8,16,2  (28e)
    {2, 8, 4, 0},  // Si: 2,8,4     (14e)
    {2, 8, 8, 2},  // Ca: 2,8,8,2   (20e)
    {2, 8,10, 2},  // Ti: 2,8,10,2  (22e)
    {2, 8,18,10},  // Sr: 2,8,18,10 (38e)
    {2, 8,18, 1},  // Au: 2,8,18,1  (outer shells only; 6s1 visible, inner implied)
    {2, 8,18, 4},  // Pb: 2,8,18,4  (6s2 6p2)
    {2, 8,15, 2},  // Eu: 2,8,15,2  (4f7 6s2 simplified)
    {2, 8,18, 6},  // U:  2,8,18,6  (5f3 6d1 7s2 simplified)
};

// Electron shell radii (px) — visual Bohr model
const float SubAtomicSim::SHELL_R[4] = { 58.f, 100.f, 142.f, 184.f };

// ── Physics constants (panel-space, arbitrary visual units) ──────────────────
static constexpr float K_ELEC       = 500000.f;  // electron–nucleus Coulomb
static constexpr float K_EE         = 200000.f;  // electron–electron Coulomb
static constexpr float K_NUC        = 60000.f;   // proton–proton Coulomb
static constexpr float YUKAWA_G2    = 250000.f;  // Yukawa coupling²  (nuclear)
static constexpr float YUKAWA_L     = 9.f;       // Yukawa range px
static constexpr float HARD_CORE_R  = 10.f;      // nucleon hard-core exclusion radius
static constexpr float HARD_CORE_K  = 20000.f;   // hard-core repulsion stiffness
static constexpr float CORNELL_ALPHA= 800.f;     // QCD-like short-range repulsion
static constexpr float CORNELL_SIGMA= 4.0f;      // QCD-like linear confinement
static constexpr float QUARK_HC_R   = 3.f;       // quark hard-core radius
static constexpr float QUARK_HC_K   = 400.f;

// ── reset ─────────────────────────────────────────────────────────────────────

void SubAtomicSim::reset() {
    active                 = false;
    zoom_level             = 0;
    atom_type              = -1;
    focused_nuc            = 0;
    settle_frames_         = 0;
    current_binding_energy = 0.f;
    nuclear_stability      = 1.f;
    nucleons.clear();
    electrons.clear();
    quarks.clear();
    electron_trails.clear();
}

// ── Force primitives ──────────────────────────────────────────────────────────

// Yukawa nuclear: always attractive.
// V(r) = -g² exp(-r/λ) / r  →  F = g² exp(-r/λ) (1/r + 1/λ) / r
glm::vec2 SubAtomicSim::yukawa(glm::vec2 r_vec, float g2, float lam) const {
    float d   = glm::length(r_vec) + 0.01f;
    float mag = g2 * std::exp(-d / lam) * (1.f/d + 1.f/lam) / d;
    return -(r_vec / d) * mag;  // negative = pulls toward each other
}

// Coulomb: sign of q1q2 determines attraction/repulsion.
// F = k q1q2 / r²  along r_vec (positive = repulsive, negative = attractive)
glm::vec2 SubAtomicSim::coulomb(glm::vec2 r_vec, float q1q2, float k) const {
    float d   = glm::length(r_vec) + 0.01f;
    float mag = k * q1q2 / (d * d);
    return (r_vec / d) * mag;
}

// Cornell (QCD-inspired): V(r) = -α/r + σr  →  F = α/r² - σ
// Repulsive at short r (asymptotic freedom), confining at long r.
glm::vec2 SubAtomicSim::cornell(glm::vec2 r_vec, float alpha, float sigma) const {
    float d   = glm::length(r_vec) + 0.01f;
    float mag = alpha / (d * d) - sigma;   // >0 = repulsive, <0 = confining
    return (r_vec / d) * mag;
}

// ── init_from_atom ────────────────────────────────────────────────────────────

void SubAtomicSim::init_from_atom(int atype) {
    if (atype < 0 || atype >= static_cast<int>(ATOM_COUNT)) return;
    nucleons.clear();
    electrons.clear();
    quarks.clear();
    electron_trails.clear();
    settle_frames_ = 0;

    static std::mt19937 rng{ 42 };  // fixed seed for reproducibility
    std::uniform_real_distribution<float> jit(-1.5f, 1.5f);

    const auto& nc = NUCLEUS[atype];
    int A = nc.p + nc.n;

    // Cap displayed nucleons for performance (O(n²) physics); heavy elements
    // are visually approximated with a proportional scaled-down nucleus.
    static constexpr int MAX_NUCLEONS_DISPLAY = 56;
    int A_display = std::min(A, MAX_NUCLEONS_DISPLAY);
    int p_display = (A <= MAX_NUCLEONS_DISPLAY) ? nc.p
                  : std::max(1, static_cast<int>(nc.p * float(MAX_NUCLEONS_DISPLAY) / float(A)));
    int n_display = A_display - p_display;

    // Fibonacci spiral placement within nuclear radius
    // r_nuc = 10 * A^(1/3) gives ~10px inter-nucleon spacing at equilibrium
    float r_nuc = (A_display <= 1) ? 0.f : 10.f * std::pow(float(A_display), 1.f/3.f);
    const float golden_angle = 2.399963f;  // 2π/φ²

    int p_placed = 0, n_placed = 0;
    for (int i = 0; i < A_display; ++i) {
        float radius = (A_display <= 1) ? 0.f
                                : r_nuc * std::sqrt(float(i + 0.5f) / float(A_display));
        float theta  = i * golden_angle;
        glm::vec2 pos = { radius * std::cos(theta) + jit(rng),
                          radius * std::sin(theta) + jit(rng) };

        bool is_proton = (p_placed < p_display) &&
                         (n_placed >= n_display || (i % 2 == 0));
        if (is_proton) ++p_placed; else ++n_placed;

        SubParticle sp;
        sp.pos         = pos;
        sp.vel         = { 0.f, 0.f };
        sp.type        = is_proton ? SubPType::Proton : SubPType::Neutron;
        sp.nucleon_idx = -1;
        sp.charge      = is_proton ? 1.f : 0.f;
        nucleons.push_back(sp);
    }

    // Place electrons in real shell configurations with circular orbital velocities
    for (int sh = 0; sh < 4; ++sh) {
        int n_e = SHELLS[atype][sh];
        if (n_e == 0) break;

        float r_sh  = SHELL_R[sh];
        // v = sqrt(K_ELEC * Z / r) for stable circular orbit
        float v_orb = std::sqrt(K_ELEC * float(nc.p) / r_sh);
        // Clamp to visually interesting range
        v_orb = std::clamp(v_orb, 50.f, 400.f);

        for (int ei = 0; ei < n_e; ++ei) {
            float theta = float(ei) / float(n_e) * 6.28318f + sh * 0.4f;
            glm::vec2 pos  = { r_sh * std::cos(theta), r_sh * std::sin(theta) };
            glm::vec2 tang = { -std::sin(theta),        std::cos(theta) };

            SubParticle sp;
            sp.pos         = pos;
            sp.vel         = tang * v_orb;
            sp.type        = SubPType::Electron;
            sp.nucleon_idx = -1;
            sp.charge      = -1.f;
            electrons.push_back(sp);
        }
    }

    electron_trails.resize(electrons.size());
}

// ── init_quark_view ───────────────────────────────────────────────────────────

void SubAtomicSim::init_quark_view(int nuc_idx) {
    quarks.clear();
    if (nuc_idx < 0 || nuc_idx >= static_cast<int>(nucleons.size())) return;

    const SubParticle& nuc = nucleons[nuc_idx];
    bool is_proton = (nuc.type == SubPType::Proton);
    // Proton = uud (2 up + 1 down), Neutron = udd (1 up + 2 down)

    // Place quarks at Cornell equilibrium: r = sqrt(α/σ) ≈ 14px from pair centroid
    // In a triangle, if vertices are at r from centroid, pair distance = r*sqrt(3)
    // We want pair distance ≈ 14px → r ≈ 8px from centroid
    const float qr = 8.f;

    for (int qi = 0; qi < 3; ++qi) {
        float theta = qi * 2.094395f;  // 2π/3

        SubParticle q;
        q.pos         = nuc.pos + glm::vec2{ qr * std::cos(theta),
                                              qr * std::sin(theta) };
        q.vel         = { 0.f, 0.f };
        q.nucleon_idx = nuc_idx;

        bool is_up = is_proton ? (qi < 2) : (qi == 0);  // uud vs udd
        q.type   = is_up ? SubPType::QuarkUp : SubPType::QuarkDown;
        q.charge = is_up ? (2.f / 3.f) : (-1.f / 3.f);
        quarks.push_back(q);
    }
}

// ── step_nucleon ──────────────────────────────────────────────────────────────

void SubAtomicSim::step_nucleon(float dt) {
    const int N = static_cast<int>(nucleons.size());
    if (N == 0) {
        current_binding_energy = 0.f;
        nuclear_stability      = 1.f;
        return;
    }

    const float damp = (settle_frames_ < 90) ? 0.75f : 0.97f;

    std::vector<glm::vec2> forces(static_cast<size_t>(N), { 0.f, 0.f });
    float binding_energy_acc = 0.f;  // accumulated per-frame potential energy

    for (int sub = 0; sub < 4; ++sub) {
        float sdt = dt * 0.25f;

        // Reset per sub-step
        for (auto& f : forces) f = { 0.f, 0.f };

        for (int i = 0; i < N; ++i) {
            for (int j = i + 1; j < N; ++j) {
                glm::vec2 r = nucleons[i].pos - nucleons[j].pos;
                float     d = glm::length(r) + 0.001f;

                // Yukawa: attracts ALL nucleon pairs (strong nuclear force)
                glm::vec2 f = yukawa(r, YUKAWA_G2, YUKAWA_L);

                // Coulomb: repels proton–proton only
                bool both_protons = (nucleons[i].charge > 0.f && nucleons[j].charge > 0.f);
                if (both_protons)
                    f += coulomb(r, 1.f, K_NUC);

                // Hard-core Pauli exclusion
                if (d < HARD_CORE_R)
                    f += (r / d) * (HARD_CORE_R - d) * HARD_CORE_K;

                forces[i] += f;
                forces[j] -= f;

                // Accumulate binding energy potential on last sub-step
                if (sub == 3) {
                    float V_yukawa  = -YUKAWA_G2 * std::exp(-d / YUKAWA_L) / d;
                    float V_coulomb = both_protons ? K_NUC / d : 0.f;
                    binding_energy_acc += V_yukawa + V_coulomb;
                }
            }
        }

        for (int i = 0; i < N; ++i) {
            nucleons[i].vel += forces[i] * sdt;
            nucleons[i].pos += nucleons[i].vel * sdt;
        }
    }

    // Apply damping once per frame (framerate-independent approximation)
    for (auto& nuc : nucleons)
        nuc.vel *= damp;

    // Update binding energy and nuclear stability for LOD coupling
    current_binding_energy = binding_energy_acc;

    // Expected binding energy: liquid-drop reference — scales with A*V_typical
    float A        = float(N);
    float expected = -YUKAWA_G2 * A * 0.8f / YUKAWA_L;  // typical saturation energy
    if (expected < -1.f) {
        nuclear_stability = std::clamp(binding_energy_acc / expected, 0.f, 1.f);
    } else {
        nuclear_stability = 1.f;
    }
}

// ── step_electrons ────────────────────────────────────────────────────────────

void SubAtomicSim::step_electrons(float dt) {
    const int NE = static_cast<int>(electrons.size());
    const int NP = static_cast<int>(nucleons.size());
    if (NE == 0) return;

    static std::mt19937           rng{ std::random_device{}() };
    static std::normal_distribution<float> noise(0.f, 1.f);

    for (int sub = 0; sub < 4; ++sub) {
        float sdt = dt * 0.25f;

        for (int i = 0; i < NE; ++i) {
            glm::vec2 f = { 0.f, 0.f };

            // Attraction to each proton: q1q2 = (-1)(+1) = -1 → attractive
            for (int j = 0; j < NP; ++j) {
                if (nucleons[j].charge <= 0.f) continue;
                glm::vec2 r = electrons[i].pos - nucleons[j].pos;
                f += coulomb(r, -1.f, K_ELEC);
            }

            // Repulsion from other electrons: q1q2 = (-1)(-1) = +1 → repulsive
            for (int j = 0; j < NE; ++j) {
                if (j == i) continue;
                glm::vec2 r = electrons[i].pos - electrons[j].pos;
                f += coulomb(r, 1.f, K_EE);
            }

            // Thermal kick proportional to atom energy (visible excitation)
            float thermal = atom_energy_ * 60.f;
            f += glm::vec2{ noise(rng) * thermal, noise(rng) * thermal };

            electrons[i].vel += f * sdt;
            electrons[i].pos += electrons[i].vel * sdt;
        }
    }

    // Light damping — preserve orbital motion
    for (auto& el : electrons)
        el.vel *= 0.995f;

    // Update trails
    for (int i = 0; i < NE; ++i) {
        auto& trail = electron_trails[static_cast<size_t>(i)];
        trail.push_back(electrons[i].pos);
        if (trail.size() > 22) trail.pop_front();
    }
}

// ── step_quarks ───────────────────────────────────────────────────────────────

void SubAtomicSim::step_quarks(float dt) {
    const int NQ = static_cast<int>(quarks.size());
    if (NQ < 3) return;

    glm::vec2 nuc_pos = { 0.f, 0.f };
    if (focused_nuc >= 0 && focused_nuc < static_cast<int>(nucleons.size()))
        nuc_pos = nucleons[static_cast<size_t>(focused_nuc)].pos;

    glm::vec2 forces[3] = {};

    for (int sub = 0; sub < 8; ++sub) {
        float sdt = dt / 8.f;

        for (auto& ff : forces) ff = { 0.f, 0.f };

        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                glm::vec2 r = quarks[i].pos - quarks[j].pos;
                float     d = glm::length(r) + 0.001f;

                glm::vec2 f = cornell(r, CORNELL_ALPHA, CORNELL_SIGMA);

                // Hard-core (minimum separation)
                if (d < QUARK_HC_R)
                    f += (r / d) * (QUARK_HC_R - d) * QUARK_HC_K;

                forces[i] += f;
                forces[j] -= f;
            }
        }

        // Restoring: keep quark centroid anchored to nucleon position
        glm::vec2 centroid = { 0.f, 0.f };
        for (int i = 0; i < 3; ++i) centroid += quarks[i].pos;
        centroid /= 3.f;
        glm::vec2 restore = (nuc_pos - centroid) * 6.f;
        for (int i = 0; i < 3; ++i) forces[i] += restore;

        for (int i = 0; i < 3; ++i) {
            quarks[i].vel += forces[i] * sdt;
            quarks[i].pos += quarks[i].vel * sdt;
        }
    }

    // Moderate damping for quarks
    for (auto& q : quarks)
        q.vel *= 0.98f;
}

// ── update ────────────────────────────────────────────────────────────────────

void SubAtomicSim::update(double dt, int hovered_atom_type, float atom_energy,
                           int new_zoom) {
    atom_energy_ = atom_energy;

    bool should_show = (hovered_atom_type >= 0 &&
                        hovered_atom_type < static_cast<int>(ATOM_COUNT) &&
                        new_zoom > 0);

    if (!should_show) {
        active     = false;
        zoom_level = 0;
        return;
    }

    // Reinitialise whenever the atom type changes
    if (hovered_atom_type != atom_type) {
        atom_type = hovered_atom_type;
        init_from_atom(atom_type);
    }

    // Handle zoom level transitions
    int old_zoom = zoom_level;
    zoom_level   = new_zoom;

    if (zoom_level == 2 && old_zoom < 2) {
        init_quark_view(focused_nuc);
    } else if (zoom_level < 2) {
        quarks.clear();
    }

    active = true;
    ++settle_frames_;

    float fdt = static_cast<float>(std::min(dt, 0.05));
    step_nucleon(fdt);
    step_electrons(fdt);
    if (zoom_level == 2 && !quarks.empty())
        step_quarks(fdt);
}

// ── render_panel ──────────────────────────────────────────────────────────────

static const char* ATOM_NAMES[ATOM_COUNT]   = {
    "Hydrogen","Carbon","Nitrogen","Oxygen",
    "Phosphorus","Sulfur","Sodium","Chlorine",
    "Iron","Nickel","Silicon","Calcium",
    "Titanium","Strontium","Gold","Lead",
    "Europium","Uranium"
};
static const char* ATOM_SYMBOLS[ATOM_COUNT] = {
    "H","C","N","O","P","S","Na","Cl",
    "Fe","Ni","Si","Ca","Ti","Sr","Au","Pb","Eu","U"
};

void SubAtomicSim::render_panel(int hover_particle_idx, const Particles& particles) {
    if (!active || atom_type < 0 || atom_type >= static_cast<int>(ATOM_COUNT)) return;
    if (hover_particle_idx < 0) return;

    // Verify hovered particle type still matches (guard against stale state)
    {
        auto pidx = static_cast<uint32_t>(hover_particle_idx);
        if (pidx >= static_cast<uint32_t>(particles.types.size())) return;
        int ptype = static_cast<int>(particles.types[pidx]);
        if (ptype != atom_type) return;
    }

    const auto& nc = NUCLEUS[static_cast<size_t>(atom_type)];
    int A = nc.p + nc.n;
    int Z = nc.p;

    char title[80];
    std::snprintf(title, sizeof(title), "%s (%s)  Z=%d  A=%d##sub",
                  ATOM_NAMES[atom_type], ATOM_SYMBOLS[atom_type], Z, A);

    ImGui::SetNextWindowPos(ImVec2(490.f, 80.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(480.f, 520.f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin(title, nullptr, flags)) { ImGui::End(); return; }

    ImDrawList* dl       = ImGui::GetWindowDrawList();
    ImVec2      canvas_tl = ImGui::GetCursorScreenPos();

    const float canvas_w = 460.f;
    const float canvas_h = 400.f;
    ImGui::InvisibleButton("canvas##sub", ImVec2(canvas_w, canvas_h));

    // Panel centre in screen space — physics coordinates are relative to (0,0)
    ImVec2 ctr = { canvas_tl.x + canvas_w * 0.5f,
                   canvas_tl.y + canvas_h * 0.5f };

    // Nucleon render radius scaled by mass number
    float nuc_r = (A <= 4) ? 9.f : (A <= 12) ? 8.f : (A <= 20) ? 7.f : 6.f;
    bool  show_labels = (A <= 20);

    // ── Zoom Level 1: Nucleon + Electron view ─────────────────────────────────
    if (zoom_level == 1) {

        // Faint shell guide circles
        for (int sh = 0; sh < 4; ++sh) {
            if (SHELLS[atom_type][sh] == 0) break;
            dl->AddCircle(ctr, SHELL_R[sh],
                          IM_COL32(55, 75, 120, 55), 64, 1.f);
        }

        // Electron trails
        for (size_t ei = 0; ei < electron_trails.size(); ++ei) {
            const auto& trail = electron_trails[ei];
            int tn = static_cast<int>(trail.size());
            for (int ti = 1; ti < tn; ++ti) {
                float alpha = float(ti) / float(tn) * 0.30f;
                ImU32  tc   = IM_COL32(70, 160, 255, int(alpha * 255));
                ImVec2 p0   = { ctr.x + trail[size_t(ti-1)].x,
                                ctr.y + trail[size_t(ti-1)].y };
                ImVec2 p1   = { ctr.x + trail[size_t(ti)].x,
                                ctr.y + trail[size_t(ti)].y };
                dl->AddLine(p0, p1, tc, 1.5f);
            }
        }

        // Electrons
        for (const auto& el : electrons) {
            ImVec2 p = { ctr.x + el.pos.x, ctr.y + el.pos.y };
            dl->AddCircleFilled(p, 4.f,  IM_COL32(80, 180, 255, 230));
            dl->AddCircle      (p, 4.5f, IM_COL32(160, 220, 255, 130), 12, 1.f);
        }

        // Nucleons (draw after electrons so they appear on top)
        for (const auto& nuc : nucleons) {
            ImVec2 p = { ctr.x + nuc.pos.x, ctr.y + nuc.pos.y };
            if (nuc.type == SubPType::Proton) {
                dl->AddCircleFilled(p, nuc_r, IM_COL32(220, 65, 65, 245));
                dl->AddCircle      (p, nuc_r, IM_COL32(255, 120, 120, 160), 16, 1.f);
                if (show_labels)
                    dl->AddText(ImVec2(p.x - 3.f, p.y - 6.f),
                                IM_COL32(255, 255, 255, 220), "p");
            } else {
                dl->AddCircleFilled(p, nuc_r, IM_COL32(155, 155, 155, 245));
                dl->AddCircle      (p, nuc_r, IM_COL32(210, 210, 210, 140), 16, 1.f);
                if (show_labels)
                    dl->AddText(ImVec2(p.x - 3.f, p.y - 6.f),
                                IM_COL32(40, 40, 40, 220), "n");
            }
        }

        ImGui::TextDisabled("Nucleon view  |  Zoom > 150x for quark structure");

    // ── Zoom Level 2: Quark view ──────────────────────────────────────────────
    } else if (zoom_level == 2) {

        bool is_proton = (focused_nuc >= 0 &&
                          focused_nuc < static_cast<int>(nucleons.size()))
                       ? (nucleons[static_cast<size_t>(focused_nuc)].type == SubPType::Proton)
                       : true;

        const char* nuc_label = is_proton ? "Proton  (u u d)" : "Neutron  (u d d)";

        // Nucleon boundary circle
        dl->AddCircle(ctr, 52.f, IM_COL32(90, 90, 200, 70), 64, 1.5f);

        // Gluon field lines between quarks
        if (quarks.size() >= 3) {
            glm::vec2 nuc_p = (focused_nuc >= 0 &&
                               focused_nuc < static_cast<int>(nucleons.size()))
                            ? nucleons[static_cast<size_t>(focused_nuc)].pos
                            : glm::vec2{ 0.f, 0.f };

            const float QSCALE = 4.5f;  // zoom quarks for display

            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                glm::vec2 di = (quarks[size_t(i)].pos - nuc_p) * QSCALE;
                glm::vec2 dj = (quarks[size_t(j)].pos - nuc_p) * QSCALE;
                ImVec2 pi = { ctr.x + di.x, ctr.y + di.y };
                ImVec2 pj = { ctr.x + dj.x, ctr.y + dj.y };
                dl->AddLine(pi, pj, IM_COL32(80, 200, 80, 55), 2.f);
            }

            // Quarks
            for (const auto& q : quarks) {
                glm::vec2 disp = (q.pos - nuc_p) * QSCALE;
                ImVec2 p = { ctr.x + disp.x, ctr.y + disp.y };
                bool   up = (q.type == SubPType::QuarkUp);
                ImU32  qc = up ? IM_COL32(255, 155, 50, 245)
                               : IM_COL32(70, 200, 70, 245);
                dl->AddCircleFilled(p, 13.f, qc);
                dl->AddCircle      (p, 13.f, IM_COL32(255, 255, 255, 100), 20, 1.5f);

                // Label: "u" / "d" and fractional charge
                const char* ql  = up ? "u" : "d";
                const char* qch = up ? "+\xE2\x85\x94" : "-\xE2\x85\x93";  // UTF-8 ⅔ ⅓
                dl->AddText(ImVec2(p.x - 4.f, p.y - 12.f),
                            IM_COL32(255, 255, 255, 230), ql);
                dl->AddText(ImVec2(p.x - 8.f, p.y + 1.f),
                            IM_COL32(220, 220, 220, 180), qch);
            }
        }

        // Cornell confinement visualisation (radial rings)
        for (int ring = 1; ring <= 4; ++ring) {
            dl->AddCircle(ctr, float(ring) * 13.f,
                          IM_COL32(70, 50, 160, 22 - ring * 4), 32,
                          float(ring) * 0.4f);
        }

        // Title text at top of canvas
        ImVec2 lsz = ImGui::CalcTextSize(nuc_label);
        dl->AddText(ImVec2(ctr.x - lsz.x * 0.5f, canvas_tl.y + 4.f),
                    IM_COL32(180, 180, 255, 210), nuc_label);

        ImGui::TextDisabled("Quark view  |  Cornell V(r) = -\xCE\xB1/r + \xCF\x83r");
    }

    // ── Legend / stats ────────────────────────────────────────────────────────
    ImGui::Separator();
    if (zoom_level == 1) {
        // Build shell config string
        char shell_str[32] = {};
        int  off = 0;
        for (int sh = 0; sh < 4; ++sh) {
            if (SHELLS[atom_type][sh] == 0) break;
            if (sh > 0) shell_str[off++] = ',';
            off += std::snprintf(shell_str + off,
                                 sizeof(shell_str) - static_cast<size_t>(off),
                                 "%d", SHELLS[atom_type][sh]);
        }
        ImGui::Text("%dp  %dn  %de  |  Shells: %s", nc.p, nc.n,
                    static_cast<int>(electrons.size()), shell_str);
        // Nuclear stability indicator (LOD coupling)
        float stab = nuclear_stability;
        ImGui::SameLine();
        ImGui::TextColored(
            stab > 0.7f ? ImVec4(0.4f,1.f,0.4f,1.f) :
            stab > 0.4f ? ImVec4(1.f,0.85f,0.f,1.f) :
                          ImVec4(1.f,0.3f,0.2f,1.f),
            "  Stability %.0f%%", stab * 100.f);
    } else if (zoom_level == 2) {
        bool is_p = (focused_nuc >= 0 &&
                     focused_nuc < static_cast<int>(nucleons.size()) &&
                     nucleons[static_cast<size_t>(focused_nuc)].type == SubPType::Proton);
        ImGui::Text("Charge: %s  |  \xCE\xB1=%.0f  \xCF\x83=%.1f  |  Cornell potential",
                    is_p ? "+1 (uud)" : " 0 (udd)",
                    CORNELL_ALPHA, CORNELL_SIGMA);
    }

    ImGui::End();
}
