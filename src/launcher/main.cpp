#include "common/simple_renderer.h"
#include "physics/error_dialog.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// ── Subprocess launch ───────────────────────────────────────────────────────

static void launch_expansion(const char* exe_name) {
#ifdef _WIN32
    // Build path relative to launcher exe
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* sep = strrchr(path, '\\');
    if (sep) { sep[1] = '\0'; }
    std::string cmd = std::string(path) + exe_name + ".exe";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CreateProcessA(cmd.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    // Fork + exec sibling binary
    std::string cmd = std::string("./") + exe_name;
    pid_t pid = fork();
    if (pid == 0) {
        execlp(cmd.c_str(), exe_name, nullptr);
        _exit(127);
    }
#endif
}

// ── Expansion card data ─────────────────────────────────────────────────────

struct ExpansionCard {
    const char* title;
    const char* exe_name;
    const char* description;
    ImVec4      color;
};

static const ExpansionCard EXPANSIONS[] = {
    {
        "Particle Physics",
        "particle_physics",
        "Subatomic particle simulation with the Standard Model,\n"
        "nuclear reactions, covalent bonds, and 282 particle types.",
        {0.3f, 0.6f, 1.0f, 1.0f}
    },
    {
        "Cosmic Sandbox",
        "particle_cosmos",
        "Universe sandbox — stars, planets, moons, asteroids.\n"
        "Gravitational N-body, orbital mechanics, tidal forces.",
        {1.0f, 0.7f, 0.2f, 1.0f}
    },
    {
        "Biochemical Simulator",
        "particle_biochem",
        "Cells, bacteria, viruses, and molecular biology.\n"
        "Metabolism, replication, immune response, evolution.",
        {0.3f, 0.9f, 0.4f, 1.0f}
    },
};
static constexpr int EXPANSION_COUNT = sizeof(EXPANSIONS) / sizeof(EXPANSIONS[0]);

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
#ifdef _WIN32
    {
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) {
            char* last_sep = strrchr(exe_path, '\\');
            if (last_sep) { *last_sep = '\0'; SetCurrentDirectoryA(exe_path); }
        }
    }
#endif

    if (!glfwInit()) {
        show_error_dialog("Startup Error", "Failed to initialize GLFW.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(
        900, 600, "Particle Playground Launcher v" APP_VERSION, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        show_error_dialog("Startup Error", "Failed to create window.");
        return 1;
    }

    VulkanContext vk;
    try {
        vk.init(window);
    } catch (const std::exception& e) {
        show_error_dialog("Vulkan Error", e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    SimpleRenderer renderer;
    renderer.init(vk, window);

    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        auto* r = static_cast<SimpleRenderer*>(glfwGetWindowUserPointer(w));
        if (r) r->swapchain_dirty = true;
    });

    bool should_close = false;

    while (!glfwWindowShouldClose(window) && !should_close) {
        glfwPollEvents();

        if (!renderer.begin_frame(vk, window))
            continue;

        // ── Draw launcher UI ────────────────────────────────────────────────
        ImGuiIO& io = ImGui::GetIO();
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;

        // Centered title
        {
            ImGui::SetNextWindowPos({win_w * 0.5f, 40.0f}, ImGuiCond_Always, {0.5f, 0.0f});
            ImGui::SetNextWindowSize({0, 0});
            ImGui::Begin("##Title", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
            ImGui::PushFont(nullptr);
            float old_size = ImGui::GetFontSize();
            ImGui::SetWindowFontScale(2.0f);
            ImGui::TextColored({0.8f, 0.85f, 1.0f, 1.0f}, "Particle Playground");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored({0.5f, 0.55f, 0.65f, 1.0f},
                "              Select an expansion to launch");
            ImGui::PopFont();
            (void)old_size;
            ImGui::End();
        }

        // Expansion cards
        float card_w = 260.0f;
        float card_h = 200.0f;
        float spacing = 30.0f;
        float total_w = EXPANSION_COUNT * card_w + (EXPANSION_COUNT - 1) * spacing;
        float start_x = (win_w - total_w) * 0.5f;
        float start_y = (win_h - card_h) * 0.5f + 20.0f;

        for (int i = 0; i < EXPANSION_COUNT; i++) {
            const auto& card = EXPANSIONS[i];
            float x = start_x + i * (card_w + spacing);

            ImGui::SetNextWindowPos({x, start_y});
            ImGui::SetNextWindowSize({card_w, card_h});

            char label[64];
            snprintf(label, sizeof(label), "##card_%d", i);
            ImGui::Begin(label, nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize);

            ImGui::TextColored(card.color, "%s", card.title);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", card.description);
            ImGui::Spacing();

            // Launch button at bottom
            float btn_y = card_h - 42.0f;
            ImGui::SetCursorPosY(btn_y);
            ImGui::PushStyleColor(ImGuiCol_Button,        {card.color.x * 0.3f, card.color.y * 0.3f, card.color.z * 0.3f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  card.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {card.color.x * 0.8f, card.color.y * 0.8f, card.color.z * 0.8f, 1.0f});
            if (ImGui::Button("Launch", {card_w - 16.0f, 28.0f})) {
                launch_expansion(card.exe_name);
                should_close = true;
            }
            ImGui::PopStyleColor(3);

            ImGui::End();
        }

        // Version footer
        {
            ImGui::SetNextWindowPos({win_w * 0.5f, win_h - 30.0f}, ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::Begin("##footer", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
            ImGui::TextColored({0.35f, 0.35f, 0.4f, 1.0f}, "v" APP_VERSION);
            ImGui::End();
        }

        renderer.end_frame(vk);

        // Cap to ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    renderer.destroy(vk);
    vk.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
