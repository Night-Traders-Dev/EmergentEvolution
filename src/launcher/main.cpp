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
#include <algorithm>

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
    const char* tagline;
    const char* description;
    const char* features[6];
    int         feature_count;
    const char* stats;
    ImVec4      color;
    ImVec4      color_dark;
};

static const ExpansionCard EXPANSIONS[] = {
    {
        "Particle Physics",
        "particle_physics",
        "The Standard Model & Beyond",
        "Real-time particle physics sandbox simulating all four fundamental forces\n"
        "on Vulkan compute shaders with nuclear reactions, covalent bonding, and\n"
        "radioactive decay.",
        {
            "282 particle types (Standard Model + BSM + mesons)",
            "Nuclear fusion, fission, and decay chains",
            "Covalent bonds and molecule assembly",
            "6 force multiplier sliders for experimentation",
            "239 achievements and 20 guided scenarios",
            "Save/load simulations, online repository",
        },
        6,
        "100K particles  |  282 types  |  13 environments",
        {0.3f, 0.6f, 1.0f, 1.0f},
        {0.08f, 0.15f, 0.30f, 1.0f},
    },
    {
        "Cosmic Sandbox",
        "particle_cosmos",
        "Stars, Planets & Black Holes",
        "3D celestial mechanics simulator with GPU-raytraced rendering,\n"
        "procedurally generated planets, stellar evolution, and N-body gravity\n"
        "with optional general relativity corrections.",
        {
            "23 body types with procedural textures",
            "6 orbital integrators (Verlet to PEFRL)",
            "Stellar evolution through 9 stages",
            "20 sky background presets",
            "Spawn Studio with live 3D ghost preview",
            "Roche disruption, tidal locking, Hawking radiation",
        },
        6,
        "GPU raytracing  |  23 types  |  20 backgrounds",
        {1.0f, 0.7f, 0.2f, 1.0f},
        {0.30f, 0.18f, 0.04f, 1.0f},
    },
    {
        "Biochemical Simulator",
        "particle_biochem",
        "Cells, Bacteria & Viruses",
        "3D cellular biology sandbox with GPU SDF-raytraced rendering,\n"
        "AI-driven entity movement, heritable gene mutations, and\n"
        "multi-stage viral infection with immune response.",
        {
            "9 entity types with 19 morphological variants",
            "16 heritable gene traits with mutations",
            "Multi-stage viral infection and lysis bursts",
            "Bacterial antibiotic warfare (quorum sensing)",
            "4 environment presets (Lung, Pond, Petri, Brain)",
            "Telomere tracking, senescence, and cell division",
        },
        6,
        "SDF raytracing  |  19 variants  |  16 genes",
        {0.3f, 0.9f, 0.4f, 1.0f},
        {0.06f, 0.22f, 0.08f, 1.0f},
    },
};
static constexpr int EXPANSION_COUNT = sizeof(EXPANSIONS) / sizeof(EXPANSIONS[0]);

// ── Drawing helpers ─────────────────────────────────────────────────────────

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

// ── Procedural card icons ───────────────────────────────────────────────────

static void draw_atom_icon(ImDrawList* dl, float cx, float cy, float radius,
                            ImU32 col, float time) {
    // Nucleus
    dl->AddCircleFilled(ImVec2(cx, cy), radius * 0.15f, col, 12);
    // 3 electron orbits
    for (int i = 0; i < 3; i++) {
        float tilt = (float)i * 1.0472f; // 60 degrees apart
        constexpr int SEGS = 48;
        ImVec2 pts[SEGS + 1];
        for (int s = 0; s <= SEGS; s++) {
            float ang = (float)s / SEGS * 6.2832f;
            float ex = cosf(ang) * radius;
            float ey = sinf(ang) * radius * 0.35f;
            float rx = ex * cosf(tilt) - ey * sinf(tilt);
            float ry = ex * sinf(tilt) + ey * cosf(tilt);
            pts[s] = ImVec2(cx + rx, cy + ry);
        }
        int alpha = 60 + i * 10;
        ImU32 orbit_col = (col & ~IM_COL32_A_MASK) | ((uint32_t)alpha << IM_COL32_A_SHIFT);
        dl->AddPolyline(pts, SEGS + 1, orbit_col, 0, 1.0f);
        // Electron dot
        float e_ang = time * (1.2f + i * 0.4f) + (float)i * 2.094f;
        float ex2 = cosf(e_ang) * radius;
        float ey2 = sinf(e_ang) * radius * 0.35f;
        float rx2 = ex2 * cosf(tilt) - ey2 * sinf(tilt);
        float ry2 = ex2 * sinf(tilt) + ey2 * cosf(tilt);
        dl->AddCircleFilled(ImVec2(cx + rx2, cy + ry2), radius * 0.08f, col, 8);
    }
}

