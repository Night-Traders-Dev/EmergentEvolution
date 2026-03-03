#include "common/simple_renderer.h"
#include "common/error_dialog.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <vector>
#include <random>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <climits>
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// ── Resolve directory containing this executable ────────────────────────────

static std::string get_exe_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* sep = strrchr(path, '\\');
    if (sep) sep[1] = '\0';
    return path;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "./";
    buf[len] = '\0';
    char* sep = strrchr(buf, '/');
    if (sep) sep[1] = '\0';
    return buf;
#endif
}

// ── Subprocess launch ───────────────────────────────────────────────────────

static void launch_expansion(const char* exe_name) {
    std::string dir = get_exe_dir();

#ifdef _WIN32
    std::string cmd = dir + exe_name + ".exe";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CreateProcessA(cmd.c_str(), nullptr, nullptr, nullptr,
                   FALSE, 0, nullptr, dir.c_str(), &si, &pi);
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    std::string cmd = dir + exe_name;
    pid_t pid = fork();
    if (pid == 0) {
        execl(cmd.c_str(), exe_name, nullptr);
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
        "Stars, planets, moons, asteroids, black holes.\n"
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

// ── Background animation system ─────────────────────────────────────────────

static void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                              ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 28;
    for (int s = STEPS; s >= 0; --s) {
        float t = (float)s / STEPS;
        float r = radius * t;
        if (r < 1.0f) continue;
        float blend = 1.0f - t;
        int a_c = (center_col >> IM_COL32_A_SHIFT) & 0xFF;
        int a_e = (edge_col   >> IM_COL32_A_SHIFT) & 0xFF;
        int a = a_c + (int)((a_e - a_c) * blend);
        int r_c = (center_col >> IM_COL32_R_SHIFT) & 0xFF, r_e = (edge_col >> IM_COL32_R_SHIFT) & 0xFF;
        int g_c = (center_col >> IM_COL32_G_SHIFT) & 0xFF, g_e = (edge_col >> IM_COL32_G_SHIFT) & 0xFF;
        int b_c = (center_col >> IM_COL32_B_SHIFT) & 0xFF, b_e = (edge_col >> IM_COL32_B_SHIFT) & 0xFF;
        int rr = r_c + (int)((r_e - r_c) * blend);
        int gg = g_c + (int)((g_e - g_c) * blend);
        int bb = b_c + (int)((b_e - b_c) * blend);
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 64);
    }
}

static void draw_vignette(ImDrawList* dl, float W, float H) {
    constexpr int BANDS = 32;
    float band_h = H / static_cast<float>(BANDS);
    for (int i = 0; i < BANDS; ++i) {
        float y_center = (static_cast<float>(i) + 0.5f) / static_cast<float>(BANDS);
        float edge_dist = std::abs(y_center - 0.5f) * 2.0f;
        float vignette = edge_dist * edge_dist * edge_dist;
        float strength = (y_center < 0.5f) ? 0.7f : 0.9f;
        int a = static_cast<int>(vignette * strength * 255.0f);
        if (a < 1) continue;
        dl->AddRectFilled(ImVec2(0, i * band_h), ImVec2(W, (i + 1) * band_h),
                          IM_COL32(2, 8, 16, std::min(a, 255)));
    }
}

struct BgParticle {
    float x, y, vx, vy;
    float base_r, r;
    float pulse_phase;
    ImU32 color, glow_color;
};

static std::vector<BgParticle> bg_particles;
static std::vector<std::vector<ImVec2>> bg_trails;
static float bg_time = 0.0f;
static bool bg_inited = false;

