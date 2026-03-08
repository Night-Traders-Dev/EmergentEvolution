#include "cosmos/cosmos_app_internal.h"
#include "common/paths.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "cosmos/ui/cosmos_ui_data.h"

namespace {

struct SpawnPreviewStyle {
    int planet_look = 0; // 0 auto, 1 rocky, 2 water, 3 ice, 4 earth-like, 5 gas giant
    bool spawn_rings = false;
    bool spawn_moons = false;
    int moon_count = 0;
    int moon_layout = 0;
    float moon_inclination_deg = 8.0f;
    float moon_spacing_scale = 1.0f;
    bool system_preview = false;
};

void draw_spawn_preview_thumb(const char* thumb_id, int preview_type, float mass_hint,
                              uint32_t seed, const SpawnPreviewStyle& style) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float side = std::clamp(avail.x, 140.0f, 230.0f);
    ImGui::InvisibleButton(thumb_id, ImVec2(side, side));
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float r = side * 0.30f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(9, 13, 26, 255), IM_COL32(14, 18, 30, 255),
        IM_COL32(6, 8, 15, 255), IM_COL32(4, 6, 12, 255));
    dl->AddRect(p0, p1, IM_COL32(70, 78, 102, 170), 6.0f);

    uint32_t local_seed = seed ^ (uint32_t)(preview_type * 2654435761u) ^
                          (uint32_t)(std::abs((int)(mass_hint * 1000000.0f)) + 17);
    std::mt19937 prng(local_seed);
    auto rf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(prng);
    };
    int stars = 42;
    for (int i = 0; i < stars; ++i) {
        ImVec2 sp(rf(p0.x + 6.0f, p1.x - 6.0f), rf(p0.y + 6.0f, p1.y - 6.0f));
        int a = (int)rf(40.0f, 165.0f);
        float sz = rf(0.7f, 1.9f);
        dl->AddCircleFilled(sp, sz, IM_COL32(210, 220, 245, a), 10);
    }

    if (style.system_preview) {
        draw_radial_glow(dl, c.x, c.y, r * 1.45f, IM_COL32(255, 240, 180, 80), IM_COL32(255, 230, 120, 0));
        dl->AddCircleFilled(c, r * 0.42f, IM_COL32(255, 228, 146, 255), 48);
        for (int i = 0; i < 3; ++i) {
            float orb = r * (0.90f + (float)i * 0.42f);
            dl->AddCircle(c, orb, IM_COL32(210, 210, 195, 95), 64, 1.0f);
            float a = rf(0.0f, 6.2831853f);
            ImVec2 pc(c.x + std::cos(a) * orb, c.y + std::sin(a) * orb);
            ImU32 col = i == 0 ? IM_COL32(190, 170, 160, 240) : (i == 1 ? IM_COL32(110, 170, 235, 240) : IM_COL32(210, 200, 165, 240));
            dl->AddCircleFilled(pc, r * (0.06f + 0.01f * i), col, 24);
        }
        return;
    }

    if (is_star_type((uint32_t)preview_type)) {
        ImU32 tint = CTYPE_COLORS[std::clamp(preview_type, 0, (int)CTYPE_COUNT - 1)];
        float glow = r * (1.4f + std::clamp(std::log10(std::max(mass_hint, 1.0e-5f) + 1.0f), 0.0f, 0.9f));
        draw_radial_glow(dl, c.x, c.y, glow, IM_COL32((tint >> IM_COL32_R_SHIFT) & 0xFF,
                                                       (tint >> IM_COL32_G_SHIFT) & 0xFF,
                                                       (tint >> IM_COL32_B_SHIFT) & 0xFF, 120),
                         IM_COL32((tint >> IM_COL32_R_SHIFT) & 0xFF,
                                  (tint >> IM_COL32_G_SHIFT) & 0xFF,
                                  (tint >> IM_COL32_B_SHIFT) & 0xFF, 0));
        dl->AddCircleFilled(c, r * 0.78f, tint, 72);
        for (int i = 0; i < 10; ++i) {
            float a = rf(0.0f, 6.2831853f);
            float len = r * rf(0.9f, 1.4f);
            ImVec2 pA(c.x + std::cos(a) * r * 0.8f, c.y + std::sin(a) * r * 0.8f);
            ImVec2 pB(c.x + std::cos(a) * (r * 0.8f + len), c.y + std::sin(a) * (r * 0.8f + len));
            dl->AddLine(pA, pB, IM_COL32(255, 220, 140, 120), 1.2f);
        }
        return;
    }

    if (is_black_hole_type((uint32_t)preview_type)) {
        draw_radial_glow(dl, c.x, c.y, r * 1.45f, IM_COL32(130, 180, 255, 28), IM_COL32(80, 120, 190, 0));
        dl->AddCircleFilled(c, r * 0.85f, IM_COL32(8, 8, 12, 255), 72);
        for (int i = 0; i < 6; ++i) {
            float rr = r * (1.0f + 0.07f * (float)i);
            ImU32 col = IM_COL32((int)(220 - i * 16), (int)(170 - i * 18), (int)(95 - i * 9), (int)(140 - i * 18));
            dl->AddCircle(c, rr, col, 96, 1.5f);
        }
        dl->AddCircle(c, r * 1.25f, IM_COL32(170, 210, 255, 58), 84, 1.0f);
        return;
    }

    int look = style.planet_look;
    if (preview_type == CTYPE_PLANET || preview_type == CTYPE_MOON) {
        if (look == 0) {
            float mass_earth = mass_hint / std::max(EARTH_MASS_SOLAR, 1.0e-12f);
            if (mass_earth > 18.0f) look = 5;
            else if (mass_earth > 1.9f) look = 1;
            else look = 4;
        }
        if (preview_type == CTYPE_MOON && look == 5) look = 2;
    } else if (preview_type == CTYPE_COMET) {
        look = 3;
    } else if (preview_type == CTYPE_DUST) {
        look = 1;
    } else if (preview_type == CTYPE_ASTEROID) {
        look = 1;
    } else if (preview_type == CTYPE_NEBULA) {
        look = 2;
    }

    ImVec4 base_a(0.55f, 0.58f, 0.62f, 1.0f), base_b(0.32f, 0.36f, 0.40f, 1.0f);
    if (look == 2) { base_a = ImVec4(0.20f, 0.45f, 0.82f, 1.0f); base_b = ImVec4(0.10f, 0.24f, 0.54f, 1.0f); }
    if (look == 3) { base_a = ImVec4(0.78f, 0.86f, 0.95f, 1.0f); base_b = ImVec4(0.48f, 0.62f, 0.78f, 1.0f); }
    if (look == 4) { base_a = ImVec4(0.30f, 0.55f, 0.30f, 1.0f); base_b = ImVec4(0.18f, 0.30f, 0.62f, 1.0f); }
    if (look == 5) { base_a = ImVec4(0.84f, 0.64f, 0.36f, 1.0f); base_b = ImVec4(0.62f, 0.42f, 0.23f, 1.0f); }

    if (preview_type == CTYPE_ASTEROID || preview_type == CTYPE_COMET) {
        ImVec2 poly[16];
        for (int i = 0; i < 16; ++i) {
            float a = (float)i / 16.0f * 6.2831853f;
            float rr = r * (0.62f + rf(-0.16f, 0.12f));
            poly[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr * 0.92f);
        }
        dl->AddConvexPolyFilled(poly, 16, ImColor(base_b));
        for (int i = 0; i < 9; ++i) {
            float a = rf(0.0f, 6.2831853f);
            float d = rf(r * 0.08f, r * 0.5f);
            ImVec2 cp(c.x + std::cos(a) * d, c.y + std::sin(a) * d * 0.85f);
            dl->AddCircleFilled(cp, rf(1.4f, 3.3f), IM_COL32(150, 160, 180, 70), 14);
        }
        if (preview_type == CTYPE_COMET) {
            for (int t = 0; t < 28; ++t) {
                float f = (float)t / 27.0f;
                ImVec2 pA(c.x - r * (0.45f + f * 1.7f), c.y + r * (0.16f * std::sin(f * 8.0f)));
                dl->AddCircleFilled(pA, 2.0f * (1.0f - f * 0.85f), IM_COL32(180, 220, 255, (int)(95 * (1.0f - f))), 10);
            }
        }
    } else if (preview_type == CTYPE_DUST) {
        for (int i = 0; i < 120; ++i) {
            float a = rf(0.0f, 6.2831853f);
            float d = r * std::sqrt(rf(0.0f, 1.0f));
            ImVec2 dp(c.x + std::cos(a) * d, c.y + std::sin(a) * d * 0.85f);
            dl->AddCircleFilled(dp, rf(0.7f, 1.8f), IM_COL32(215, 195, 168, (int)rf(45.0f, 170.0f)), 8);
        }
    } else if (preview_type == CTYPE_NEBULA) {
        for (int i = 0; i < 36; ++i) {
            float a = rf(0.0f, 6.2831853f);
            float d = rf(0.0f, r * 0.9f);
            ImVec2 np(c.x + std::cos(a) * d, c.y + std::sin(a) * d * 0.72f);
            float nr = rf(r * 0.13f, r * 0.34f);
            dl->AddCircleFilled(np, nr, IM_COL32((int)rf(100.0f, 170.0f), (int)rf(80.0f, 140.0f), (int)rf(170.0f, 250.0f), 55), 18);
        }
    } else {
        dl->AddCircleFilled(c, r, ImColor(base_b), 72);
        for (int i = 0; i < 24; ++i) {
            float t = (float)i / 23.0f;
            ImVec2 cc(c.x - r * 0.20f + t * r * 0.35f, c.y - r * 0.55f + t * r * 1.1f);
            float rr = r * (0.36f + 0.07f * std::sin(t * 6.283f));
            dl->AddCircleFilled(cc, rr, ImColor(base_a.x * (0.90f + 0.10f * t),
                                                base_a.y * (0.90f + 0.08f * t),
                                                base_a.z * (0.90f + 0.06f * t), 1.0f), 40);
        }
        if (look == 5) {
            for (int b = 0; b < 8; ++b) {
                float y = c.y - r * 0.85f + (float)b * (r * 0.24f);
                float th = r * (0.06f + rf(0.0f, 0.04f));
                ImU32 col = IM_COL32((int)(200 + rf(0.0f, 40.0f)), (int)(130 + rf(0.0f, 45.0f)),
                                     (int)(80 + rf(0.0f, 35.0f)), 180);
                dl->AddRectFilled(ImVec2(c.x - r * 0.92f, y - th), ImVec2(c.x + r * 0.92f, y + th), col, th * 2.0f);
            }
        } else {
            int blobs = 12;
            for (int b = 0; b < blobs; ++b) {
                float a = rf(0.0f, 6.2831853f);
                float rr = rf(r * 0.12f, r * 0.72f);
                ImVec2 bc(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr * 0.85f);
                float br = rf(r * 0.05f, r * 0.16f);
                ImU32 col = IM_COL32((int)(80 + rf(0.0f, 120.0f)), (int)(90 + rf(0.0f, 130.0f)),
                                     (int)(100 + rf(0.0f, 140.0f)), 90);
                dl->AddCircleFilled(bc, br, col, 20);
            }
        }
    }

    dl->AddCircle(c, r, IM_COL32(230, 240, 255, 75), 72, 1.6f);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.28f, c.y - r * 0.32f), r * 0.32f, IM_COL32(255, 255, 255, 38), 28);
    if (style.spawn_rings) {
        dl->AddCircle(ImVec2(c.x, c.y), r * 1.35f, IM_COL32(220, 210, 185, 120), 80, 1.4f);
        dl->AddCircle(ImVec2(c.x, c.y), r * 1.52f, IM_COL32(200, 190, 170, 90), 80, 1.0f);
    }
    if (style.spawn_moons) {
        int shown = std::min(std::max(style.moon_count, 1), 10);
        int layout = std::clamp(style.moon_layout, 0, 4);
        float incl = glm::radians(std::clamp(style.moon_inclination_deg, 0.0f, 85.0f));
        float spacing = std::clamp(style.moon_spacing_scale, 0.35f, 4.0f);
        for (int m = 0; m < shown; ++m) {
            float frac = (float)m / (float)std::max(shown - 1, 1);
            float a = 0.45f + frac * 5.5f;
            float base_d = r * (1.45f + (0.04f + 0.05f * spacing) * (float)(m % 6));
            if (layout == 1) base_d *= 0.83f;
            if (layout == 2) base_d *= 1.26f;
            if (layout == 3) base_d = r * (1.35f * std::pow(1.32f, (float)m));
            if (layout == 4) a = hash_float((uint32_t)m * 9781u + 31u) * 6.2831853f;
            float y_scale = (layout == 4) ? 0.98f : (0.45f + 0.45f * std::sin(incl));
            ImVec2 mc(c.x + std::cos(a) * base_d, c.y + std::sin(a) * base_d * y_scale);
            dl->AddCircleFilled(mc, 2.0f + 0.5f * (float)(m % 3), IM_COL32(205, 210, 220, 220), 12);
        }
    }
}

} // namespace

