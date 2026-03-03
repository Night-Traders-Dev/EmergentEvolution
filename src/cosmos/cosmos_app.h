#pragma once
// ── Cosmic Sandbox — Application ────────────────────────────────────────────

#include "cosmos/cosmos_types.h"
#include "common/simple_renderer.h"

struct GLFWwindow;

class CosmosApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();

    VulkanContext   vk;
    SimpleRenderer  renderer;
    CosmosConfig    cfg;
    CosmosState     state;
    bool            paused = false;

private:
    void render_ui();
    void step_physics(float dt);
};
