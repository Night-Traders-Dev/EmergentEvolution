#pragma once

#include <cstdint>

// Forward declaration
class PhysicsSimulation;

struct TutorialStep {
    const char* title;
    const char* text;
    bool (*check_complete)(const PhysicsSimulation& sim);  // null = manual "Next" advance
};

class TutorialManager {
public:
    bool active = false;
    int  current_step = 0;

    void start();
    void advance();
    void skip();
    bool is_step_complete(const PhysicsSimulation& sim) const;
    const TutorialStep& current() const;
    int step_count() const;
};
