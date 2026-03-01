#pragma once

#include <cstdint>

class PhysicsSimulation;

struct Scenario {
    const char* name;
    const char* description;
    const char* category;      // "Nuclear", "Chemistry", "Cosmology", "Sandbox"
    void (*setup)(PhysicsSimulation& sim);
    bool (*check_goal)(const PhysicsSimulation& sim);  // null = sandbox (no goal)
    const char* goal_text;     // shown as HUD objective
    const char* hint_text;     // shown when stuck
};

class ScenarioManager {
public:
    bool active = false;
    int  scenario_idx = -1;
    bool goal_complete = false;

    void start_scenario(int idx, PhysicsSimulation& sim);
    void check_goal(const PhysicsSimulation& sim);
    void end();

    const Scenario& current() const;
    static int scenario_count();
    static const Scenario& get(int idx);
};
