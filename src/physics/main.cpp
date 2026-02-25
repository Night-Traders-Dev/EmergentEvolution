#include "physics/simulation.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <chrono>

static PhysicsSimulation* g_sim_resize = nullptr;

static void framebuffer_resize_callback(GLFWwindow*, int, int) {
    if (g_sim_resize)
        g_sim_resize->renderer.swapchain_dirty = true;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(REGION_W / 2),
        static_cast<int>(REGION_H / 2),
        "Particle Physics — Protons, Neutrons, Electrons",
        nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        std::cerr << "Failed to create GLFW window\n";
        return 1;
    }

    PhysicsSimulation sim;
    g_sim_resize = &sim;

    glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);
    PhysicsSim_RegisterScrollCallback(window, &sim);

    try {
        sim.init(window);
    } catch (const std::exception& e) {
        std::cerr << "Init error: " << e.what() << "\n";
        return 1;
    }

    using Clock = std::chrono::high_resolution_clock;
    auto last_time = Clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        if (dt > 0.1) dt = 0.1;

        try {
            sim.tick(window, dt);
        } catch (const std::exception& e) {
            std::cerr << "Tick error: " << e.what() << "\n";
            break;
        }
    }

    try {
        sim.destroy();
    } catch (const std::exception& e) {
        std::cerr << "Cleanup error: " << e.what() << "\n";
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