void CosmosApp::draw_spawn_menu() {
    // Auto-toggle space fabric grid with spawn menu visibility
    if (spawn_menu_visible_ && !spawn_grid_was_visible_) {
        grid_was_on_before_spawn_ = cfg.cosmos_space_fabric;
        cfg.cosmos_space_fabric = true;
        spawn_grid_was_visible_ = true;
    } else if (!spawn_menu_visible_ && spawn_grid_was_visible_) {
        if (!grid_was_on_before_spawn_)
            cfg.cosmos_space_fabric = false;
        spawn_grid_was_visible_ = false;
    }

    if (!spawn_menu_visible_) return;

    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 36.0f;
    float menu_h = 210.0f;
    float menu_w = io.DisplaySize.x;
    float menu_y = io.DisplaySize.y - menu_h - bar_h;

    ImGui::SetNextWindowPos({0.0f, menu_y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({menu_w, menu_h}, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.26f, 0.80f));

    if (!ImGui::Begin("##SpawnMenu", &spawn_menu_visible_,
                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    struct TypeEntry { int type; float mass; const char* desc; };
    static const TypeEntry BASIC[] = {
        {CTYPE_PLANET, 3.003e-6f, "Rocky or gas world"},
        {CTYPE_MOON, 3.70e-8f, "Natural satellite"},
        {CTYPE_ASTEROID, 2.0e-10f, "Small rocky body"},
        {CTYPE_COMET, 8.0e-11f, "Icy body with tail"},
        {CTYPE_DUST, 5.0e-12f, "Ring/disk particle"},
        {CTYPE_NEBULA, 0.02f, "Gas cloud"},
    };
    static const TypeEntry STARS[] = {
        {CTYPE_STAR, 1.0f, "Generic star"},
        {CTYPE_STAR_O, 30.0f, "Blue supergiant >30kK"},
        {CTYPE_STAR_B, 5.0f, "Blue-white 10-30kK"},
        {CTYPE_STAR_A, 1.8f, "White 7.5-10kK"},
        {CTYPE_STAR_F, 1.2f, "Yellow-white 6-7.5kK"},
        {CTYPE_STAR_G, 1.0f, "Sun-like 5.2-6kK"},
        {CTYPE_STAR_K, 0.6f, "Orange 3.7-5.2kK"},
        {CTYPE_STAR_M, 0.2f, "Red dwarf 2.4-3.7kK"},
        {CTYPE_STAR_L, 0.06f, "Brown dwarf 1.3-2.4kK"},
        {CTYPE_STAR_T, 0.04f, "Cool brown dwarf"},
        {CTYPE_STAR_Y, 0.02f, "Ultra-cool <500K"},
        {CTYPE_STAR_WR, 20.0f, "Wolf-Rayet, extreme wind"},
    };
    static const TypeEntry BHS[] = {
        {CTYPE_BLACK_HOLE, 200.0f, "Generic black hole"},
        {CTYPE_BH_STELLAR, 10.0f, "3-20 solar masses"},
        {CTYPE_BH_INTERMEDIATE, 1000.0f, "100-100k solar masses"},
        {CTYPE_BH_SUPERMASSIVE, 1000000.0f, "Galactic center class"},
        {CTYPE_BH_PRIMORDIAL, 0.5f, "Sub-stellar mass"},
    };
    static int catalog_tab = 0;
    const TypeEntry* active_list = BASIC;
    int active_count = (int)IM_ARRAYSIZE(BASIC);
    if (catalog_tab == 1) { active_list = STARS; active_count = (int)IM_ARRAYSIZE(STARS); }
    if (catalog_tab == 2) { active_list = BHS; active_count = (int)IM_ARRAYSIZE(BHS); }

    // ── Left sidebar: vertical category tabs ──
    constexpr float SIDEBAR_W = 40.0f;
    const char* tab_icons[] = {"Bd", "St", "BH", "Lb", "Md"};
    const char* tab_tooltips[] = {"Bodies", "Stars", "Black Holes", "Known Objects", "Existing Objects"};

    ImGui::BeginChild("##sidebar", ImVec2(SIDEBAR_W, 0), false);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 6));
    ImGui::Dummy(ImVec2(0, 8));
    for (int t = 0; t < 5; ++t) {
        bool active = (catalog_tab == t);
        ImVec4 bg = active ? ImVec4(0.28f, 0.42f, 0.62f, 1.0f) : ImVec4(0.14f, 0.14f, 0.16f, 0.9f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bg.x + 0.08f, bg.y + 0.08f, bg.z + 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(bg.x + 0.14f, bg.y + 0.14f, bg.z + 0.14f, 1.0f));
        ImGui::SetCursorPosX(4);
        if (ImGui::Button(tab_icons[t], ImVec2(SIDEBAR_W - 6, 32))) {
            if (catalog_tab != t) {
                catalog_tab = t;
                const TypeEntry* new_list = BASIC;
                int new_count = (int)IM_ARRAYSIZE(BASIC);
                if (t == 1) { new_list = STARS; new_count = (int)IM_ARRAYSIZE(STARS); }
                if (t == 2) { new_list = BHS; new_count = (int)IM_ARRAYSIZE(BHS); }
                if (t < 3) {
                    bool found = false;
                    for (int j = 0; j < new_count; ++j)
                        if (new_list[j].type == spawn_type) { found = true; break; }
                    if (!found) {
                        spawn_type = new_list[0].type;
                        spawn_mass = new_list[0].mass;
                    }
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tab_tooltips[t]);
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    // ── Main content area (right of sidebar) ──
    ImGui::BeginChild("##main_content", ImVec2(0, 0), false);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));

    if (catalog_tab == 3) {
        static int known_subtab = 0;
        static int kuiper_count = 450;
        static int oort_count = 900;
        static int known_preview_choice[4] = {0, 0, 0, 0};
        static uint32_t known_preview_seed = 0x7A11B55Du;
        const char* known_tabs[] = {"Basic", "Stars", "Black Holes", "System"};

        auto hash_name_seed = [](const char* name, uint32_t salt) -> uint32_t {
            uint32_t h = 2166136261u ^ salt;
            if (!name) return h;
            for (const unsigned char* p = (const unsigned char*)name; *p; ++p)
                h = (h ^ (uint32_t)(*p)) * 16777619u;
            return h;
        };

        auto make_body = [&](const char* name, uint32_t type, float mass_solar, float radius_km,
                             float temperature_k, float rotation_hours,
                             uint32_t stage, float fuel) -> CelestialBody {
            CelestialBody b;
            b.type = type;
            b.mass = std::max(mass_solar, 1.0e-12f);
            b.radius = std::max(radius_km / SIM_UNIT_TO_KM, 0.06f);
            b.temperature = temperature_k;
            b.stellar_stage = stage;
            b.fuel = std::clamp(fuel, 0.0f, 1.0f);
            b.seed = hash_name_seed(name, (uint32_t)(type * 2654435761u));
            b.name = name ? std::string(name) : generate_body_name(b.seed, b.type);
            if (rotation_hours > 0.0f)
                b.angular_vel = (2.0f * 3.14159265359f) / (rotation_hours * 3600.0f);
            clear_ring_system(b);
            clear_impact_signature(b);
            if (is_star_type(type)) {
                b.type = classify_star_spectral(std::max(temperature_k, 250.0f), std::max(mass_solar, 0.003f));
                b.stellar_stage = stage;
                b.material_phase = PHASE_PLASMA;
                b.phase_intensity = 1.0f;
                float r_solar = std::max(radius_km / 696340.0f, 1.0e-4f);
                float stefan = r_solar * r_solar * std::pow(std::max(temperature_k, 100.0f) / 5778.0f, 4.0f);
                float mass_law = (b.mass < 0.43f)
                    ? 0.23f * std::pow(std::max(b.mass, 0.01f), 2.3f)
                    : (b.mass < 2.0f ? std::pow(b.mass, 4.0f)
                                     : (b.mass < 20.0f ? 1.5f * std::pow(b.mass, 3.5f)
                                                       : 3200.0f * std::pow(std::max(b.mass, 20.0f) / 20.0f, 2.2f)));
                b.luminosity = std::max(stefan, mass_law);
            } else if (is_black_hole_type(type)) {
                b.type = classify_black_hole(std::max(mass_solar, 0.01f));
                b.temperature = 0.0f;
                b.fuel = 0.0f;
                b.material_phase = PHASE_PLASMA;
                b.phase_intensity = 1.0f;
                b.luminosity = 0.0f;
            } else {
                b.material_phase = (type == CTYPE_NEBULA) ? PHASE_GAS : PHASE_SOLID;
                b.phase_intensity = (type == CTYPE_NEBULA) ? 0.75f : 0.0f;
                if (type == CTYPE_NEBULA) {
                    b.custom_material = true;
                    b.custom_hydrogen = 0.90f;
                    b.custom_silicate = 0.06f;
                    b.custom_water = 0.03f;
                    b.custom_iron = 0.01f;
                }
            }
            return b;
        };

        auto append_body = [&](CelestialBody b) -> int {
            refresh_body_render_state(b, &state);
            state.bodies.push_back(std::move(b));
            state.trails.emplace_back();
            return (int)state.bodies.size() - 1;
        };

        auto place_orbit_km = [&](CelestialBody& body, int parent_idx, float orbit_km,
                                  float phase_rad, float inclination_deg) {
            if (parent_idx < 0 || parent_idx >= (int)state.bodies.size()) return;
            const CelestialBody& parent = state.bodies[(size_t)parent_idx];
            float orbit_r = std::max(orbit_km / SIM_UNIT_TO_KM, parent.radius + body.radius + 2.0f);
            float inc = glm::radians(inclination_deg);
            glm::vec3 rel(
                std::cos(phase_rad) * orbit_r,
                std::sin(inc) * std::sin(phase_rad) * orbit_r,
                std::cos(inc) * std::sin(phase_rad) * orbit_r);
            body.pos = parent.pos + rel;
            body.parent = parent_idx;
            // For extremely distant orbits (Oort-scale), avoid iterative fitting and use
            // an analytic circular estimate to keep spawn stable and cheap.
            if (orbit_r > 5.0e6f) {
                glm::vec3 r_hat = glm::normalize(rel);
                glm::vec3 up(0.0f, 1.0f, 0.0f);
                glm::vec3 tangent = glm::cross(up, r_hat);
                if (glm::dot(tangent, tangent) < 1.0e-8f)
                    tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
                tangent = glm::normalize(tangent);
                float mu = cfg.G * std::max(parent.mass + body.mass, 1.0e-8f);
                float v_circ = std::sqrt(std::max(mu / std::max(orbit_r, 1.0e-6f), 0.0f));
                body.vel = parent.vel + tangent * v_circ;
            } else {
                body.vel = verlet_auto_orbit_velocity(body, parent, 0.0f, 1.0f);
            }
        };

        auto find_star_or_create_sun = [&]() -> int {
            for (int i = 0; i < (int)state.bodies.size(); ++i) {
                const auto& b = state.bodies[(size_t)i];
                if (!b.marked_for_removal && is_star_type(b.type))
                    return i;
            }
            CelestialBody sun = make_body("Sun", CTYPE_STAR_G, 1.0f, 696340.0f, 5778.0f,
                                          26.0f * 24.0f, SSTAGE_MAIN_SEQUENCE, 0.72f);
            sun.pos = camera.target;
            sun.vel = glm::vec3(0.0f);
            return append_body(std::move(sun));
        };

        auto spawn_kuiper_belt = [&](int sun_idx, int count) {
            if (sun_idx < 0 || sun_idx >= (int)state.bodies.size()) return;
            debug_logf("spawn/kuiper_belt start sun_idx=%d count=%d bodies_before=%zu",
                       sun_idx, count, state.bodies.size());
            constexpr float AU_KM = 149597870.7f;
            std::mt19937 rng((uint32_t)(sim_time_ * 1000.0f + 9173.0f));
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            std::uniform_real_distribution<float> dist_au(30.0f, 50.0f);
            std::uniform_real_distribution<float> inc_deg(-10.0f, 10.0f);
            for (int i = 0; i < count; ++i) {
                bool comet = (u01(rng) < 0.62f);
                float mass = comet ? (4.0e-13f + u01(rng) * 6.0e-11f)
                                   : (5.0e-12f + u01(rng) * 2.0e-10f);
                float radius_km = comet ? (1.0f + u01(rng) * 60.0f) : (15.0f + u01(rng) * 1200.0f);
                CelestialBody obj = make_body(
                    comet ? "Kuiper Comet" : "Kuiper Object",
                    comet ? CTYPE_COMET : CTYPE_ASTEROID,
                    mass, radius_km, comet ? 45.0f : 70.0f, 8.0f + u01(rng) * 20.0f,
                    SSTAGE_MAIN_SEQUENCE, 0.0f);
                float phase = u01(rng) * 6.2831853f;
                place_orbit_km(obj, sun_idx, dist_au(rng) * AU_KM, phase, inc_deg(rng));
                obj.non_attracting = true;
                obj.seed = hash_combine(obj.seed, (uint32_t)i);
                append_body(std::move(obj));
            }
            debug_logf("spawn/kuiper_belt end bodies_after=%zu", state.bodies.size());
            validate_body_state("spawn/kuiper_belt", true);
            debug_logf("spawn/kuiper_belt validated bodies=%zu", state.bodies.size());
        };

        auto spawn_oort_cloud = [&](int sun_idx, int count) {
            if (sun_idx < 0 || sun_idx >= (int)state.bodies.size()) return;
            debug_logf("spawn/oort_cloud start sun_idx=%d count=%d bodies_before=%zu",
                       sun_idx, count, state.bodies.size());
            constexpr float AU_KM = 149597870.7f;
            std::mt19937 rng((uint32_t)(sim_time_ * 1300.0f + 29011.0f));
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            std::uniform_real_distribution<float> dist_au(2000.0f, 12000.0f);
            std::uniform_real_distribution<float> cos_i(-1.0f, 1.0f);
            for (int i = 0; i < count; ++i) {
                float mass = 2.0e-14f + u01(rng) * 4.0e-12f;
                float radius_km = 0.3f + u01(rng) * 24.0f;
                CelestialBody comet = make_body("Oort Comet", CTYPE_COMET, mass, radius_km, 18.0f,
                                                6.0f + u01(rng) * 30.0f, SSTAGE_MAIN_SEQUENCE, 0.0f);
                float phase = u01(rng) * 6.2831853f;
                float inc = glm::degrees(std::acos(std::clamp(cos_i(rng), -1.0f, 1.0f)));
                place_orbit_km(comet, sun_idx, dist_au(rng) * AU_KM, phase, inc);
                comet.non_attracting = true;
                comet.seed = hash_combine(comet.seed, (uint32_t)(i * 19 + 7));
                append_body(std::move(comet));
            }
            debug_logf("spawn/oort_cloud end bodies_after=%zu", state.bodies.size());
            validate_body_state("spawn/oort_cloud", true);
            debug_logf("spawn/oort_cloud validated bodies=%zu", state.bodies.size());
        };

        auto spawn_solar_system = [&](bool with_structures) {
            debug_logf("spawn/solar_system start with_structures=%d bodies_before=%zu",
                       with_structures ? 1 : 0, state.bodies.size());
            constexpr float AU_KM = 149597870.7f;
            struct PlanetDef {
                const char* name;
                float mass;
                float radius_km;
                float temp_k;
                float orbit_au;
                float incl_deg;
                float rotation_h;
            };
            struct MoonDef {
                const char* name;
                int planet_idx;
                float mass;
                float radius_km;
                float temp_k;
                float orbit_km;
                float incl_deg;
                float rotation_h;
            };
            const PlanetDef planets[] = {
                {"Mercury", 1.660e-7f, 2439.7f, 440.0f, 0.387f, 7.0f, 1407.6f},
                {"Venus",   2.447e-6f, 6051.8f, 737.0f, 0.723f, 3.4f, -5832.0f},
                {"Earth",   3.003e-6f, 6371.0f, 288.0f, 1.000f, 0.0f, 24.0f},
                {"Mars",    3.227e-7f, 3389.5f, 210.0f, 1.524f, 1.9f, 24.6f},
                {"Jupiter", 9.5458e-4f, 69911.0f, 165.0f, 5.203f, 1.3f, 9.9f},
                {"Saturn",  2.858e-4f, 58232.0f, 134.0f, 9.537f, 2.5f, 10.7f},
                {"Uranus",  4.366e-5f, 25362.0f, 76.0f, 19.191f, 0.8f, -17.2f},
                {"Neptune", 5.151e-5f, 24622.0f, 72.0f, 30.070f, 1.8f, 16.1f},
                {"Pluto",   6.56e-9f, 1188.3f, 44.0f, 39.482f, 17.2f, -153.3f},
            };
            const MoonDef moons[] = {
                {"Moon",      2, 3.694e-8f, 1737.4f, 220.0f,   384400.0f,  5.1f, 655.7f},
                {"Phobos",    3, 5.41e-16f, 11.3f,   230.0f,     9376.0f,  1.1f,   7.7f},
                {"Deimos",    3, 9.10e-17f,  6.2f,   200.0f,    23463.0f,  0.9f,  30.3f},
                {"Io",        4, 4.49e-8f, 1821.6f,  110.0f,   421700.0f,  0.0f,  42.5f},
                {"Europa",    4, 2.41e-8f, 1560.8f,  102.0f,   671100.0f,  0.5f,  85.2f},
                {"Ganymede",  4, 7.80e-8f, 2634.1f,  110.0f,  1070400.0f,  0.2f, 171.7f},
                {"Callisto",  4, 5.67e-8f, 2410.3f,  134.0f,  1882700.0f,  0.2f, 400.5f},
                {"Titan",     5, 6.76e-8f, 2574.7f,   94.0f,  1221870.0f,  0.3f, 382.7f},
                {"Enceladus", 5, 5.44e-11f, 252.1f,   75.0f,   237950.0f,  0.0f,  32.9f},
                {"Titania",   6, 1.76e-8f,  788.9f,   65.0f,   436300.0f,  0.1f, 208.7f},
                {"Triton",    7, 1.08e-8f, 1353.4f,   38.0f,   354759.0f, 156.9f, 141.0f},
                {"Charon",    8, 7.57e-10f, 606.0f,   48.0f,    19596.0f,  0.0f, 153.3f},
            };

            CelestialBody sun = make_body("Sun", CTYPE_STAR_G, 1.0f, 696340.0f, 5778.0f,
                                          26.0f * 24.0f, SSTAGE_MAIN_SEQUENCE, 0.72f);
            sun.pos = camera.target;
            sun.vel = glm::vec3(0.0f);
            int sun_idx = append_body(std::move(sun));

            int planet_indices[IM_ARRAYSIZE(planets)]{};
            for (int i = 0; i < (int)IM_ARRAYSIZE(planets); ++i) {
                const PlanetDef& pd = planets[(size_t)i];
                CelestialBody p = make_body(pd.name, CTYPE_PLANET, pd.mass, pd.radius_km,
                                            pd.temp_k, pd.rotation_h, SSTAGE_MAIN_SEQUENCE, 0.0f);
                float phase = (float)i * (6.2831853f / (float)IM_ARRAYSIZE(planets));
                place_orbit_km(p, sun_idx, pd.orbit_au * AU_KM, phase, pd.incl_deg);
                if (std::strcmp(pd.name, "Saturn") == 0) {
                    p.ring_inner_radius = p.radius * 1.35f;
                    p.ring_outer_radius = p.radius * 2.35f;
                    p.ring_density = 0.38f;
                    p.ring_ice_fraction = 0.72f;
                    p.ring_tilt = 0.12f;
                }
                planet_indices[i] = append_body(std::move(p));
            }

            for (size_t i = 0; i < IM_ARRAYSIZE(moons); ++i) {
                const MoonDef& md = moons[i];
                if (md.planet_idx < 0 || md.planet_idx >= (int)IM_ARRAYSIZE(planets)) continue;
                int parent_idx = planet_indices[md.planet_idx];
                CelestialBody m = make_body(md.name, CTYPE_MOON, md.mass, md.radius_km,
                                            md.temp_k, md.rotation_h, SSTAGE_MAIN_SEQUENCE, 0.0f);
                float phase = (float)i * 0.93f + 0.7f;
                place_orbit_km(m, parent_idx, md.orbit_km, phase, md.incl_deg);
                append_body(std::move(m));
            }

            if (with_structures) {
                spawn_kuiper_belt(sun_idx, kuiper_count);
                spawn_oort_cloud(sun_idx, oort_count);
            }
            debug_logf("spawn/solar_system end bodies_after=%zu", state.bodies.size());
            validate_body_state("spawn/solar_system", true);
            debug_logf("spawn/solar_system validated bodies=%zu", state.bodies.size());
        };

        auto find_body_by_name = [&](const char* name) -> int {
            for (int i = 0; i < (int)state.bodies.size(); ++i) {
                const auto& b = state.bodies[(size_t)i];
                if (b.marked_for_removal) continue;
                if (!b.name.empty() && b.name == name) return i;
            }
            return -1;
        };

        auto ensure_named_planet = [&](const char* name, float mass_solar, float radius_km,
                                       float temp_k, float orbit_au, float incl_deg,
                                       float rotation_h) -> int {
            int idx = find_body_by_name(name);
            if (idx >= 0) return idx;
            constexpr float AU_KM = 149597870.7f;
            int sun_idx = find_star_or_create_sun();
            CelestialBody p = make_body(name, CTYPE_PLANET, mass_solar, radius_km,
                                        temp_k, rotation_h, SSTAGE_MAIN_SEQUENCE, 0.0f);
            float phase = hash_float(hash_name_seed(name, 314159u)) * 6.2831853f;
            place_orbit_km(p, sun_idx, orbit_au * AU_KM, phase, incl_deg);
            return append_body(std::move(p));
        };

        auto spawn_named_moon = [&](const char* name, int parent_idx, float mass_solar,
                                    float radius_km, float temp_k, float orbit_km,
                                    float incl_deg, float rotation_h) {
            CelestialBody m = make_body(name, CTYPE_MOON, mass_solar, radius_km,
                                        temp_k, rotation_h, SSTAGE_MAIN_SEQUENCE, 0.0f);
            float phase = hash_float(hash_name_seed(name, 271828u)) * 6.2831853f;
            place_orbit_km(m, parent_idx, orbit_km, phase, incl_deg);
            append_body(std::move(m));
        };

        auto spawn_galilean_system = [&]() {
            int jupiter = ensure_named_planet("Jupiter", 9.5458e-4f, 69911.0f, 165.0f, 5.203f, 1.3f, 9.9f);
            spawn_named_moon("Io", jupiter, 4.49e-8f, 1821.6f, 110.0f, 421700.0f, 0.0f, 42.5f);
            spawn_named_moon("Europa", jupiter, 2.41e-8f, 1560.8f, 102.0f, 671100.0f, 0.5f, 85.2f);
            spawn_named_moon("Ganymede", jupiter, 7.80e-8f, 2634.1f, 110.0f, 1070400.0f, 0.2f, 171.7f);
            spawn_named_moon("Callisto", jupiter, 5.67e-8f, 2410.3f, 134.0f, 1882700.0f, 0.2f, 400.5f);
        };

        auto spawn_saturn_system = [&]() {
            int saturn = ensure_named_planet("Saturn", 2.858e-4f, 58232.0f, 134.0f, 9.537f, 2.5f, 10.7f);
            if (saturn >= 0 && saturn < (int)state.bodies.size()) {
                auto& p = state.bodies[(size_t)saturn];
                p.ring_inner_radius = p.radius * 1.35f;
                p.ring_outer_radius = p.radius * 2.35f;
                p.ring_density = 0.38f;
                p.ring_ice_fraction = 0.72f;
                p.ring_tilt = 0.12f;
                p.visuals_valid = false;
            }
            spawn_named_moon("Titan", saturn, 6.76e-8f, 2574.7f, 94.0f, 1221870.0f, 0.3f, 382.7f);
            spawn_named_moon("Enceladus", saturn, 5.44e-11f, 252.1f, 75.0f, 237950.0f, 0.0f, 32.9f);
            spawn_named_moon("Rhea", saturn, 1.17e-8f, 763.8f, 76.0f, 527108.0f, 0.3f, 108.4f);
            spawn_named_moon("Iapetus", saturn, 9.06e-10f, 734.5f, 81.0f, 3560820.0f, 15.5f, 1903.0f);
        };

        auto spawn_uranus_system = [&]() {
            int uranus = ensure_named_planet("Uranus", 4.366e-5f, 25362.0f, 76.0f, 19.191f, 0.8f, -17.2f);
            spawn_named_moon("Miranda", uranus, 3.30e-11f, 235.8f, 60.0f, 129390.0f, 4.3f, 33.9f);
            spawn_named_moon("Ariel", uranus, 6.75e-10f, 578.9f, 58.0f, 190900.0f, 0.3f, 60.5f);
            spawn_named_moon("Umbriel", uranus, 6.50e-10f, 584.7f, 58.0f, 266000.0f, 0.1f, 99.5f);
            spawn_named_moon("Titania", uranus, 1.76e-8f, 788.9f, 65.0f, 436300.0f, 0.1f, 208.7f);
            spawn_named_moon("Oberon", uranus, 1.51e-8f, 761.4f, 63.0f, 583500.0f, 0.1f, 323.3f);
        };

        auto spawn_neptune_system = [&]() {
            int neptune = ensure_named_planet("Neptune", 5.151e-5f, 24622.0f, 72.0f, 30.070f, 1.8f, 16.1f);
            spawn_named_moon("Triton", neptune, 1.08e-8f, 1353.4f, 38.0f, 354759.0f, 156.9f, 141.0f);
            spawn_named_moon("Nereid", neptune, 1.55e-12f, 170.0f, 50.0f, 5513818.0f, 7.2f, 360.0f * 360.0f);
        };

        auto spawn_pluto_system = [&]() {
            int pluto = ensure_named_planet("Pluto", 6.56e-9f, 1188.3f, 44.0f, 39.482f, 17.2f, -153.3f);
            spawn_named_moon("Charon", pluto, 7.57e-10f, 606.0f, 48.0f, 19596.0f, 0.0f, 153.3f);
            spawn_named_moon("Nix", pluto, 2.35e-14f, 24.8f, 40.0f, 48694.0f, 0.1f, 600.0f);
            spawn_named_moon("Hydra", pluto, 2.80e-14f, 30.5f, 40.0f, 64738.0f, 0.2f, 900.0f);
        };

        ImGui::Separator();
        for (int t = 0; t < 4; ++t) {
            if (t > 0) ImGui::SameLine();
            if (known_subtab == t) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.26f, 0.10f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.30f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.46f, 0.34f, 0.15f, 1.0f));
            }
            if (ImGui::Button(known_tabs[t], ImVec2(130, 24))) known_subtab = t;
            if (known_subtab == t) ImGui::PopStyleColor(3);
        }

        if (ImGui::BeginChild("##known_objects", ImVec2(0, 0), true)) {
            struct KnownPreviewOption {
                const char* label;
                int type;
                float mass;
                bool system_preview;
                int planet_look;
                bool rings;
                bool moons;
                int moon_count;
            };
            static const KnownPreviewOption PREV_BASIC[] = {
                {"Sun (Sol)", CTYPE_STAR_G, 1.0f, false, 0, false, false, 0},
                {"Earth-like Planet", CTYPE_PLANET, 3.003e-6f, false, 4, false, true, 1},
                {"Moon", CTYPE_MOON, 3.70e-8f, false, 2, false, false, 0},
                {"Asteroid", CTYPE_ASTEROID, 2.0e-10f, false, 1, false, false, 0},
                {"Comet", CTYPE_COMET, 8.0e-11f, false, 3, false, false, 0},
                {"Dust", CTYPE_DUST, 5.0e-12f, false, 1, false, false, 0},
                {"Our Solar System", CTYPE_PLANET, 3.003e-6f, true, 4, true, true, 4},
            };
            static const KnownPreviewOption PREV_STARS[] = {
                {"Sirius A", CTYPE_STAR_A, 2.063f, false, 0, false, false, 0},
                {"Proxima Centauri", CTYPE_STAR_M, 0.122f, false, 0, false, false, 0},
                {"Betelgeuse", CTYPE_STAR_M, 16.5f, false, 0, false, false, 0},
                {"Vega", CTYPE_STAR_A, 2.135f, false, 0, false, false, 0},
            };
            static const KnownPreviewOption PREV_BH[] = {
                {"Cygnus X-1", CTYPE_BH_STELLAR, 21.0f, false, 0, false, false, 0},
                {"Sagittarius A*", CTYPE_BH_SUPERMASSIVE, 4.297e6f, false, 0, false, false, 0},
                {"M87*", CTYPE_BH_SUPERMASSIVE, 6.5e9f, false, 0, false, false, 0},
                {"TON 618", CTYPE_BH_SUPERMASSIVE, 6.6e10f, false, 0, false, false, 0},
            };
            static const KnownPreviewOption PREV_SYSTEM[] = {
                {"Our Solar System", CTYPE_PLANET, 3.003e-6f, true, 4, true, true, 5},
                {"Jupiter System", CTYPE_PLANET, 9.5458e-4f, true, 5, true, true, 4},
                {"Saturn System", CTYPE_PLANET, 2.858e-4f, true, 5, true, true, 3},
                {"Neptune System", CTYPE_PLANET, 5.151e-5f, true, 5, false, true, 2},
            };
            const KnownPreviewOption* preview_list = PREV_BASIC;
            int preview_count = (int)IM_ARRAYSIZE(PREV_BASIC);
            if (known_subtab == 1) {
                preview_list = PREV_STARS;
                preview_count = (int)IM_ARRAYSIZE(PREV_STARS);
            } else if (known_subtab == 2) {
                preview_list = PREV_BH;
                preview_count = (int)IM_ARRAYSIZE(PREV_BH);
            } else if (known_subtab == 3) {
                preview_list = PREV_SYSTEM;
                preview_count = (int)IM_ARRAYSIZE(PREV_SYSTEM);
            }
            int& kp = known_preview_choice[std::clamp(known_subtab, 0, 3)];
            kp = std::clamp(kp, 0, std::max(preview_count - 1, 0));
            ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Known Object Preview");
            ImGui::Combo("Preview Target##Known", &kp,
                         [](void* data, int idx, const char** out_text) {
                             auto* arr = static_cast<const KnownPreviewOption*>(data);
                             *out_text = arr[idx].label;
                             return true;
                         },
                         (void*)preview_list, preview_count);
            if (ImGui::Button("Regenerate Preview##Known", ImVec2(-1, 0)))
                known_preview_seed = hash_combine(known_preview_seed, (uint32_t)(sim_time_ * 1000.0f) + 0xA511E9B3u);
            SpawnPreviewStyle known_pv{};
            known_pv.system_preview = preview_list[kp].system_preview;
            known_pv.planet_look = preview_list[kp].planet_look;
            known_pv.spawn_rings = preview_list[kp].rings;
            known_pv.spawn_moons = preview_list[kp].moons;
            known_pv.moon_count = preview_list[kp].moon_count;
            known_pv.moon_layout = 0;
            known_pv.moon_inclination_deg = 8.0f;
            known_pv.moon_spacing_scale = 1.0f;
            draw_spawn_preview_thumb("##known_preview_thumb", preview_list[kp].type,
                                     preview_list[kp].mass, known_preview_seed, known_pv);
            ImGui::Separator();

            if (known_subtab == 0) {
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Known Objects - Basic");
                ImGui::TextWrapped("Physical mass/radius values are based on real-world references; "
                                   "orbits are generated around parent bodies with true ordering.");
                if (ImGui::Button("Spawn Sun (Sol)", ImVec2(-1, 28))) {
                    CelestialBody sun = make_body("Sun", CTYPE_STAR_G, 1.0f, 696340.0f, 5778.0f,
                                                  26.0f * 24.0f, SSTAGE_MAIN_SEQUENCE, 0.72f);
                    sun.pos = camera.target;
                    sun.vel = glm::vec3(0.0f);
                    append_body(std::move(sun));
                }
                if (ImGui::Button("Spawn Solar System (Sun + Planets + Major Moons)", ImVec2(-1, 28)))
                    spawn_solar_system(false);
                if (ImGui::Button("Spawn Solar System + Kuiper Belt + Oort Cloud", ImVec2(-1, 28)))
                    spawn_solar_system(true);
                ImGui::Separator();
                ImGui::SliderInt("Kuiper Belt Count", &kuiper_count, 64, 1600);
                ImGui::SliderInt("Oort Cloud Count", &oort_count, 128, 2400);
                if (ImGui::Button("Spawn Kuiper Belt Around Nearest Star", ImVec2(-1, 26))) {
                    int sun_idx = find_star_or_create_sun();
                    spawn_kuiper_belt(sun_idx, kuiper_count);
                }
                if (ImGui::Button("Spawn Oort Cloud Around Nearest Star", ImVec2(-1, 26))) {
                    int sun_idx = find_star_or_create_sun();
                    spawn_oort_cloud(sun_idx, oort_count);
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Named Moon Systems");
                if (ImGui::Button("Jupiter System (Galilean Moons)", ImVec2(-1, 26)))
                    spawn_galilean_system();
                if (ImGui::Button("Saturn System (Titan + Major Moons)", ImVec2(-1, 26)))
                    spawn_saturn_system();
                if (ImGui::Button("Uranus System (Major Moons)", ImVec2(-1, 26)))
                    spawn_uranus_system();
                if (ImGui::Button("Neptune System (Triton + Nereid)", ImVec2(-1, 26)))
                    spawn_neptune_system();
                if (ImGui::Button("Pluto System (Charon + Outer Moons)", ImVec2(-1, 26)))
                    spawn_pluto_system();
            } else if (known_subtab == 1) {
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Known Objects - Stars");
                if (ImGui::Button("Sirius A", ImVec2(-1, 26))) {
                    CelestialBody s = make_body("Sirius A", CTYPE_STAR_A, 2.063f, 1189640.0f, 9940.0f,
                                                120.0f, SSTAGE_MAIN_SEQUENCE, 0.68f);
                    s.pos = camera.target;
                    append_body(std::move(s));
                }
                if (ImGui::Button("Proxima Centauri", ImVec2(-1, 26))) {
                    CelestialBody s = make_body("Proxima Centauri", CTYPE_STAR_M, 0.122f, 107280.0f, 3042.0f,
                                                2000.0f, SSTAGE_MAIN_SEQUENCE, 0.82f);
                    s.pos = camera.target;
                    append_body(std::move(s));
                }
                if (ImGui::Button("Betelgeuse", ImVec2(-1, 26))) {
                    CelestialBody s = make_body("Betelgeuse", CTYPE_STAR_M, 16.5f, 617000000.0f, 3500.0f,
                                                1500.0f, SSTAGE_RED_GIANT, 0.20f);
                    s.pos = camera.target;
                    append_body(std::move(s));
                }
                if (ImGui::Button("Vega", ImVec2(-1, 26))) {
                    CelestialBody s = make_body("Vega", CTYPE_STAR_A, 2.135f, 1099000.0f, 9602.0f,
                                                12.5f, SSTAGE_MAIN_SEQUENCE, 0.60f);
                    s.pos = camera.target;
                    append_body(std::move(s));
                }
            } else if (known_subtab == 2) {
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Known Objects - Black Holes");
                if (ImGui::Button("Cygnus X-1", ImVec2(-1, 26))) {
                    CelestialBody bh = make_body("Cygnus X-1", CTYPE_BH_STELLAR, 21.0f, 120.0f, 0.0f,
                                                 0.0f, SSTAGE_NEUTRON_STAR, 0.0f);
                    bh.pos = camera.target;
                    append_body(std::move(bh));
                }
                if (ImGui::Button("Sagittarius A*", ImVec2(-1, 26))) {
                    CelestialBody bh = make_body("Sagittarius A*", CTYPE_BH_SUPERMASSIVE, 4.297e6f, 2000.0f, 0.0f,
                                                 0.0f, SSTAGE_NEUTRON_STAR, 0.0f);
                    bh.pos = camera.target;
                    append_body(std::move(bh));
                }
                if (ImGui::Button("M87*", ImVec2(-1, 26))) {
                    CelestialBody bh = make_body("M87*", CTYPE_BH_SUPERMASSIVE, 6.5e9f, 4000.0f, 0.0f,
                                                 0.0f, SSTAGE_NEUTRON_STAR, 0.0f);
                    bh.pos = camera.target;
                    append_body(std::move(bh));
                }
                if (ImGui::Button("TON 618", ImVec2(-1, 26))) {
                    CelestialBody bh = make_body("TON 618", CTYPE_BH_SUPERMASSIVE, 6.6e10f, 5000.0f, 0.0f,
                                                 0.0f, SSTAGE_NEUTRON_STAR, 0.0f);
                    bh.pos = camera.target;
                    append_body(std::move(bh));
                }
            } else {
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Known Objects - System");
                ImGui::TextWrapped("System-level one-click presets built from the Known Objects catalog.");
                if (ImGui::Button("Our Solar System (Known Objects)", ImVec2(-1, 30)))
                    spawn_solar_system(false);
                if (ImGui::Button("Our Solar System + Kuiper Belt + Oort Cloud", ImVec2(-1, 30)))
                    spawn_solar_system(true);
                ImGui::Separator();
                if (ImGui::Button("Add Jupiter System to Current Scene", ImVec2(-1, 26)))
                    spawn_galilean_system();
                if (ImGui::Button("Add Saturn System to Current Scene", ImVec2(-1, 26)))
                    spawn_saturn_system();
                if (ImGui::Button("Add Uranus System to Current Scene", ImVec2(-1, 26)))
                    spawn_uranus_system();
                if (ImGui::Button("Add Neptune System to Current Scene", ImVec2(-1, 26)))
                    spawn_neptune_system();
                if (ImGui::Button("Add Pluto System to Current Scene", ImVec2(-1, 26)))
                    spawn_pluto_system();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild(); // main_content
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    if (catalog_tab == 4) {
        static int attach_host = -1;
        static int attach_moon_count = 2;
        static int attach_moon_layout = 0;
        static float attach_moon_incl = 8.0f;
        static float attach_moon_spacing = 1.0f;
        static float attach_ring_inner = 1.6f;
        static float attach_ring_outer = 3.2f;
        static float attach_ring_density = 0.35f;
        static float attach_ring_ice = 0.55f;
        static uint32_t existing_preview_seed = 0x51A8E22Du;

        std::vector<int> host_indices;
        std::vector<std::string> host_labels;
        host_indices.reserve(state.bodies.size());
        host_labels.reserve(state.bodies.size());
        for (int i = 0; i < (int)state.bodies.size(); ++i) {
            const auto& b = state.bodies[(size_t)i];
            if (b.marked_for_removal) continue;
            if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;
            host_indices.push_back(i);
            const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Body";
            std::string name = b.name.empty() ? std::string(type_name) : b.name;
            host_labels.push_back(name + " (" + type_name + ")");
        }

        if (!host_indices.empty()) {
            bool found = false;
            for (int idx : host_indices) {
                if (idx == attach_host) { found = true; break; }
            }
            if (!found) attach_host = host_indices.front();
        } else {
            attach_host = -1;
        }

        ImGui::Separator();
        if (ImGui::BeginChild("##attach_tools", ImVec2(0, 0), true)) {
            ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Spawn On Existing Objects");
            ImGui::TextColored(ImVec4(0.62f, 0.60f, 0.56f, 1.0f),
                               "Pick a host body, then add moons or a dust ring.");
            if (host_indices.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                   "No eligible host bodies found.");
            } else {
                int current_host_choice = 0;
                for (int k = 0; k < (int)host_indices.size(); ++k) {
                    if (host_indices[(size_t)k] == attach_host) { current_host_choice = k; break; }
                }
                const char* preview = host_labels[(size_t)current_host_choice].c_str();
                if (ImGui::BeginCombo("Host Body", preview)) {
                    for (int k = 0; k < (int)host_indices.size(); ++k) {
                        bool selected = (host_indices[(size_t)k] == attach_host);
                        if (ImGui::Selectable(host_labels[(size_t)k].c_str(), selected))
                            attach_host = host_indices[(size_t)k];
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const CelestialBody* host = (attach_host >= 0 && attach_host < (int)state.bodies.size())
                    ? &state.bodies[(size_t)attach_host] : nullptr;
                bool can_add_moons = (host != nullptr && host->type == CTYPE_PLANET);
                bool can_add_ring = (host != nullptr &&
                    (host->type == CTYPE_PLANET || host->type == CTYPE_MOON || host->type == CTYPE_NEBULA));

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Existing Object Preview");
                if (ImGui::Button("Regenerate Preview##Existing", ImVec2(-1, 0)))
                    existing_preview_seed = hash_combine(existing_preview_seed, (uint32_t)(sim_time_ * 1000.0f) + 0x6D2B79F5u);
                SpawnPreviewStyle existing_pv{};
                existing_pv.planet_look = 0;
                existing_pv.spawn_moons = can_add_moons;
                existing_pv.moon_count = attach_moon_count;
                existing_pv.moon_layout = attach_moon_layout;
                existing_pv.moon_inclination_deg = attach_moon_incl;
                existing_pv.moon_spacing_scale = attach_moon_spacing;
                existing_pv.spawn_rings = can_add_ring;
                int preview_type = host ? (int)host->type : CTYPE_PLANET;
                float preview_mass = host ? host->mass : 3.003e-6f;
                draw_spawn_preview_thumb("##existing_preview_thumb", preview_type,
                                         preview_mass, existing_preview_seed, existing_pv);

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Moons");
                ImGui::SliderInt("Moon Count", &attach_moon_count, 1, 100);
                const char* moon_layouts[] = {"Prograde Disk", "Compact Disk", "Wide Disk", "Resonant Chain", "Isotropic Cloud"};
                ImGui::Combo("Orbit Layout", &attach_moon_layout, moon_layouts, IM_ARRAYSIZE(moon_layouts));
                ImGui::SliderFloat("Inclination Deg", &attach_moon_incl, 0.0f, 85.0f, "%.1f");
                ImGui::SliderFloat("Spacing Scale", &attach_moon_spacing, 0.35f, 4.0f, "%.2f");
                if (!can_add_moons) ImGui::BeginDisabled();
                if (ImGui::Button("Spawn Moons on Host", ImVec2(-1, 28))) {
                    spawn_moons_for_host(attach_host, attach_moon_count, attach_moon_layout,
                                         attach_moon_incl, attach_moon_spacing);
                }
                if (!can_add_moons) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                       "Moons can be attached to planets only.");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Rings");
                ImGui::SliderFloat("Ring Inner xR", &attach_ring_inner, 1.15f, 4.0f, "%.2f");
                ImGui::SliderFloat("Ring Outer xR", &attach_ring_outer, 1.5f, 8.0f, "%.2f");
                ImGui::SliderFloat("Ring Density", &attach_ring_density, 0.01f, 1.0f, "%.2f");
                ImGui::SliderFloat("Ring Ice Fraction", &attach_ring_ice, 0.0f, 1.0f, "%.2f");
                if (!can_add_ring) ImGui::BeginDisabled();
                if (ImGui::Button("Spawn Ring on Host", ImVec2(-1, 28))) {
                    spawn_ring_for_host(attach_host, attach_ring_inner, attach_ring_outer,
                                        attach_ring_density, attach_ring_ice);
                }
                if (!can_add_ring) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                       "Rings can be attached to planets, moons, or nebulae.");
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild(); // main_content
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    // ── Body selection grid (tabs 0-2) ──
    // Header
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
    ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.66f, 1.0f), "%s", catalog_tab == 0 ? "Bodies" : (catalog_tab == 1 ? "Stars" : "Black Holes"));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.52f, 1.0f), "  Click to select, then configure properties on the right.");
    ImGui::Dummy(ImVec2(0, 2));

    // Body card grid - left side
    float props_panel_w = 260.0f;
    float grid_w = ImGui::GetContentRegionAvail().x - props_panel_w - 8;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
    if (ImGui::BeginChild("##body_grid", ImVec2(grid_w, 0), false)) {
    // Grid of body cards
    float card_w = 120.0f;
    float card_h = 52.0f;
    float padding = 6.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + padding) / (card_w + padding)));

    for (int i = 0; i < active_count; ++i) {
        const int t = active_list[i].type;
        const float default_mass = active_list[i].mass;
        const char* desc = active_list[i].desc;
        bool sel = (spawn_type == t);

        if (i % cols != 0) ImGui::SameLine(0, padding);

        ImU32 col = CTYPE_COLORS[t];
        float cr = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float cg = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float cb = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

        ImGui::PushID(t);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        // Card background
        ImVec4 card_bg = sel
            ? ImVec4(cr * 0.28f, cg * 0.28f, cb * 0.28f, 0.95f)
            : ImVec4(0.12f, 0.12f, 0.14f, 0.90f);
        ImGui::PushStyleColor(ImGuiCol_Button, card_bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(cr * 0.22f + 0.06f, cg * 0.22f + 0.06f, cb * 0.22f + 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(cr * 0.30f + 0.08f, cg * 0.30f + 0.08f, cb * 0.30f + 0.08f, 1.0f));

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##card", ImVec2(card_w, card_h))) {
            spawn_type = t;
            spawn_mass = default_mass;
        }
        ImGui::PopStyleColor(3);

        // Draw card content over the button
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Color accent bar on left edge
        ImU32 accent = IM_COL32((int)(cr * 255), (int)(cg * 255), (int)(cb * 255), sel ? 255 : 140);
        dl->AddRectFilled(ImVec2(cursor.x, cursor.y + 3), ImVec2(cursor.x + 3, cursor.y + card_h - 3), accent, 2.0f);

        // Selection highlight border
        if (sel) {
            dl->AddRect(cursor, ImVec2(cursor.x + card_w, cursor.y + card_h),
                        IM_COL32((int)(cr * 200), (int)(cg * 200), (int)(cb * 200), 200), 6.0f, 0, 2.0f);
        }

        // Type name
        dl->AddText(ImVec2(cursor.x + 8, cursor.y + 5),
                    IM_COL32(240, 235, 225, sel ? 255 : 200), CTYPE_NAMES[t]);

        // Mass
        char mass_str[32];
        snprintf(mass_str, sizeof(mass_str), "%.2e M\xe2\x98\x89", default_mass);
        dl->AddText(ImVec2(cursor.x + 8, cursor.y + 20),
                    IM_COL32(160, 155, 140, sel ? 220 : 160), mass_str);

        // Description
        dl->AddText(ImVec2(cursor.x + 8, cursor.y + 35),
                    IM_COL32(120, 120, 130, sel ? 180 : 120), desc);

        ImGui::PopStyleVar();
        ImGui::PopID();
    }

    // ── Spawn button at bottom of grid ──
    ImGui::Dummy(ImVec2(0, 8));
    ImU32 spawn_col = CTYPE_COLORS[spawn_type % CTYPE_COUNT];
    float sr = (float)((spawn_col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    float sg = (float)((spawn_col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    float sb = (float)((spawn_col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(sr * 0.35f + 0.05f, sg * 0.35f + 0.05f, sb * 0.35f + 0.05f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(sr * 0.50f + 0.08f, sg * 0.50f + 0.08f, sb * 0.50f + 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(sr * 0.65f + 0.10f, sg * 0.65f + 0.10f, sb * 0.65f + 0.10f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    char label[64];
    bool small_type = (spawn_type == CTYPE_ASTEROID || spawn_type == CTYPE_COMET || spawn_type == CTYPE_DUST);
    int batch_count = std::clamp(spawn_draft_.small_body_spawn_count, 1, 1000);
    if (small_type && batch_count > 1)
        snprintf(label, sizeof(label), "Spawn x%d %s", batch_count, CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
    else
        snprintf(label, sizeof(label), "Spawn %s", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);

    if (ImGui::Button(label, ImVec2(-1, 28))) {
        int spawned = spawn_preview_body(camera.target);
        if (spawned >= 0 && spawned < (int)state.bodies.size()) {
            const auto& b = state.bodies[spawned];
            selected_body = spawned;
            camera.focus_on(b.pos, spawned, b.radius);
            camera.target_distance = std::max(b.radius * 8.0f, 30.0f);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.48f, 1.0f), "Or left-click empty space to place at cursor");
    }
    ImGui::EndChild(); // body_grid

    // ── Right properties panel ──
    ImGui::SameLine(0, 8);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 0.95f));
    if (ImGui::BeginChild("##props_panel", ImVec2(props_panel_w, 0), true, ImGuiWindowFlags_NoScrollbar)) {
        // ── Properties header ──
        ImU32 hdr_col = CTYPE_COLORS[spawn_type % CTYPE_COUNT];
        float hr = (float)((hdr_col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float hg = (float)((hdr_col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float hb = (float)((hdr_col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        ImGui::TextColored(ImVec4(hr, hg, hb, 1.0f), "%s", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.52f, 1.0f), "Properties");
        ImGui::Separator();

        // ── Mass & Dynamics ──
        ImGui::TextColored(ImVec4(0.75f, 0.72f, 0.65f, 1.0f), "Mass & Dynamics");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##Mass", &spawn_mass, 1.0e-13f, 500.0f, "%.3e M\xe2\x98\x89", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Orbital Velocity", &spawn_in_orbit_);
        {
        bool small_body = (spawn_type == CTYPE_ASTEROID || spawn_type == CTYPE_COMET || spawn_type == CTYPE_DUST);
        if (small_body) {
            ImGui::SliderInt("Count", &spawn_draft_.small_body_spawn_count, 1, 1000);
            const char* layouts[] = {"Random", "Sphere", "Cube", "Torus"};
            ImGui::Combo("Layout", &spawn_draft_.small_body_layout, layouts, IM_ARRAYSIZE(layouts));
        }
        }

        // ── Overrides (collapsible) ──
        if (ImGui::TreeNode("Overrides")) {
            ImGui::Checkbox("Temperature", &spawn_draft_.override_temperature);
            if (spawn_draft_.override_temperature)
                ImGui::SliderFloat("##TempK", &spawn_draft_.temperature, 2.7f, 8000.0f, "%.0f K");
            ImGui::Checkbox("Radius", &spawn_draft_.override_radius);
            if (spawn_draft_.override_radius)
                ImGui::SliderFloat("##Rad", &spawn_draft_.radius, 0.04f, 120.0f, "%.2f");
            ImGui::Checkbox("Rotation", &spawn_draft_.override_rotation);
            if (spawn_draft_.override_rotation)
                ImGui::SliderFloat("##RotH", &spawn_draft_.rotation_hours, 0.1f, 2000.0f, "%.1f hrs");
            ImGui::Checkbox("Velocity", &spawn_draft_.override_velocity);
            if (spawn_draft_.override_velocity) {
                ImGui::SliderFloat("Vx", &spawn_draft_.velocity_kms.x, -200.0f, 200.0f, "%.1f km/s");
                ImGui::SliderFloat("Vy", &spawn_draft_.velocity_kms.y, -200.0f, 200.0f, "%.1f km/s");
                ImGui::SliderFloat("Vz", &spawn_draft_.velocity_kms.z, -200.0f, 200.0f, "%.1f km/s");
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.75f, 0.72f, 0.65f, 1.0f), "Appearance");
        if (spawn_type == CTYPE_PLANET || spawn_type == CTYPE_MOON) {
            const char* looks[] = {"Auto", "Rocky", "Water", "Ice", "Earth-like", "Gas Giant"};
            ImGui::Combo("Planet Look", &spawn_draft_.planet_look, looks, IM_ARRAYSIZE(looks));
            ImGui::Checkbox("Add Moons", &spawn_draft_.spawn_moons);
            if (spawn_draft_.spawn_moons) {
                ImGui::SliderInt("Moon Count", &spawn_draft_.moon_count, 1, 100);
                const char* moon_layouts[] = {"Prograde Disk", "Compact Disk", "Wide Disk", "Resonant Chain", "Isotropic Cloud"};
                ImGui::Combo("Moon Orbit Layout", &spawn_draft_.moon_orbit_layout, moon_layouts, IM_ARRAYSIZE(moon_layouts));
                ImGui::SliderFloat("Moon Inclination", &spawn_draft_.moon_inclination_deg, 0.0f, 85.0f, "%.1f deg");
                ImGui::SliderFloat("Moon Spacing", &spawn_draft_.moon_spacing_scale, 0.35f, 4.0f, "%.2f");
            }
            ImGui::Checkbox("Add Rings", &spawn_draft_.spawn_rings);
            if (spawn_draft_.spawn_rings) {
                ImGui::Checkbox("Override Ring Layout", &spawn_draft_.override_ring_layout);
                if (spawn_draft_.override_ring_layout) {
                    const char* ring_layouts[] = {
                        "Saturn", "Uranus", "Neptune", "Torus",
                        "Realistic Disk", "Unrealistic Geometries", "Resonance Gaps"
                    };
                    auto apply_ring_preset = [&]() {
                        switch (std::clamp(spawn_draft_.ring_layout_type, 0, 6)) {
                        case 0: // Saturn
                            spawn_draft_.ring_inner_mult = 1.24f;
                            spawn_draft_.ring_outer_mult = 2.48f;
                            spawn_draft_.ring_density = 0.82f;
                            spawn_draft_.ring_ice_fraction = 0.84f;
                            break;
                        case 1: // Uranus
                            spawn_draft_.ring_inner_mult = 1.68f;
                            spawn_draft_.ring_outer_mult = 2.16f;
                            spawn_draft_.ring_density = 0.26f;
                            spawn_draft_.ring_ice_fraction = 0.58f;
                            break;
                        case 2: // Neptune
                            spawn_draft_.ring_inner_mult = 2.15f;
                            spawn_draft_.ring_outer_mult = 2.90f;
                            spawn_draft_.ring_density = 0.14f;
                            spawn_draft_.ring_ice_fraction = 0.50f;
                            break;
                        case 3: // Torus
                            spawn_draft_.ring_inner_mult = 1.92f;
                            spawn_draft_.ring_outer_mult = 2.58f;
                            spawn_draft_.ring_density = 0.95f;
                            spawn_draft_.ring_ice_fraction = 0.36f;
                            break;
                        case 4: // Realistic disk
                            spawn_draft_.ring_inner_mult = 1.50f;
                            spawn_draft_.ring_outer_mult = 3.20f;
                            spawn_draft_.ring_density = 0.42f;
                            spawn_draft_.ring_ice_fraction = 0.62f;
                            break;
                        case 5: // Unrealistic geometries
                            spawn_draft_.ring_inner_mult = 1.15f;
                            spawn_draft_.ring_outer_mult = 4.80f;
                            spawn_draft_.ring_density = 0.92f;
                            spawn_draft_.ring_ice_fraction = 0.12f;
                            break;
                        case 6: // Resonance gaps
                        default:
                            spawn_draft_.ring_inner_mult = 1.36f;
                            spawn_draft_.ring_outer_mult = 3.65f;
                            spawn_draft_.ring_density = 0.56f;
                            spawn_draft_.ring_ice_fraction = 0.58f;
                            break;
                        }
                    };
                    int prev_ring_layout = spawn_draft_.ring_layout_type;
                    ImGui::Combo("Ring Type", &spawn_draft_.ring_layout_type, ring_layouts, IM_ARRAYSIZE(ring_layouts));
                    if (spawn_draft_.ring_layout_type != prev_ring_layout)
                        apply_ring_preset();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Apply Preset"))
                        apply_ring_preset();
                    ImGui::SliderFloat("Ring Inner xR", &spawn_draft_.ring_inner_mult, 1.15f, 4.0f, "%.2f");
                    ImGui::SliderFloat("Ring Outer xR", &spawn_draft_.ring_outer_mult, 1.5f, 8.0f, "%.2f");
                    ImGui::SliderFloat("Ring Density", &spawn_draft_.ring_density, 0.01f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Ring Ice", &spawn_draft_.ring_ice_fraction, 0.0f, 1.0f, "%.2f");
                }
            }
        } else {
            spawn_draft_.planet_look = 0;
            spawn_draft_.spawn_moons = false;
            spawn_draft_.spawn_rings = false;
            spawn_draft_.moon_orbit_layout = 0;
            spawn_draft_.moon_inclination_deg = 8.0f;
            spawn_draft_.moon_spacing_scale = 1.0f;
        }

        if (is_star_type((uint32_t)spawn_type)) {
            const char* stage_modes[] = {
                "Auto",
                "Main Sequence",
                "Subgiant",
                "Red Giant",
                "Horizontal Branch",
                "AGB",
                "Supergiant",
                "Hypergiant",
                "White Dwarf",
                "Neutron Star"
            };
            int stage_ui = std::clamp(spawn_draft_.star_stage_hint + 1, 0, (int)IM_ARRAYSIZE(stage_modes) - 1);
            if (ImGui::Combo("Star Variant", &stage_ui, stage_modes, IM_ARRAYSIZE(stage_modes)))
                spawn_draft_.star_stage_hint = stage_ui - 1;
        } else {
            spawn_draft_.star_stage_hint = -1;
        }

        if (spawn_type == CTYPE_NEBULA) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Nebula Render Path");
            int mode = std::clamp(cfg.nebula_render_mode, 0, 2);
            bool m0 = (mode == 0);
            bool m1 = (mode == 1);
            bool m2 = (mode == 2);
            if (ImGui::Checkbox("Volumetric Raymarching", &m0) && m0) mode = 0;
            if (ImGui::Checkbox("Volumetric Raymarching + Compute Shaders", &m1) && m1) mode = 1;
            if (ImGui::Checkbox("Advanced Particle Systems", &m2) && m2) mode = 2;
            cfg.nebula_render_mode = mode;
            ImGui::TextColored(ImVec4(0.60f, 0.70f, 0.82f, 1.0f),
                               "Raymarching uses Perlin + Beer-Lambert + semi-Lagrangian advection.");
        }

        // ── Preview ──
        static uint32_t props_preview_seed = 0xC05109ADu;
        ImGui::Separator();
        if (ImGui::Button("Regenerate##Preview", ImVec2(-1, 0)))
            props_preview_seed = hash_combine(props_preview_seed, (uint32_t)(sim_time_ * 1000.0f) + 0x9E3779B9u);
        SpawnPreviewStyle props_pv{};
        props_pv.planet_look = spawn_draft_.planet_look;
        props_pv.spawn_rings = spawn_draft_.spawn_rings && (spawn_type == CTYPE_PLANET || spawn_type == CTYPE_MOON);
        props_pv.spawn_moons = spawn_draft_.spawn_moons && (spawn_type == CTYPE_PLANET || spawn_type == CTYPE_MOON);
        props_pv.moon_count = spawn_draft_.moon_count;
        props_pv.moon_layout = spawn_draft_.moon_orbit_layout;
        props_pv.moon_inclination_deg = spawn_draft_.moon_inclination_deg;
        props_pv.moon_spacing_scale = spawn_draft_.moon_spacing_scale;
        draw_spawn_preview_thumb("##spawn_preview_thumb", spawn_type, spawn_mass, props_preview_seed, props_pv);

        // ── Material ──
        if (ImGui::TreeNode("Material")) {
            ImGui::Checkbox("Override", &spawn_draft_.override_material);
            if (spawn_draft_.override_material) {
                ImGui::SliderFloat("Iron", &spawn_draft_.material_iron, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Silicate", &spawn_draft_.material_silicate, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Ice/Water", &spawn_draft_.material_ice, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("H2", &spawn_draft_.material_hydrogen, 0.0f, 1.0f, "%.2f");
                float total = spawn_draft_.material_iron + spawn_draft_.material_silicate +
                              spawn_draft_.material_ice + spawn_draft_.material_hydrogen;
                if (total > 1.0e-6f)
                    ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f), "Sum: %.2f", total);
                if (ImGui::SmallButton("Normalize"))  {
                    float s = std::max(total, 1.0e-6f);
                    spawn_draft_.material_iron /= s;
                    spawn_draft_.material_silicate /= s;
                    spawn_draft_.material_ice /= s;
                    spawn_draft_.material_hydrogen /= s;
                }
            }
            ImGui::TreePop();
        }

        // ── Quick Presets ──
        if (ImGui::TreeNode("Quick Presets")) {
            auto star_luminosity = [&](float mass, float radius_sim, float temperature_k) {
                float r_solar = std::max(radius_sim / (EARTH_RADIUS_SIM_UNITS * 109.1f), 1.0e-4f);
                float stefan = r_solar * r_solar * std::pow(std::max(temperature_k, 100.0f) / 5778.0f, 4.0f);
                float mass_law = (mass < 0.43f)
                    ? 0.23f * std::pow(std::max(mass, 0.01f), 2.3f)
                    : (mass < 2.0f ? std::pow(mass, 4.0f)
                                   : (mass < 20.0f ? 1.5f * std::pow(mass, 3.5f)
                                                   : 3200.0f * std::pow(std::max(mass, 20.0f) / 20.0f, 2.2f)));
                return std::max(stefan, mass_law);
            };

            if (ImGui::Button("Solar System", ImVec2(-1, 0))) {
                glm::vec3 offset = camera.target;
                CelestialBody s;
                s.pos = offset; s.mass = 1.0f; s.radius = 696340.0f / SIM_UNIT_TO_KM;
                s.temperature = 5778.0f; s.type = classify_star_spectral(5778.0f, 1.0f);
                s.seed = 42;
                s.fuel = 0.72f;
                s.angular_vel = (2.0f * 3.14159265359f) / (26.0f * 24.0f * 3600.0f);
                s.luminosity = star_luminosity(s.mass, s.radius, s.temperature);
                s.name = generate_body_name(s.seed, s.type);
                int star_idx = (int)state.bodies.size();
                state.bodies.push_back(s); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);

                float radii[] = {80, 140, 210, 300};
                float masses[] = {1.66e-7f, 3.00e-6f, 3.22e-7f, 9.54e-4f};
                float temps[] = {600.0f, 300.0f, 180.0f, 90.0f};
                for (int i = 0; i < 4; i++) {
                    CelestialBody p;
                    float angle = (float)i * 1.57f;
                    p.pos = offset + glm::vec3(cosf(angle) * radii[i], 0, sinf(angle) * radii[i]);
                    float v = std::sqrt(cfg.G * s.mass / radii[i]);
                    p.vel = s.vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                    p.mass = masses[i]; p.radius = 6 + masses[i] * 3;
                    p.temperature = temps[i];
                    p.type = CTYPE_PLANET; p.parent = star_idx;
                    p.seed = (uint32_t)(i * 31337 + 54321);
                    p.name = generate_body_name(p.seed, p.type);
                    refresh_body_render_state(p, &state);
                    state.bodies.push_back(p); state.trails.emplace_back();
                }
            }

            if (ImGui::Button("Binary Stars", ImVec2(-1, 0))) {
                glm::vec3 center = camera.target;
                float sep = 60.0f;
                float v = std::sqrt(cfg.G * 50.0f / sep);

                CelestialBody s1;
                s1.pos = center + glm::vec3(sep * 0.5f, 0, 0);
                s1.vel = glm::vec3(0, 0, v * 0.5f);
                s1.mass = 50.0f; s1.radius = 22.0f; s1.temperature = 8000.0f;
                s1.type = classify_star_spectral(8000.0f, 50.0f); s1.seed = 111;
                s1.fuel = 0.68f;
                s1.angular_vel = (2.0f * 3.14159265359f) / (38.0f * 3600.0f);
                s1.luminosity = star_luminosity(s1.mass, s1.radius, s1.temperature);
                s1.name = generate_body_name(s1.seed, s1.type);
                state.bodies.push_back(s1); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);

                CelestialBody s2;
                s2.pos = center - glm::vec3(sep * 0.5f, 0, 0);
                s2.vel = glm::vec3(0, 0, -v * 0.5f);
                s2.mass = 50.0f; s2.radius = 22.0f; s2.temperature = 3500.0f;
                s2.type = classify_star_spectral(3500.0f, 50.0f); s2.seed = 222;
                s2.fuel = 0.62f;
                s2.angular_vel = (2.0f * 3.14159265359f) / (84.0f * 3600.0f);
                s2.luminosity = star_luminosity(s2.mass, s2.radius, s2.temperature);
                s2.name = generate_body_name(s2.seed, s2.type);
                state.bodies.push_back(s2); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);
            }

            if (ImGui::Button("Asteroid Belt", ImVec2(-1, 0))) {
                std::mt19937 rng((unsigned)sim_time_);
                auto randf = [&](float lo, float hi) {
                    return std::uniform_real_distribution<float>(lo, hi)(rng);
                };
                float nearest_mass = 1.0f;
                glm::vec3 nearest_pos = camera.target;
                glm::vec3 nearest_vel(0);
                for (auto& b : state.bodies) {
                    if (is_star_type(b.type)) {
                        float d = glm::length(b.pos - camera.target);
                        if (d < 800.0f) {
                            nearest_mass = b.mass;
                            nearest_pos = b.pos;
                            nearest_vel = b.vel;
                        }
                    }
                }
                for (int i = 0; i < 30; i++) {
                    CelestialBody a;
                    float r = randf(400.0f, 500.0f);
                    float angle = randf(0, 6.2832f);
                    a.pos = nearest_pos + glm::vec3(cosf(angle) * r, randf(-10, 10), sinf(angle) * r);
                    float v = std::sqrt(cfg.G * nearest_mass / r) * randf(0.9f, 1.1f);
                    a.vel = nearest_vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                    a.mass = randf(5.0e-11f, 3.0e-9f); a.radius = randf(0.4f, 1.6f);
                    a.type = CTYPE_ASTEROID;
                    a.seed = (uint32_t)(rng());
                    a.name = generate_body_name(a.seed, a.type);
                    state.bodies.push_back(a); state.trails.emplace_back();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild(); // props_panel
    ImGui::PopStyleColor();

    ImGui::PopStyleVar(); // WindowPadding for main_content
    ImGui::EndChild(); // main_content

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
