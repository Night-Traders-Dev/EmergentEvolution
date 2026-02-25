#include "physics/simulation.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <omp.h>

static PhysicsSimulation* g_sim_resize = nullptr;

static void framebuffer_resize_callback(GLFWwindow*, int, int) {
    if (g_sim_resize)
        g_sim_resize->renderer.swapchain_dirty = true;
}

int main() {
    omp_set_num_threads(std::min(omp_get_max_threads(), 8));

    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    GLFWwindow* window = glfwCreateWindow(
        mode->width, mode->height,
        "Particle Playground",
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
