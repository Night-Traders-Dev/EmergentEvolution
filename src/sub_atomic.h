#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include <glm/glm.hpp>
#include "types.h"
#include "particles.h"

enum class SubPType : uint8_t {
    Proton = 0, Neutron, Electron, QuarkUp, QuarkDown
};

struct SubParticle {
    glm::vec2 pos;          // panel-space coordinates (px from panel center)
    glm::vec2 vel;          // px/s
    SubPType  type;
    int       nucleon_idx;  // index into nucleons[] for quarks; -1 otherwise
    float     charge;       // +1 proton, 0 neutron, -1 electron, +2/3 up, -1/3 down
};

class SubAtomicSim {
public:
    bool active      = false;
    int  zoom_level  = 0;   // 0=hidden, 1=nucleon view, 2=quark view
    int  atom_type   = -1;  // currently shown atom type (0=H..7=Cl), -1=none
    int  focused_nuc = 0;   // which nucleon index to zoom into for quark view

    std::vector<SubParticle>           nucleons;   // protons + neutrons
    std::vector<SubParticle>           electrons;
    std::vector<SubParticle>           quarks;     // active only when zoom_level==2

    // Electron trail ring buffers (one deque per electron)
    std::vector<std::deque<glm::vec2>> electron_trails;

    void reset();

    // Called each frame from Simulation::tick() — after hover detection
    void update(double dt, int hovered_atom_type, float atom_energy, int new_zoom);

    // Draw floating ImGui window — call AFTER iface.render_imgui()
    void render_panel(int hover_particle_idx, const Particles& particles);

private:
    float atom_energy_    = 0.5f;
    int   settle_frames_  = 0;   // counts up; nucleons use heavy damping until settled

    void init_from_atom(int atype);
    void init_quark_view(int nuc_idx);
    void step_nucleon(float dt);
    void step_electrons(float dt);
    void step_quarks(float dt);

    // Force primitives (all forces in panel-space px/s²)
    // yukawa: nuclear attractive; coulomb: EM; cornell: QCD confinement
    glm::vec2 yukawa (glm::vec2 r_vec, float g2, float lam)           const;
    glm::vec2 coulomb(glm::vec2 r_vec, float q1q2, float k)           const;
    glm::vec2 cornell(glm::vec2 r_vec, float alpha, float sigma)      const;

    struct NucleonComp { int p; int n; };
    static const NucleonComp NUCLEUS[ATOM_COUNT];
    static const int          SHELLS[ATOM_COUNT][4];
    static const float        SHELL_R[4];
};