static void init_background(float W, float H) {
    static const ImU32 PALETTE[] = {
        IM_COL32(60, 120, 220, 140),   // blue
        IM_COL32(100, 80, 200, 120),   // purple
        IM_COL32(40, 180, 200, 110),   // teal
        IM_COL32(80, 100, 240, 100),   // indigo
        IM_COL32(140, 100, 220, 90),   // lavender
        IM_COL32(30, 160, 180, 100),   // cyan
        IM_COL32(200, 180, 255, 60),   // faint white-purple
        IM_COL32(255, 220, 200, 50),   // faint warm
    };
    static const int NCOLORS = sizeof(PALETTE) / sizeof(PALETTE[0]);

    std::mt19937 rng(42);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };

    constexpr int N = 80;
    bg_particles.resize(N);
    bg_trails.resize(N);

    for (int i = 0; i < N; ++i) {
        auto& p = bg_particles[i];
        p.x = randf() * W;
        p.y = randf() * H;
        float angle = randf() * 6.2832f;
        float speed = 0.15f + randf() * 0.35f;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.base_r = 2.0f + randf() * 4.0f;
        p.r = p.base_r;
        p.pulse_phase = randf() * 6.2832f;
        ImU32 c = PALETTE[i % NCOLORS];
        p.color = c;
        int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
        int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
        int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;
        p.glow_color = IM_COL32(cr, cg, cb, 30);
    }
    bg_inited = true;
}

static void draw_background(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    bg_time += dt;

    if (!bg_inited) init_background(W, H);

    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // 1. Dark base fill
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 6, 14, 255));

    // 2. Slow-drifting nebula glows
    float t = bg_time;
    draw_radial_glow(bg,
        W * 0.25f + sinf(t * 0.08f) * W * 0.12f,
        H * 0.35f + cosf(t * 0.06f) * H * 0.10f,
        350.0f, IM_COL32(20, 40, 120, 22), IM_COL32(0, 0, 0, 0));
    draw_radial_glow(bg,
        W * 0.72f + cosf(t * 0.07f) * W * 0.08f,
        H * 0.60f + sinf(t * 0.09f) * H * 0.08f,
        280.0f, IM_COL32(50, 20, 100, 18), IM_COL32(0, 0, 0, 0));
    draw_radial_glow(bg,
        W * 0.50f + sinf(t * 0.05f + 2.0f) * W * 0.15f,
        H * 0.20f + cosf(t * 0.04f + 1.0f) * H * 0.12f,
        220.0f, IM_COL32(15, 60, 80, 15), IM_COL32(0, 0, 0, 0));

    // 3. Update particles
    for (auto& p : bg_particles) {
        p.r = p.base_r * (1.0f + 0.18f * sinf(t * 1.5f + p.pulse_phase));
        p.x += p.vx * dt * 60.0f;
        p.y += p.vy * dt * 60.0f;
        if (p.x < -20.0f) p.x += W + 40.0f;
        if (p.x > W + 20.0f) p.x -= W + 40.0f;
        if (p.y < -20.0f) p.y += H + 40.0f;
        if (p.y > H + 20.0f) p.y -= H + 40.0f;
    }

    // 4. Update trails
    for (size_t i = 0; i < bg_particles.size(); ++i) {
        bg_trails[i].push_back(ImVec2(bg_particles[i].x, bg_particles[i].y));
        if (bg_trails[i].size() > 12)
            bg_trails[i].erase(bg_trails[i].begin());
    }

    // 5. Draw trails
    for (size_t i = 0; i < bg_particles.size(); ++i) {
        auto& trail = bg_trails[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float frac = (float)j / (float)trail.size();
            float alpha = frac * 0.25f;
            float width = bg_particles[i].r * (0.1f + 0.4f * frac);
            ImU32 col = (bg_particles[i].color & ~IM_COL32_A_MASK)
                      | ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j - 1], trail[j], col, width);
        }
    }

    // 6. Draw particles with glow
    for (auto& p : bg_particles) {
        draw_radial_glow(bg, p.x, p.y, p.r * 4.0f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
    }

    // 7. Force lines between close particles
    float link_dist = 100.0f;
    for (size_t i = 0; i < bg_particles.size(); ++i) {
        for (size_t j = i + 1; j < bg_particles.size(); ++j) {
            float dx = bg_particles[j].x - bg_particles[i].x;
            float dy = bg_particles[j].y - bg_particles[i].y;
            float d2 = dx * dx + dy * dy;
            if (d2 < link_dist * link_dist && d2 > 25.0f) {
                float d = sqrtf(d2);
                float alpha = (1.0f - d / link_dist) * 0.06f;
                bg->AddLine(ImVec2(bg_particles[i].x, bg_particles[i].y),
                            ImVec2(bg_particles[j].x, bg_particles[j].y),
                            IM_COL32(40, 100, 200, (int)(alpha * 255)), 1.0f);
            }
        }
    }

    // 8. Vignette + scanlines
    draw_vignette(bg, W, H);
    float gap = 4.0f;
    for (float y = 0; y < H; y += gap)
        bg->AddRectFilled(ImVec2(0, y + gap * 0.5f), ImVec2(W, y + gap),
                          IM_COL32(0, 0, 0, 5));
}

