#pragma once

#include <cstdint>

class PhysicsSimulation;

struct ScenarioTask {
    const char* narrative;    // story text shown when task begins
    const char* goal_text;    // HUD objective text
    const char* hint_text;    // hint when stuck
    bool (*check)(const PhysicsSimulation& sim);
};

// ── Spawn restrictions per scenario ──────────────────────────────────────────

struct ScenarioSpawnRules {
    uint64_t allowed_base[2] = {~0ULL, ~0ULL};  // bits 0-127 (particle types), default all
    int max_element_Z = 118;       // max Z in periodic table (-1 = no elements)
    bool allow_molecules = true;
    bool allow_force_objects = true;
    bool allow_hadrons = true;

    bool is_type_allowed(uint32_t t) const {
        if (t >= 128) return true;  // mesons/reserved always allowed
        return (allowed_base[t / 64] >> (t % 64)) & 1ULL;
    }
};

struct Scenario {
    const char* name;
    const char* description;
    const char* category;      // "Nuclear", "Chemistry", "Cosmology", "Sandbox"
    const char* storyline;     // intro narrative shown on start
    void (*setup)(PhysicsSimulation& sim);
    const ScenarioTask* tasks;
    int task_count;            // 0 = sandbox (no goals)
    ScenarioSpawnRules spawn_rules;  // which particles the player may spawn
};

class ScenarioManager {
public:
    bool  active = false;
    int   scenario_idx = -1;
    bool  goal_complete = false;      // all tasks done
    int   current_task = 0;           // index into tasks array
    bool  task_just_completed = false; // set true on task complete, cleared on advance
    float task_complete_timer = 0.0f; // seconds since current task was completed
    float narrative_timer = 0.0f;     // seconds since narrative was shown (for fade)

    void start_scenario(int idx, PhysicsSimulation& sim);
    void check_goal(const PhysicsSimulation& sim);
    void end();

    const Scenario& current() const;
    const ScenarioTask* current_task_ptr() const;
    int total_tasks() const;
    static int scenario_count();
    static const Scenario& get(int idx);
};