static void draw_star_icon(ImDrawList* dl, float cx, float cy, float radius,
                            ImU32 col, float time) {
    // Central star with corona
    float pulse = 1.0f + 0.15f * sinf(time * 2.0f);
    ImU32 glow = (col & ~IM_COL32_A_MASK) | (30u << IM_COL32_A_SHIFT);
    draw_radial_glow(dl, cx, cy, radius * pulse, glow, IM_COL32(0, 0, 0, 0));
    dl->AddCircleFilled(ImVec2(cx, cy), radius * 0.25f * pulse, col, 16);
    // Orbiting planets
    for (int i = 0; i < 3; i++) {
        float orbit_r = radius * (0.45f + i * 0.2f);
        float speed = 0.8f - i * 0.2f;
        float angle = time * speed + (float)i * 2.094f;
        float px = cx + cosf(angle) * orbit_r;
        float py = cy + sinf(angle) * orbit_r * 0.5f;
        float dot_r = radius * (0.06f + i * 0.015f);
        ImU32 planet_col[] = {
            IM_COL32(100, 160, 255, 180),
            IM_COL32(200, 140, 80, 160),
            IM_COL32(120, 200, 120, 140),
        };
        dl->AddCircleFilled(ImVec2(px, py), dot_r, planet_col[i], 8);
    }
}

static void draw_cell_icon(ImDrawList* dl, float cx, float cy, float radius,
                            ImU32 col, float time) {
    // Cell membrane (wobbly circle)
    constexpr int SEGS = 48;
    ImVec2 pts[SEGS + 1];
    for (int s = 0; s <= SEGS; s++) {
        float ang = (float)s / SEGS * 6.2832f;
        float wobble = 1.0f + 0.08f * sinf(ang * 3.0f + time * 1.5f)
                      + 0.05f * sinf(ang * 5.0f - time * 0.8f);
        float r = radius * 0.7f * wobble;
        pts[s] = ImVec2(cx + cosf(ang) * r, cy + sinf(ang) * r);
    }
    ImU32 membrane_col = (col & ~IM_COL32_A_MASK) | (80u << IM_COL32_A_SHIFT);
    dl->AddPolyline(pts, SEGS + 1, membrane_col, ImDrawFlags_Closed, 1.5f);
    // Nucleus
    float nx = cx + sinf(time * 0.5f) * radius * 0.08f;
    float ny = cy + cosf(time * 0.7f) * radius * 0.06f;
    dl->AddCircleFilled(ImVec2(nx, ny), radius * 0.2f, col, 12);
    // Organelles
    for (int i = 0; i < 4; i++) {
        float ang = (float)i * 1.5708f + time * 0.3f;
        float dist = radius * (0.35f + 0.05f * sinf(time + (float)i));
        float ox = cx + cosf(ang) * dist;
        float oy = cy + sinf(ang) * dist;
        ImU32 org_col = (col & ~IM_COL32_A_MASK) | (50u << IM_COL32_A_SHIFT);
        dl->AddCircleFilled(ImVec2(ox, oy), radius * 0.08f, org_col, 8);
    }
}