// ── Card mini-particle systems ──────────────────────────────────────────────

struct CardParticle {
    float x, y, vx, vy, r;
    float pulse_phase;
};

static constexpr int CARD_PARTICLES = 10;
static CardParticle card_particles[EXPANSION_COUNT][CARD_PARTICLES];
static bool card_particles_inited = false;

static void init_card_particles() {
    std::mt19937 rng(123);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };

    for (int c = 0; c < EXPANSION_COUNT; c++) {
        for (int p = 0; p < CARD_PARTICLES; p++) {
            auto& cp = card_particles[c][p];
            cp.x = randf();
            cp.y = randf();
            float angle = randf() * 6.2832f;
            float speed = 0.2f + randf() * 0.4f;
            cp.vx = cosf(angle) * speed;
            cp.vy = sinf(angle) * speed;
            cp.r = 1.5f + randf() * 2.5f;
            cp.pulse_phase = randf() * 6.2832f;
        }
    }
    card_particles_inited = true;
}

static void draw_card_particles(ImDrawList* dl, int card_idx, float x, float y,
                                 float w, float h, const ImVec4& color, float dt) {
    if (!card_particles_inited) init_card_particles();

    for (int p = 0; p < CARD_PARTICLES; p++) {
        auto& cp = card_particles[card_idx][p];
        cp.x += cp.vx * dt * 0.02f;
        cp.y += cp.vy * dt * 0.02f;
        if (cp.x < 0) cp.x += 1.0f;
        if (cp.x > 1) cp.x -= 1.0f;
        if (cp.y < 0) cp.y += 1.0f;
        if (cp.y > 1) cp.y -= 1.0f;

        float px = x + cp.x * w;
        float py = y + cp.y * h;
        float r = cp.r * (1.0f + 0.2f * sinf(bg_time * 2.0f + cp.pulse_phase));
        ImU32 col = IM_COL32(
            (int)(color.x * 255), (int)(color.y * 255),
            (int)(color.z * 255), 40);
        ImU32 glow = IM_COL32(
            (int)(color.x * 255), (int)(color.y * 255),
            (int)(color.z * 255), 12);
        draw_radial_glow(dl, px, py, r * 3.0f, glow, IM_COL32(0, 0, 0, 0));
        dl->AddCircleFilled(ImVec2(px, py), r, col);
    }
}

// ── Title glow effect ───────────────────────────────────────────────────────

static void draw_title_glow(ImDrawList* dl, const char* text, float cx, float cy, float scale) {
    // Multi-layer bloom effect
    ImVec2 text_size = ImGui::CalcTextSize(text);
    text_size.x *= scale;
    text_size.y *= scale;
    float tx = cx - text_size.x * 0.5f;
    float ty = cy;

    // Glow layers (large to small, decreasing alpha)
    for (int layer = 3; layer >= 0; layer--) {
        float offset = (float)(layer + 1) * 1.5f;
        int alpha = 8 + layer * 3;
        ImU32 glow_col = IM_COL32(140, 170, 255, alpha);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(nullptr, 14.0f * scale,
                    ImVec2(tx + dx * offset, ty + dy * offset), glow_col, text);
            }
        }
    }
}

// ── Keyboard navigation state ───────────────────────────────────────────────