// ── Background animation system ─────────────────────────────────────────────

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

    constexpr int N = 90;
    bg_particles.resize(N);
    bg_trails.resize(N);

    for (int i = 0; i < N; ++i) {
        auto& p = bg_particles[i];
        p.x = randf() * W;
        p.y = randf() * H;
        float angle = randf() * 6.2832f;
        float speed = 0.12f + randf() * 0.30f;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.base_r = 1.5f + randf() * 3.5f;
        p.r = p.base_r;
        p.pulse_phase = randf() * 6.2832f;
        ImU32 c = PALETTE[i % NCOLORS];
        p.color = c;
        int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
        int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
        int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;
        p.glow_color = IM_COL32(cr, cg, cb, 25);
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
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 8, 18, 255));

    // 2. Slow-drifting nebula glows
    float t = bg_time;
    draw_radial_glow(bg,
        W * 0.20f + sinf(t * 0.06f) * W * 0.10f,
        H * 0.30f + cosf(t * 0.05f) * H * 0.08f,
        380.0f, IM_COL32(15, 30, 90, 18), IM_COL32(0, 0, 0, 0));
    draw_radial_glow(bg,
        W * 0.75f + cosf(t * 0.05f) * W * 0.06f,
        H * 0.65f + sinf(t * 0.07f) * H * 0.06f,
        300.0f, IM_COL32(40, 15, 80, 14), IM_COL32(0, 0, 0, 0));
    draw_radial_glow(bg,
        W * 0.50f + sinf(t * 0.04f + 2.0f) * W * 0.12f,
        H * 0.15f + cosf(t * 0.03f + 1.0f) * H * 0.10f,
        240.0f, IM_COL32(10, 50, 60, 12), IM_COL32(0, 0, 0, 0));

    // 3. Update & draw particles
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
        if (bg_trails[i].size() > 10)
            bg_trails[i].erase(bg_trails[i].begin());
    }

    // 5. Draw trails
    for (size_t i = 0; i < bg_particles.size(); ++i) {
        auto& trail = bg_trails[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float frac = (float)j / (float)trail.size();
            float alpha = frac * 0.20f;
            float width = bg_particles[i].r * (0.1f + 0.3f * frac);
            ImU32 col = (bg_particles[i].color & ~IM_COL32_A_MASK)
                      | ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j - 1], trail[j], col, width);
        }
    }

    // 6. Draw particles with glow
    for (auto& p : bg_particles) {
        draw_radial_glow(bg, p.x, p.y, p.r * 3.5f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
    }

    // 7. Force lines between close particles
    float link_dist = 90.0f;
    for (size_t i = 0; i < bg_particles.size(); ++i) {
        for (size_t j = i + 1; j < bg_particles.size(); ++j) {
            float dx = bg_particles[j].x - bg_particles[i].x;
            float dy = bg_particles[j].y - bg_particles[i].y;
            float d2 = dx * dx + dy * dy;
            if (d2 < link_dist * link_dist && d2 > 25.0f) {
                float d = sqrtf(d2);
                float alpha = (1.0f - d / link_dist) * 0.04f;
                bg->AddLine(ImVec2(bg_particles[i].x, bg_particles[i].y),
                            ImVec2(bg_particles[j].x, bg_particles[j].y),
                            IM_COL32(40, 90, 180, (int)(alpha * 255)), 1.0f);
            }
        }
    }

    // 8. Vignette
    draw_vignette(bg, W, H);
}

// ── Title glow effect ───────────────────────────────────────────────────────

static void draw_title_glow(ImDrawList* dl, const char* text, float cx, float cy,
                             float font_size, ImU32 glow_col) {
    for (int layer = 3; layer >= 0; layer--) {
        float offset = (float)(layer + 1) * 1.5f;
        int alpha = 8 + layer * 4;
        ImU32 col = (glow_col & ~IM_COL32_A_MASK) | ((uint32_t)alpha << IM_COL32_A_SHIFT);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(nullptr, font_size,
                    ImVec2(cx + dx * offset, cy + dy * offset), col, text);
            }
        }
    }
}

// ── Feature bullet helper ───────────────────────────────────────────────────

static void draw_feature_bullet(const char* text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.x, color.y, color.z, 0.7f));
    ImGui::TextUnformatted("*");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.75f, 0.82f, 1.0f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// ── Keyboard navigation state ───────────────────────────────────────────────