static int focused_card = -1;
static const char* kb_selected_exe = nullptr;

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_UP) {
        if (focused_card < 0) focused_card = 0;
        else if (focused_card > 0) focused_card--;
    }
    if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_DOWN) {
        if (focused_card < 0) focused_card = 0;
        else if (focused_card < EXPANSION_COUNT - 1) focused_card++;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (focused_card >= 0 && focused_card < EXPANSION_COUNT)
            kb_selected_exe = EXPANSIONS[focused_card].exe_name;
    }
}

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

#ifndef _WIN32
    setenv("LIBDECOR_PLUGIN_DIR", "/nonexistent", 0);
#endif

    if (!glfwInit()) {
        show_error_dialog("Startup Error", "Failed to initialize GLFW.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(
        1100, 700, "Emergent Evolution v" APP_VERSION, nullptr, nullptr);
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
    glfwSetKeyCallback(window, key_callback);

    const char* selected_exe = nullptr;

    using Clock = std::chrono::high_resolution_clock;
    auto last_time = Clock::now();

    // Card hover animation state
    float card_hover[EXPANSION_COUNT] = {};
    float card_glow_alpha[EXPANSION_COUNT] = {};

    while (!glfwWindowShouldClose(window) && !selected_exe) {
        glfwPollEvents();

        // Check keyboard selection
        if (kb_selected_exe) {
            selected_exe = kb_selected_exe;
            break;
        }

        auto now = Clock::now();
        float dt = (float)std::chrono::duration<double>(now - last_time).count();
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        if (!renderer.begin_frame(vk, window))
            continue;

        ImGuiIO& io = ImGui::GetIO();
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;

        // ── Animated background ────────────────────────────────────────────
        draw_background(dt);

        ImDrawList* bg = ImGui::GetBackgroundDrawList();

        // ── Title with glow ────────────────────────────────────────────────
        {
            float title_y = 40.0f;
            draw_title_glow(bg, "Emergent Evolution", win_w * 0.5f, title_y, 2.0f);

            ImGui::SetNextWindowPos({win_w * 0.5f, title_y}, ImGuiCond_Always, {0.5f, 0.0f});
            ImGui::SetNextWindowSize({0, 0});
            ImGui::Begin("##Title", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs);
            ImGui::PushFont(nullptr);
            ImGui::SetWindowFontScale(2.0f);
            ImGui::TextColored({0.8f, 0.85f, 1.0f, 1.0f}, "Emergent Evolution");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored({0.5f, 0.55f, 0.65f, 1.0f},
                "Select an expansion to launch");
            ImGui::PopFont();
            ImGui::End();
        }

        // ── Expansion cards ────────────────────────────────────────────────
        float card_w = 280.0f;
        float card_h = 220.0f;
        float spacing = 35.0f;
        float total_w = EXPANSION_COUNT * card_w + (EXPANSION_COUNT - 1) * spacing;
        float start_x = (win_w - total_w) * 0.5f;
        float start_y = (win_h - card_h) * 0.5f + 20.0f;

        for (int i = 0; i < EXPANSION_COUNT; i++) {
            const auto& card = EXPANSIONS[i];
            float x = start_x + i * (card_w + spacing);
            bool is_focused = (focused_card == i);

            // Smooth hover animation
            bool hovered_now = false;

            ImGui::SetNextWindowPos({x, start_y});
            ImGui::SetNextWindowSize({card_w, card_h});

            char label[64];
            snprintf(label, sizeof(label), "##card_%d", i);

            // Semi-transparent card background
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.08f, 0.12f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(
                card.color.x * 0.3f, card.color.y * 0.3f, card.color.z * 0.3f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

            ImGui::Begin(label, nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize);

            hovered_now = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            if (hovered_now) focused_card = i;

            ImGui::TextColored(card.color, "%s", card.title);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", card.description);
            ImGui::Spacing();

            // Launch button at bottom
            float btn_y = card_h - 46.0f;
            ImGui::SetCursorPosY(btn_y);
            ImGui::PushStyleColor(ImGuiCol_Button,
                {card.color.x * 0.2f, card.color.y * 0.2f, card.color.z * 0.2f, 0.8f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, card.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                {card.color.x * 0.8f, card.color.y * 0.8f, card.color.z * 0.8f, 1.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            if (ImGui::Button("Launch", {card_w - 16.0f, 30.0f}))
                selected_exe = card.exe_name;

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);

            // ── Card glow border + hover effects (on background draw list) ──
            float target_hover = (hovered_now || is_focused) ? 1.0f : 0.0f;
            card_hover[i] += (target_hover - card_hover[i]) * dt * 8.0f;
            card_glow_alpha[i] = card_hover[i];

            if (card_glow_alpha[i] > 0.01f) {
                float ga = card_glow_alpha[i];
                // Outer glow
                for (int layer = 3; layer >= 1; layer--) {
                    float expand = (float)layer * 3.0f * ga;
                    int alpha = (int)(ga * 15.0f / (float)layer);
                    ImU32 glow_col = IM_COL32(
                        (int)(card.color.x * 255), (int)(card.color.y * 255),
                        (int)(card.color.z * 255), alpha);
                    bg->AddRect(
                        ImVec2(x - expand, start_y - expand),
                        ImVec2(x + card_w + expand, start_y + card_h + expand),
                        glow_col, 6.0f + expand, 0, 1.5f);
                }
                // Bright border
                int border_alpha = (int)(ga * 180.0f);
                ImU32 border_col = IM_COL32(
                    (int)(card.color.x * 255), (int)(card.color.y * 255),
                    (int)(card.color.z * 255), border_alpha);
                bg->AddRect(
                    ImVec2(x, start_y), ImVec2(x + card_w, start_y + card_h),
                    border_col, 6.0f, 0, 1.5f);
            }

            // ── Mini particle system inside card ────────────────────────────
            draw_card_particles(bg, i, x, start_y, card_w, card_h, card.color, dt);
        }

        // ── Keyboard focus indicator ───────────────────────────────────────
        if (focused_card >= 0) {
            float fx = start_x + focused_card * (card_w + spacing);
            float pulse = 0.5f + 0.5f * sinf(bg_time * 3.0f);
            int alpha = (int)(pulse * 60.0f);
            ImU32 focus_col = IM_COL32(255, 255, 255, alpha);
            bg->AddRect(ImVec2(fx - 2, start_y - 2),
                        ImVec2(fx + card_w + 2, start_y + card_h + 2),
                        focus_col, 8.0f, 0, 2.0f);
        }

        // ── Version footer with badge ──────────────────────────────────────
        {
            const char* ver_text = "v" APP_VERSION;
            ImVec2 ver_size = ImGui::CalcTextSize(ver_text);
            float badge_x = win_w * 0.5f - ver_size.x * 0.5f - 8.0f;
            float badge_y = win_h - 32.0f;
            float badge_w = ver_size.x + 16.0f;
            float badge_h = ver_size.y + 8.0f;
            bg->AddRectFilled(ImVec2(badge_x, badge_y),
                              ImVec2(badge_x + badge_w, badge_y + badge_h),
                              IM_COL32(20, 25, 40, 160), 10.0f);
            bg->AddRect(ImVec2(badge_x, badge_y),
                        ImVec2(badge_x + badge_w, badge_y + badge_h),
                        IM_COL32(60, 80, 120, 100), 10.0f);

            ImGui::SetNextWindowPos({win_w * 0.5f, win_h - 28.0f}, ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::Begin("##footer", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs);
            ImGui::TextColored({0.45f, 0.5f, 0.6f, 1.0f}, "%s", ver_text);
            ImGui::End();
        }

        renderer.end_frame(vk);

        // Cap to ~60 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Tear down Vulkan + GLFW completely before launching
    renderer.destroy(vk);
    vk.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();

    // Now launch the selected expansion (after all GPU/display resources are freed)
    if (selected_exe)
        launch_expansion(selected_exe);

    return 0;
}