static int focused_card = 0;
static const char* kb_selected_exe = nullptr;
static float launch_fade = 0.0f;

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_UP) {
        if (focused_card > 0) focused_card--;
    }
    if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_DOWN) {
        if (focused_card < EXPANSION_COUNT - 1) focused_card++;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE) {
        if (focused_card >= 0 && focused_card < EXPANSION_COUNT)
            kb_selected_exe = EXPANSIONS[focused_card].exe_name;
    }
    // Number keys 1-3 for quick launch
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
        int idx = key - GLFW_KEY_1;
        if (idx < EXPANSION_COUNT) {
            focused_card = idx;
            kb_selected_exe = EXPANSIONS[idx].exe_name;
        }
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

    // Get primary monitor for better default sizing
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int init_w = std::min(1400, (int)(mode->width * 0.75f));
    int init_h = std::min(850, (int)(mode->height * 0.75f));

    GLFWwindow* window = glfwCreateWindow(
        init_w, init_h, "Emergent Evolution v" APP_VERSION, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        show_error_dialog("Startup Error", "Failed to create window.");
        return 1;
    }

    // Center window on screen
    glfwSetWindowPos(window, (mode->width - init_w) / 2, (mode->height - init_h) / 2);

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

    // Card animation state
    float card_hover[EXPANSION_COUNT] = {};
    float card_select_anim[EXPANSION_COUNT] = {};
    float intro_anim = 0.0f; // 0 to 1 intro fade-in

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

        // Intro animation
        if (intro_anim < 1.0f)
            intro_anim = std::min(1.0f, intro_anim + dt * 1.2f);

        if (!renderer.begin_frame(vk, window))
            continue;

        ImGuiIO& io = ImGui::GetIO();
        float W = io.DisplaySize.x;
        float H = io.DisplaySize.y;

        // ── Animated background ────────────────────────────────────────────
        draw_background(dt);

        ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();

        // ── Title area ─────────────────────────────────────────────────────
        {
            float title_alpha = std::min(1.0f, intro_anim * 2.0f);
            float title_y = 28.0f + (1.0f - title_alpha) * -30.0f;

            // Title glow
            ImGui::SetWindowFontScale(1.0f);
            float font_size = 14.0f * 2.2f;
            const char* title_text = "Emergent Evolution";
            ImVec2 title_size = ImGui::CalcTextSize(title_text);
            title_size.x *= 2.2f;
            float title_x = W * 0.5f - title_size.x * 0.5f;

            draw_title_glow(bg_dl, title_text, title_x, title_y, font_size,
                            IM_COL32(120, 150, 255, 255));

            // Title window
            ImGui::SetNextWindowPos({W * 0.5f, title_y}, ImGuiCond_Always, {0.5f, 0.0f});
            ImGui::SetNextWindowSize({0, 0});
            ImGui::Begin("##Title", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs);
            ImGui::SetWindowFontScale(2.2f);
            ImGui::TextColored({0.82f, 0.86f, 1.0f, title_alpha}, "Emergent Evolution");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Spacing();
            ImGui::TextColored({0.5f, 0.55f, 0.65f, title_alpha * 0.9f},
                "GPU-Accelerated Science Simulation Suite");
            ImGui::End();
        }

        // ── Expansion cards ────────────────────────────────────────────────
        // Responsive card sizing
        float avail_w = W - 60.0f;
        float spacing = std::max(16.0f, std::min(35.0f, avail_w * 0.02f));
        float card_w = std::min(380.0f, (avail_w - spacing * (EXPANSION_COUNT - 1)) / EXPANSION_COUNT);
        float card_h = std::min(440.0f, std::max(320.0f, H * 0.52f));
        float total_w = EXPANSION_COUNT * card_w + (EXPANSION_COUNT - 1) * spacing;
        float start_x = (W - total_w) * 0.5f;
        float start_y = std::max(110.0f, H * 0.14f);

        for (int i = 0; i < EXPANSION_COUNT; i++) {
            const auto& card = EXPANSIONS[i];

            // Card intro stagger
            float card_intro = std::max(0.0f, std::min(1.0f,
                (intro_anim - 0.15f - i * 0.12f) * 3.0f));
            float ease = card_intro * card_intro * (3.0f - 2.0f * card_intro); // smoothstep

            float x = start_x + i * (card_w + spacing);
            float y_offset = (1.0f - ease) * 40.0f;
            float y = start_y + y_offset;
            bool is_focused = (focused_card == i);

            // Smooth hover animation
            bool hovered_now = false;

            float target_hover = is_focused ? 1.0f : 0.0f;
            card_hover[i] += (target_hover - card_hover[i]) * dt * 6.0f;
            float hover = card_hover[i];

            // Hover lift effect
            y -= hover * 4.0f;

            // ── Card glow border + hover effects (behind card) ──────────
            if (hover > 0.01f) {
                for (int layer = 4; layer >= 1; layer--) {
                    float expand = (float)layer * 4.0f * hover;
                    int alpha = (int)(hover * 18.0f / (float)layer);
                    ImU32 glow_col = IM_COL32(
                        (int)(card.color.x * 255), (int)(card.color.y * 255),
                        (int)(card.color.z * 255), alpha);
                    bg_dl->AddRect(
                        ImVec2(x - expand, y - expand),
                        ImVec2(x + card_w + expand, y + card_h + expand),
                        glow_col, 8.0f + expand, 0, 1.5f);
                }
            }

            // ── Card window ─────────────────────────────────────────────
            ImGui::SetNextWindowPos({x, y});
            ImGui::SetNextWindowSize({card_w, card_h});

            char label[64];
            snprintf(label, sizeof(label), "##card_%d", i);

            float bg_alpha = 0.82f + hover * 0.08f;
            ImGui::PushStyleColor(ImGuiCol_WindowBg,
                ImVec4(card.color_dark.x, card.color_dark.y, card.color_dark.z, bg_alpha * ease));
            float border_brightness = 0.25f + hover * 0.35f;
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(
                card.color.x * border_brightness,
                card.color.y * border_brightness,
                card.color.z * border_brightness,
                0.6f * ease));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f + hover * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));

            ImGui::Begin(label, nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

            hovered_now = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            if (hovered_now) focused_card = i;

            ImDrawList* card_dl = ImGui::GetWindowDrawList();

            // ── Procedural icon (top-right corner) ──────────────────────
            float icon_cx = x + card_w - 38.0f;
            float icon_cy = y + 36.0f;
            float icon_r = 24.0f;
            ImU32 icon_col = IM_COL32(
                (int)(card.color.x * 255), (int)(card.color.y * 255),
                (int)(card.color.z * 255), (int)(80 + hover * 80));
            if (i == 0) draw_atom_icon(card_dl, icon_cx, icon_cy, icon_r, icon_col, bg_time);
            else if (i == 1) draw_star_icon(card_dl, icon_cx, icon_cy, icon_r, icon_col, bg_time);
            else draw_cell_icon(card_dl, icon_cx, icon_cy, icon_r, icon_col, bg_time);

            // ── Card number badge ───────────────────────────────────────
            char num_buf[4];
            snprintf(num_buf, sizeof(num_buf), "%d", i + 1);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(card.color.x, card.color.y, card.color.z, 0.35f));
            ImGui::SetWindowFontScale(0.85f);
            ImGui::TextUnformatted(num_buf);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6.0f);

            // ── Title ───────────────────────────────────────────────────
            ImGui::SetWindowFontScale(1.35f);
            ImGui::TextColored(ImVec4(card.color.x, card.color.y, card.color.z, 1.0f),
                               "%s", card.title);
            ImGui::SetWindowFontScale(1.0f);

            // ── Tagline ─────────────────────────────────────────────────
            ImGui::TextColored(ImVec4(card.color.x * 0.7f + 0.3f,
                                       card.color.y * 0.7f + 0.3f,
                                       card.color.z * 0.7f + 0.3f, 0.65f),
                               "%s", card.tagline);

            // ── Separator ───────────────────────────────────────────────
            ImGui::Spacing();
            {
                ImVec2 p1 = ImGui::GetCursorScreenPos();
                ImVec2 p2 = ImVec2(p1.x + card_w - 28.0f, p1.y);
                ImU32 sep_col = IM_COL32(
                    (int)(card.color.x * 255), (int)(card.color.y * 255),
                    (int)(card.color.z * 255), 40);
                card_dl->AddLine(p1, p2, sep_col, 1.0f);
                ImGui::Dummy(ImVec2(0, 4.0f));
            }

            // ── Description ─────────────────────────────────────────────
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 0.9f));
            ImGui::SetWindowFontScale(0.88f);
            ImGui::TextWrapped("%s", card.description);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // ── Feature bullets ─────────────────────────────────────────
            ImGui::SetWindowFontScale(0.85f);
            for (int f = 0; f < card.feature_count; f++) {
                draw_feature_bullet(card.features[f], card.color);
            }
            ImGui::SetWindowFontScale(1.0f);

            // ── Stats bar at bottom ─────────────────────────────────────
            float stats_y = card_h - 72.0f;
            ImGui::SetCursorPosY(stats_y);
            {
                ImVec2 p1 = ImGui::GetCursorScreenPos();
                ImVec2 p2 = ImVec2(p1.x + card_w - 28.0f, p1.y);
                ImU32 sep_col = IM_COL32(
                    (int)(card.color.x * 255), (int)(card.color.y * 255),
                    (int)(card.color.z * 255), 25);
                card_dl->AddLine(p1, p2, sep_col, 1.0f);
                ImGui::Dummy(ImVec2(0, 4.0f));
            }
            ImGui::SetWindowFontScale(0.78f);
            ImGui::TextColored(ImVec4(card.color.x * 0.6f + 0.2f,
                                       card.color.y * 0.6f + 0.2f,
                                       card.color.z * 0.6f + 0.2f, 0.55f),
                               "%s", card.stats);
            ImGui::SetWindowFontScale(1.0f);

            // ── Launch button ───────────────────────────────────────────
            ImGui::SetCursorPosY(card_h - 40.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(card.color.x * 0.15f, card.color.y * 0.15f, card.color.z * 0.15f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(card.color.x * 0.4f, card.color.y * 0.4f, card.color.z * 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, card.color);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

            char btn_label[64];
            snprintf(btn_label, sizeof(btn_label), "Launch###launch_%d", i);
            if (ImGui::Button(btn_label, {card_w - 28.0f, 28.0f}))
                selected_exe = card.exe_name;

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);

            // ── Focused border pulse ───────────────────────────────────
            if (is_focused && hover > 0.5f) {
                float pulse = 0.5f + 0.5f * sinf(bg_time * 3.0f);
                int alpha = (int)(pulse * 40.0f * hover);
                ImU32 focus_col = IM_COL32(
                    (int)(card.color.x * 255), (int)(card.color.y * 255),
                    (int)(card.color.z * 255), alpha);
                bg_dl->AddRect(ImVec2(x - 2, y - 2),
                            ImVec2(x + card_w + 2, y + card_h + 2),
                            focus_col, 10.0f, 0, 2.0f);
            }
        }

        // ── Footer ─────────────────────────────────────────────────────────
        {
            float footer_alpha = std::min(1.0f, intro_anim * 1.5f - 0.3f);
            if (footer_alpha > 0.01f) {
                float footer_y = H - 52.0f;

                // Navigation hints (left)
                ImGui::SetNextWindowPos({20.0f, footer_y + 4.0f});
                ImGui::SetNextWindowSize({0, 0});
                ImGui::Begin("##nav_hint", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoInputs);
                ImGui::TextColored({0.35f, 0.38f, 0.48f, footer_alpha * 0.8f},
                    "[Arrow keys] Navigate    [Enter / 1-3] Launch    [Esc] Quit");
                ImGui::End();

                // Version badge (center)
                const char* ver_text = "v" APP_VERSION;
                ImVec2 ver_size = ImGui::CalcTextSize(ver_text);
                float badge_x = W * 0.5f - ver_size.x * 0.5f - 8.0f;
                float badge_y = footer_y;
                float badge_w = ver_size.x + 16.0f;
                float badge_h = ver_size.y + 8.0f;
                bg_dl->AddRectFilled(ImVec2(badge_x, badge_y),
                                  ImVec2(badge_x + badge_w, badge_y + badge_h),
                                  IM_COL32(15, 20, 35, (int)(140 * footer_alpha)), 10.0f);
                bg_dl->AddRect(ImVec2(badge_x, badge_y),
                            ImVec2(badge_x + badge_w, badge_y + badge_h),
                            IM_COL32(50, 65, 100, (int)(80 * footer_alpha)), 10.0f);

                ImGui::SetNextWindowPos({W * 0.5f, footer_y + 4.0f}, ImGuiCond_Always, {0.5f, 0.0f});
                ImGui::Begin("##footer_ver", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoInputs);
                ImGui::TextColored({0.45f, 0.5f, 0.6f, footer_alpha}, "%s", ver_text);
                ImGui::End();

                // Credits (right)
                ImGui::SetNextWindowPos({W - 20.0f, footer_y + 4.0f}, ImGuiCond_Always, {1.0f, 0.0f});
                ImGui::SetNextWindowSize({0, 0});
                ImGui::Begin("##credits", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoInputs);
                ImGui::TextColored({0.35f, 0.38f, 0.48f, footer_alpha * 0.7f},
                    "Vulkan 1.3  |  C++20  |  MIT License");
                ImGui::End();
            }
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
