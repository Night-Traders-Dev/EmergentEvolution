// Procedural particle texture generator — creates 128x128 RGBA PNGs for all 67 types.
// Crystal/gem-ball aesthetic with faceted surfaces, inner glow, and sparkle highlights.
// Build: g++ -O2 -std=c++17 -o gen_textures tools/gen_particle_textures.cpp -lm
// Run:   ./gen_textures   (outputs to assets/particles/)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../src/third_party/stb_image_write.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <filesystem>

static constexpr int SZ = 128;
static constexpr float CX = 63.5f;
static constexpr float CY = 63.5f;
static constexpr float PI = 3.14159265358979f;

struct Color { float r, g, b; };

// ── Noise / utility ──────────────────────────────────────────────────────────

static float fract(float x) { return x - floorf(x); }

static float hash(float x, float y) {
    float h = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return fract(h);
}

static float hash2(float x, float y) {
    float h = sinf(x * 269.5f + y * 183.3f) * 28371.2973f;
    return fract(h);
}

static float vnoise(float x, float y) {
    float ix = floorf(x), iy = floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash(ix, iy);
    float b = hash(ix + 1, iy);
    float c = hash(ix, iy + 1);
    float d = hash(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fbm(float x, float y, int octaves = 4) {
    float val = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        val += amp * vnoise(x, y);
        x *= 2.0f; y *= 2.0f;
        amp *= 0.5f;
    }
    return val;
}

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
static float smoothstep(float edge0, float edge1, float x) {
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static Color lerp_col(Color a, Color b, float t) {
    return { lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t) };
}
static Color add_col(Color a, Color b) { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
static Color scale_col(Color a, float s) { return { a.r * s, a.g * s, a.b * s }; }

// ── Image buffer ─────────────────────────────────────────────────────────────

struct Image {
    uint8_t data[SZ * SZ * 4];

    void clear() { memset(data, 0, sizeof(data)); }

    void set(int x, int y, float r, float g, float b, float a) {
        if (x < 0 || x >= SZ || y < 0 || y >= SZ) return;
        int i = (y * SZ + x) * 4;
        data[i + 0] = (uint8_t)clampf(r * 255.0f, 0, 255);
        data[i + 1] = (uint8_t)clampf(g * 255.0f, 0, 255);
        data[i + 2] = (uint8_t)clampf(b * 255.0f, 0, 255);
        data[i + 3] = (uint8_t)clampf(a * 255.0f, 0, 255);
    }

    void blend(int x, int y, float r, float g, float b, float a) {
        if (x < 0 || x >= SZ || y < 0 || y >= SZ) return;
        int i = (y * SZ + x) * 4;
        float dr = data[i + 0] / 255.0f;
        float dg = data[i + 1] / 255.0f;
        float db = data[i + 2] / 255.0f;
        float da = data[i + 3] / 255.0f;
        float oa = a + da * (1.0f - a);
        if (oa < 0.001f) return;
        float or_ = (r * a + dr * da * (1.0f - a)) / oa;
        float og = (g * a + dg * da * (1.0f - a)) / oa;
        float ob = (b * a + db * da * (1.0f - a)) / oa;
        set(x, y, or_, og, ob, oa);
    }

    bool save(const char* path) {
        return stbi_write_png(path, SZ, SZ, 4, data, SZ * 4) != 0;
    }
};

// ── Crystal/gem sphere rendering ─────────────────────────────────────────────

// Faceted crystal sphere — the primary renderer matching the example aesthetic.
// Features: noise-driven facets, multiple specular highlights, fresnel rim,
//           deep inner glow, sparkle points.
static void render_crystal(Image& img, Color col, float radius = 48.0f,
                            Color glow_col = {1.0f, 1.0f, 1.0f},
                            float facet_scale = 0.12f, float seed = 0.0f,
                            float inner_intensity = 0.5f) {
    // Light directions for multi-specular
    struct Light { float lx, ly, lz, strength, size; };
    Light lights[] = {
        { -0.45f, -0.45f, 0.77f, 0.85f, 0.20f },  // main top-left
        {  0.50f, -0.30f, 0.81f, 0.35f, 0.15f },  // secondary top-right
        { -0.20f,  0.60f, 0.77f, 0.20f, 0.12f },  // subtle bottom-left fill
        {  0.00f, -0.70f, 0.71f, 0.25f, 0.10f },  // top rim
    };

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            if (d > radius + 4.0f) continue;

            float norm_d = d / radius;

            // Anti-aliased edge
            float alpha = 1.0f - smoothstep(0.92f, 1.0f, norm_d);
            if (alpha < 0.005f) continue;

            // Surface normal (sphere)
            float nz = sqrtf(std::max(0.0f, 1.0f - norm_d * norm_d));
            float nx = dx / radius, ny = dy / radius;

            // Faceted normal perturbation — creates the crystal cut look
            float facet_n = fbm((float)x * facet_scale + seed * 3.7f,
                                 (float)y * facet_scale + seed * 5.3f, 5);
            float facet2 = vnoise((float)x * facet_scale * 2.5f + seed * 11.0f,
                                   (float)y * facet_scale * 2.5f + seed * 7.0f);
            // Perturb normal
            float pnx = nx + (facet_n - 0.5f) * 0.35f;
            float pny = ny + (facet2 - 0.5f) * 0.35f;
            float pnz = nz;
            float pnl = sqrtf(pnx * pnx + pny * pny + pnz * pnz);
            if (pnl > 0.0f) { pnx /= pnl; pny /= pnl; pnz /= pnl; }

            // Base diffuse shading from main light
            float diffuse = clampf(0.15f + 0.85f * (pnx * lights[0].lx + pny * lights[0].ly + pnz * lights[0].lz), 0.0f, 1.0f);

            // Deep crystal color: darker in the interior with rich saturation
            float depth_factor = 0.4f + 0.6f * (1.0f - norm_d);
            Color base = scale_col(col, diffuse * depth_factor);

            // Inner glow — bright core through crystal
            float ig = (1.0f - norm_d);
            ig *= ig * ig;
            base = lerp_col(base, glow_col, ig * inner_intensity);

            // Refraction-like internal color bands
            float refract = fbm(nx * 4.0f + seed * 2.1f, ny * 4.0f + seed * 3.3f, 3);
            float band = smoothstep(0.35f, 0.65f, refract) * 0.25f * (1.0f - norm_d * 0.5f);
            Color refract_col = { col.r * 1.2f, col.g * 0.9f, col.b * 1.3f };
            base = lerp_col(base, refract_col, band);

            // Multi-specular highlights — each light creates its own reflection point
            float total_spec = 0.0f;
            for (auto& L : lights) {
                // Reflect light around perturbed normal
                float dot = pnx * L.lx + pny * L.ly + pnz * L.lz;
                float rx = 2.0f * dot * pnx - L.lx;
                float ry = 2.0f * dot * pny - L.ly;
                float rz = 2.0f * dot * pnz - L.lz;
                // View direction is (0,0,1) for top-down
                float spec_dot = clampf(rz, 0.0f, 1.0f);
                float spec = powf(spec_dot, 40.0f / (L.size + 0.01f)) * L.strength;
                total_spec += spec;
            }
            base.r += total_spec;
            base.g += total_spec;
            base.b += total_spec;

            // Fresnel rim lighting — bright edge reflection
            float fresnel = powf(1.0f - nz, 3.5f);
            Color rim_col = { col.r * 0.5f + 0.5f, col.g * 0.5f + 0.5f, col.b * 0.5f + 0.5f };
            base = lerp_col(base, rim_col, fresnel * 0.55f);

            // Sparkle points — bright specular glints on facet edges
            float sparkle = vnoise((float)x * 0.4f + seed * 13.0f,
                                    (float)y * 0.4f + seed * 17.0f);
            float facet_edge = fabsf(facet_n - 0.5f) + fabsf(facet2 - 0.5f);
            float spark = smoothstep(0.7f, 0.95f, sparkle) *
                          smoothstep(0.3f, 0.6f, facet_edge) *
                          (1.0f - norm_d * 0.6f);
            base.r += spark * 1.2f;
            base.g += spark * 1.2f;
            base.b += spark * 1.2f;

            // Outer glow (beyond sphere edge)
            if (d > radius) {
                float glow_t = (d - radius) / 4.0f;
                alpha = (1.0f - glow_t) * 0.35f;
                base = scale_col(col, 0.6f);
            }

            img.set(x, y, clampf(base.r, 0, 1), clampf(base.g, 0, 1),
                    clampf(base.b, 0, 1), clampf(alpha, 0, 1));
        }
    }
}

// Crystal nucleon with quark swirl patterns (matching proton/neutron examples).
// Three spiral vortex patterns visible through the crystal surface.
static void render_crystal_nucleon(Image& img, Color col, Color q1, Color q2, Color q3,
                                    float seed = 0.0f) {
    // First render crystal base
    render_crystal(img, col, 48.0f, {1.0f, 0.95f, 0.85f}, 0.11f, seed, 0.35f);

    // Overlay quark swirl vortices
    struct Quark { float cx, cy; Color c; float phase; };
    Quark quarks[] = {
        { CX - 13.0f, CY - 11.0f, q1, 0.0f },
        { CX + 15.0f, CY - 9.0f,  q2, 2.1f },
        { CX + 1.0f,  CY + 14.0f, q3, 4.2f },
    };

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX, dy = (float)y - CY;
            float d_center = sqrtf(dx * dx + dy * dy);
            if (d_center > 46.0f) continue;

            // Depth: further from center = deeper in sphere
            float depth = sqrtf(std::max(0.0f, 1.0f - (d_center / 48.0f) * (d_center / 48.0f)));

            for (auto& q : quarks) {
                float qdx = (float)x - q.cx;
                float qdy = (float)y - q.cy;
                float qd = sqrtf(qdx * qdx + qdy * qdy);
                float qa = atan2f(qdy, qdx);

                // Spiral vortex: intensity depends on angle-radius relationship
                float spiral = sinf(qa * 2.0f - qd * 0.25f + q.phase + seed * 3.0f);
                spiral = spiral * 0.5f + 0.5f;  // 0..1

                float radial = expf(-qd * qd * 0.005f);  // gaussian falloff from quark center
                float swirl = spiral * radial;

                // Bright core at quark center
                float core = expf(-qd * qd * 0.025f);

                float intensity = swirl * 0.55f + core * 0.65f;
                if (intensity < 0.03f) continue;

                // Modulate by depth — quarks visible through crystal surface
                intensity *= depth * 0.7f + 0.3f;

                int i = (y * SZ + x) * 4;
                float cr = img.data[i + 0] / 255.0f;
                float cg = img.data[i + 1] / 255.0f;
                float cb = img.data[i + 2] / 255.0f;
                float ca = img.data[i + 3] / 255.0f;
                cr = lerp(cr, q.c.r, intensity * 0.6f);
                cg = lerp(cg, q.c.g, intensity * 0.6f);
                cb = lerp(cb, q.c.b, intensity * 0.6f);
                // Add glow
                cr += core * q.c.r * 0.3f;
                cg += core * q.c.g * 0.3f;
                cb += core * q.c.b * 0.3f;
                img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), ca);
            }
        }
    }
}

// Crystal starburst — for bosons (photon, gluon, graviton)
static void render_crystal_starburst(Image& img, Color col, int rays, float ray_len,
                                      float core_radius = 10.0f, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            // Crystal core glow
            float core = expf(-d * d / (core_radius * core_radius * 2.5f));

            // Faceted noise on core surface
            float facet = fbm((float)x * 0.15f + seed, (float)y * 0.15f + seed * 2.0f, 3);
            float core_facet = core * (0.7f + facet * 0.6f);

            // Rays with sparkle
            float ray = 0.0f;
            for (int r = 0; r < rays; ++r) {
                float ra = (float)r * PI * 2.0f / (float)rays + seed * 0.5f;
                float diff = fabsf(fmodf(angle - ra + PI * 3.0f, PI * 2.0f) - PI);
                float width = 0.05f + 0.04f * (1.0f - d / ray_len);
                float ray_intensity = expf(-diff * diff / (width * width)) *
                                     expf(-d / ray_len) * 0.8f;
                // Add sparkle along rays
                float sparkle = vnoise(d * 0.3f + (float)r * 7.0f + seed,
                                        angle * 3.0f + seed * 5.0f);
                ray_intensity *= (0.7f + sparkle * 0.6f);
                ray += ray_intensity;
            }

            float intensity = core_facet + ray;
            if (intensity < 0.01f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white_mix = core * 0.7f;
            float r = lerp(col.r, 1.0f, white_mix) * intensity;
            float g = lerp(col.g, 1.0f, white_mix) * intensity;
            float b = lerp(col.b, 1.0f, white_mix) * intensity;

            // Crystal sparkle points
            float spark = vnoise((float)x * 0.5f + seed * 11.0f, (float)y * 0.5f + seed * 13.0f);
            if (spark > 0.82f && d < ray_len * 0.7f) {
                float si = (spark - 0.82f) / 0.18f;
                r += si * 0.8f; g += si * 0.8f; b += si * 0.8f;
            }

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Crystal ghost — translucent wispy sphere for neutrinos
static void render_crystal_ghost(Image& img, Color col, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 55.0f) continue;

            float norm_d = d / 48.0f;
            float base = expf(-norm_d * norm_d * 2.0f) * 0.55f;

            // Wispy noise layers
            float n1 = fbm((float)x * 0.07f + seed * 3.0f, (float)y * 0.07f + seed * 5.0f, 4);
            float n2 = vnoise((float)x * 0.12f + seed * 7.0f, (float)y * 0.12f + seed * 11.0f);
            float wisp = base * (0.4f + n1 * 0.4f + n2 * 0.2f);

            if (wisp < 0.02f) continue;

            float alpha = clampf(wisp, 0, 0.6f);

            // Inner crystal glow
            float inner = expf(-d * d * 0.003f);
            float facet = vnoise((float)x * 0.2f + seed, (float)y * 0.2f + seed * 2.0f);
            float sparkle = smoothstep(0.7f, 0.9f, facet) * inner * 0.4f;

            float r = col.r * wisp + inner * 0.35f + sparkle;
            float g = col.g * wisp + inner * 0.30f + sparkle;
            float b = col.b * wisp + inner * 0.35f + sparkle;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Crystal quark — small gem sphere with color charge glow ring
static void render_crystal_quark(Image& img, Color col, Color charge_col, float seed = 0.0f) {
    render_crystal(img, col, 36.0f, charge_col, 0.15f, seed, 0.55f);

    // Color charge glow ring
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            float ring_d = fabsf(d - 40.0f);
            if (ring_d > 8.0f) continue;

            float ring = expf(-ring_d * ring_d * 0.18f) * 0.5f;
            // Pulsating ring with noise
            float pulse = 0.7f + 0.3f * sinf(angle * 6.0f + seed * 5.0f);
            ring *= pulse;

            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f;
            float cg = img.data[i + 1] / 255.0f;
            float cb = img.data[i + 2] / 255.0f;
            float ca = img.data[i + 3] / 255.0f;
            cr = lerp(cr, charge_col.r, ring);
            cg = lerp(cg, charge_col.g, ring);
            cb = lerp(cb, charge_col.b, ring);
            ca = std::max(ca, ring * 0.8f);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Crystal antimatter — sphere with distinctive halo ring
static void render_crystal_antimatter(Image& img, Color col, Color ring_col, float seed = 0.0f) {
    render_crystal(img, col, 40.0f, ring_col, 0.13f, seed, 0.4f);

    // Antimatter halo ring with shimmer
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            float ring_d = fabsf(d - 46.0f);
            if (ring_d > 7.0f) continue;

            float ring = expf(-ring_d * ring_d * 0.3f) * 0.55f;
            // Shimmer along ring
            float shimmer = 0.6f + 0.4f * sinf(angle * 8.0f + seed * 3.0f);
            ring *= shimmer;

            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f;
            float cg = img.data[i + 1] / 255.0f;
            float cb = img.data[i + 2] / 255.0f;
            float ca = img.data[i + 3] / 255.0f;
            cr = lerp(cr, ring_col.r, ring);
            cg = lerp(cg, ring_col.g, ring);
            cb = lerp(cb, ring_col.b, ring);
            ca = std::max(ca, ring * 0.9f);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Crystal wave ring — for W/Z bosons
static void render_crystal_wave(Image& img, Color col, int wave_n = 8, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);
            if (d > 58.0f) continue;

            // Crystal core
            float core = expf(-d * d * 0.0015f);
            float facet = fbm((float)x * 0.12f + seed, (float)y * 0.12f + seed * 2.0f, 3);
            core *= (0.7f + facet * 0.5f);

            // Concentric wave rings with crystal shimmer
            float ring1 = expf(-(d - 28.0f) * (d - 28.0f) * 0.025f) * 0.5f;
            float ring2 = expf(-(d - 42.0f) * (d - 42.0f) * 0.04f) * 0.35f;
            float wave = sinf(angle * wave_n + seed * 2.0f) * 0.12f * expf(-d * 0.015f);
            float sparkle = vnoise(angle * 5.0f + seed * 7.0f, d * 0.2f + seed * 3.0f);
            ring1 *= (0.7f + sparkle * 0.6f);
            ring2 *= (0.7f + sparkle * 0.4f);

            float intensity = core + ring1 + ring2 + wave;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white = core * 0.55f;
            float r = lerp(col.r, 1.0f, white) * intensity;
            float g = lerp(col.g, 1.0f, white) * intensity;
            float b = lerp(col.b, 1.0f, white) * intensity;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Crystal Higgs/radiant — golden sphere with concentric field rings
static void render_crystal_higgs(Image& img, Color col, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 60.0f) continue;

            float norm_d = d / 42.0f;
            float core = expf(-norm_d * norm_d * 2.5f);
            float facet = fbm((float)x * 0.1f + seed, (float)y * 0.1f + seed * 2.0f, 4);
            core *= (0.65f + facet * 0.6f);

            // Pulsing concentric field rings
            float rings = sinf(d * 0.28f + seed * 1.5f) * 0.14f * expf(-d * 0.025f);
            rings = std::max(0.0f, rings);

            float intensity = core + rings;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float hot = core * 0.75f;
            float r = lerp(col.r, 1.0f, hot) * intensity + hot * 0.3f;
            float g = lerp(col.g, 1.0f, hot) * intensity + hot * 0.2f;
            float b = col.b * intensity;

            // Crystal sparkles in the field
            float spark = vnoise((float)x * 0.4f + seed * 11.0f, (float)y * 0.4f + seed * 13.0f);
            if (spark > 0.8f) {
                float si = (spark - 0.8f) / 0.2f * core;
                r += si * 0.6f; g += si * 0.5f; b += si * 0.3f;
            }

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Crystal halo — for dark matter/energy
static void render_crystal_halo(Image& img, Color col, float radius = 44.0f,
                                 float turbulence = 10.0f, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            // Turbulent noise for nebulous edge
            float n1 = fbm((float)x * 0.06f + seed * 2.0f, (float)y * 0.06f + seed * 3.0f, 5);
            float n2 = vnoise((float)x * 0.1f + seed * 7.0f, (float)y * 0.1f + seed * 11.0f);
            float perturb = (n1 - 0.5f) * turbulence + (n2 - 0.5f) * turbulence * 0.5f;
            float eff_d = d + perturb;

            // Outer halo glow (extends beyond sphere)
            float halo_r = radius + 12.0f;
            float halo = 1.0f - smoothstep(radius * 0.5f, halo_r, eff_d);
            if (halo < 0.01f) continue;

            // Inner crystal sphere
            float norm_d = d / radius;
            float sphere = 1.0f - smoothstep(0.7f, 1.0f, norm_d);

            // Crystal faceting on inner sphere
            float facet = fbm((float)x * 0.15f + seed * 5.0f, (float)y * 0.15f + seed * 7.0f, 4);
            float nz = sqrtf(std::max(0.0f, 1.0f - clampf(norm_d, 0, 1) * clampf(norm_d, 0, 1)));
            float diffuse = clampf(0.3f + 0.7f * (nz + (facet - 0.5f) * 0.2f), 0, 1);

            // Combine sphere + halo
            float intensity = sphere * diffuse + halo * 0.5f;

            // Sparkles
            float sp = hash(floorf((float)x * 0.5f), floorf((float)y * 0.5f));
            if (sp > 0.97f && d < radius + 5.0f)
                intensity += (sp - 0.97f) * 20.0f * halo;

            // Specular highlights
            float lx = -0.4f, ly = -0.6f, lz = 0.7f;
            float nx = dx / std::max(d, 1.0f), ny = dy / std::max(d, 1.0f);
            float spec = clampf(nx * lx + ny * ly + nz * lz, 0, 1);
            intensity += powf(spec, 30.0f) * sphere * 0.6f;

            float alpha = clampf(halo * 0.9f + sphere * 0.1f, 0, 1);

            float r = col.r * intensity;
            float g = col.g * intensity;
            float b = col.b * intensity;

            // Inner glow tint
            float inner_glow = expf(-d * d * 0.002f) * 0.3f;
            r += inner_glow;
            g += inner_glow * 0.8f;
            b += inner_glow;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Crystal SUSY sparticle — wobbled edge sphere with shimmer
static void render_crystal_susy(Image& img, Color col, float seed) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float angle = atan2f(dy, dx);
            float d = sqrtf(dx * dx + dy * dy);

            // Wobbled radius (tilde visual)
            float wobble = 3.5f * sinf(angle * 5.0f + seed * 7.13f) +
                           2.5f * sinf(angle * 3.0f + seed * 11.7f);
            float eff_radius = 40.0f + wobble;
            float norm_d = d / eff_radius;

            if (d > eff_radius + 6.0f) continue;

            float alpha = 1.0f - smoothstep(0.88f, 1.0f, norm_d);
            if (alpha < 0.01f) continue;

            float nz = sqrtf(std::max(0.0f, 1.0f - clampf(norm_d, 0, 1) * clampf(norm_d, 0, 1)));
            float nx = dx / eff_radius, ny = dy / eff_radius;

            // Crystal faceting
            float facet = fbm((float)x * 0.12f + seed * 3.0f, (float)y * 0.12f + seed * 5.0f, 4);
            float pnz = nz + (facet - 0.5f) * 0.15f;

            float diffuse = clampf(0.2f + 0.8f * pnz, 0.0f, 1.0f);
            float r = col.r * diffuse;
            float g = col.g * diffuse;
            float b = col.b * diffuse;

            // Inner glow
            float ig = (1.0f - norm_d); ig *= ig;
            r += ig * 0.3f; g += ig * 0.25f; b += ig * 0.35f;

            // Crystal shimmer
            float shimmer = 0.18f * sinf(d * 0.35f + angle * 4.0f + seed * 5.0f);
            shimmer += vnoise((float)x * 0.2f + seed * 9.0f, (float)y * 0.2f + seed * 11.0f) * 0.1f;
            r += shimmer; g += shimmer; b += shimmer;

            // Multi-specular
            float sdx = dx + eff_radius * 0.3f;
            float sdy = dy + eff_radius * 0.3f;
            float sd = sqrtf(sdx * sdx + sdy * sdy) / (eff_radius * 0.3f);
            if (sd < 1.0f) {
                float spec = 1.0f - sd;
                spec *= spec; spec *= spec; spec *= spec;
                r += spec * 0.6f; g += spec * 0.6f; b += spec * 0.6f;
            }
            // Secondary specular
            sdx = dx - eff_radius * 0.2f;
            sdy = dy + eff_radius * 0.25f;
            sd = sqrtf(sdx * sdx + sdy * sdy) / (eff_radius * 0.25f);
            if (sd < 1.0f) {
                float spec = 1.0f - sd;
                spec *= spec; spec *= spec;
                r += spec * 0.25f; g += spec * 0.25f; b += spec * 0.25f;
            }

            // Fresnel rim
            float fresnel = powf(1.0f - nz, 3.0f);
            r += fresnel * col.r * 0.35f;
            g += fresnel * col.g * 0.35f;
            b += fresnel * col.b * 0.35f;

            if (d > eff_radius) {
                float glow_t = (d - eff_radius) / 6.0f;
                alpha = (1.0f - glow_t) * 0.25f;
                r = col.r * 0.4f; g = col.g * 0.4f; b = col.b * 0.4f;
            }

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), clampf(alpha, 0, 1));
        }
    }
}

// Crystal exotic — fractal-edge turbulent crystal sphere
static void render_crystal_exotic(Image& img, Color col, float seed) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            float noise = fbm((float)x * 0.05f + seed * 3.0f,
                               (float)y * 0.05f + seed * 7.0f, 5);
            float eff_radius = 42.0f + (noise - 0.5f) * 14.0f;

            if (d > eff_radius + 6.0f) continue;

            float norm_d = d / eff_radius;
            float alpha = 1.0f - smoothstep(0.82f, 1.05f, norm_d);
            if (alpha < 0.01f) continue;

            float nz = sqrtf(std::max(0.0f, 1.0f - clampf(norm_d, 0, 1) * clampf(norm_d, 0, 1)));

            // Crystal faceting
            float facet = fbm((float)x * 0.12f + seed * 5.0f, (float)y * 0.12f + seed * 9.0f, 4);
            float diffuse = clampf(0.2f + 0.8f * (nz + (facet - 0.5f) * 0.2f), 0.0f, 1.0f);

            float n2 = fbm((float)x * 0.1f + seed, (float)y * 0.1f + seed * 2.0f, 3);
            Color c = { col.r * diffuse + n2 * 0.1f,
                        col.g * diffuse + n2 * 0.07f,
                        col.b * diffuse + n2 * 0.12f };

            // Inner crystal glow
            float ig = (1.0f - norm_d); ig *= ig;
            c.r += ig * 0.3f; c.g += ig * 0.25f; c.b += ig * 0.3f;

            // Multi specular
            float sdx = dx + 16.0f, sdy = dy + 16.0f;
            float sd = sqrtf(sdx * sdx + sdy * sdy) / 14.0f;
            if (sd < 1.0f) {
                float spec = (1.0f - sd); spec *= spec; spec *= spec;
                c.r += spec * 0.5f; c.g += spec * 0.5f; c.b += spec * 0.5f;
            }
            sdx = dx - 12.0f; sdy = dy + 10.0f;
            sd = sqrtf(sdx * sdx + sdy * sdy) / 12.0f;
            if (sd < 1.0f) {
                float spec = (1.0f - sd); spec *= spec; spec *= spec;
                c.r += spec * 0.2f; c.g += spec * 0.2f; c.b += spec * 0.2f;
            }

            // Fresnel
            float fresnel = powf(1.0f - nz, 3.0f);
            c.r += fresnel * col.r * 0.3f;
            c.g += fresnel * col.g * 0.3f;
            c.b += fresnel * col.b * 0.3f;

            // Sparkle
            float spark = vnoise((float)x * 0.35f + seed * 13.0f, (float)y * 0.35f + seed * 17.0f);
            float edge = fabsf(facet - 0.5f);
            if (spark > 0.75f && edge > 0.2f) {
                float si = (spark - 0.75f) / 0.25f * (1.0f - norm_d * 0.5f);
                c.r += si * 0.5f; c.g += si * 0.5f; c.b += si * 0.5f;
            }

            img.set(x, y, clampf(c.r, 0, 1), clampf(c.g, 0, 1), clampf(c.b, 0, 1), clampf(alpha, 0, 1));
        }
    }
}

// Crystal monopole — sphere with radial field lines
static void render_crystal_monopole(Image& img, Color col, float seed = 0.0f) {
    render_crystal(img, col, 38.0f, {1.0f, 1.0f, 1.0f}, 0.13f, seed, 0.4f);

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            if (d < 36.0f || d > 60.0f) continue;

            float line = 0.0f;
            for (int n = 0; n < 12; ++n) {
                float la = (float)n * PI * 2.0f / 12.0f + seed * 0.3f;
                float diff = fabsf(fmodf(angle - la + PI * 3.0f, PI * 2.0f) - PI);
                float li = expf(-diff * diff * 90.0f) * expf(-(d - 36.0f) * 0.055f);
                // Sparkle along field lines
                float spark = vnoise(d * 0.3f + (float)n * 5.0f, angle * 2.0f + seed);
                li *= (0.6f + spark * 0.8f);
                line += li;
            }

            if (line < 0.02f) continue;
            line = clampf(line, 0, 0.65f);

            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f + line * 0.6f;
            float cg = img.data[i + 1] / 255.0f + line * 0.6f;
            float cb = img.data[i + 2] / 255.0f + line * 0.9f;
            float ca = std::max(img.data[i + 3] / 255.0f, line);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Crystal tachyon — speed-streaked asymmetric glow
static void render_crystal_tachyon(Image& img, Color col, float seed = 0.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;

            float stretch_x = dx * 0.65f;
            float d = sqrtf(stretch_x * stretch_x + dy * dy);

            float core = expf(-d * d * 0.0035f);
            float facet = fbm((float)x * 0.1f + seed, (float)y * 0.1f + seed * 2.0f, 3);
            core *= (0.6f + facet * 0.6f);

            float trail = expf(-dy * dy * 0.008f) * expf(dx * 0.035f) * 0.35f;
            trail = std::max(0.0f, trail);

            float intensity = core + trail;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white = core * 0.65f;
            float r = lerp(col.r, 1.0f, white) * intensity;
            float g = lerp(col.g, 1.0f, white) * intensity;
            float b = lerp(col.b, 1.0f, white) * intensity;

            // Speed-line sparkles
            float spark = vnoise((float)x * 0.3f + seed * 7.0f, (float)y * 0.6f + seed * 11.0f);
            if (spark > 0.8f && trail > 0.05f) {
                float si = (spark - 0.8f) / 0.2f;
                r += si * 0.5f; g += si * 0.5f; b += si * 0.5f;
            }

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// ── Particle type definitions ────────────────────────────────────────────────

static const char* const PHYS_TYPE_NAMES[67] = {
    "Proton", "Neutron", "Electron", "Photon", "Positron", "Antiproton",
    "Neutrino_e",
    "Muon", "Anti-muon", "Tau", "Anti-tau", "Neutrino_mu", "Neutrino_tau",
    "Up", "Down", "Strange", "Charm", "Top", "Bottom",
    "Anti-up", "Anti-down", "Anti-strange", "Anti-charm", "Anti-top", "Anti-bottom",
    "Gluon", "W+", "W-", "Z0", "Higgs",
    "Graviton", "Dark Matter", "Dark Energy",
    "Axino", "WIMPzilla", "SIMP", "Sterile Neutrino", "Dark Photon", "Q-Ball",
    "Selectron", "Smuon", "Stau", "Squark", "Gluino", "Photino",
    "Wino", "Zino", "Higgsino", "Neutralino", "Sneutrino",
    "Gravitino", "X Boson", "Y Boson", "Monopole", "Radion", "Dilaton",
    "Tachyon", "Preon", "Inflaton", "Majoron", "Odderon",
    "Glueball", "Skyrmion", "X17", "Chameleon",
    "Paraparticle", "Dyn. Axion QP",
};

static std::string type_to_filename(const char* name) {
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ')       out += '_';
        else if (c == '+')  out += "_plus";
        else if (c == '-')  out += "_minus";
        else if (c == '.')  { /* skip dots */ }
        else if (c == '\'') out += "_prime";
        else                out += c;
    }
    return out + ".png";
}

static const Color TYPE_COLORS[67] = {
    {0.85f, 0.15f, 0.15f},  // 0  Proton — deep ruby red
    {0.55f, 0.60f, 0.58f},  // 1  Neutron — cool grey-green
    {0.15f, 0.45f, 1.0f},   // 2  Electron — sapphire blue
    {1.0f, 1.0f, 0.55f},    // 3  Photon — warm golden
    {1.0f, 0.25f, 0.75f},   // 4  Positron — hot pink
    {0.15f, 0.80f, 0.65f},  // 5  Antiproton — teal crystal
    {0.55f, 0.85f, 0.55f},  // 6  Neutrino_e — pale green
    {0.55f, 0.25f, 0.85f},  // 7  Muon — amethyst
    {0.75f, 0.45f, 0.95f},  // 8  Anti-muon — lavender
    {0.35f, 0.15f, 0.65f},  // 9  Tau — deep violet
    {0.55f, 0.35f, 0.85f},  // 10 Anti-tau — light violet
    {0.45f, 0.75f, 0.45f},  // 11 Neutrino_mu — green mist
    {0.35f, 0.65f, 0.35f},  // 12 Neutrino_tau — forest mist
    {0.90f, 0.50f, 0.15f},  // 13 Up — amber
    {0.35f, 0.70f, 0.15f},  // 14 Down — lime
    {0.15f, 0.75f, 0.55f},  // 15 Strange — aquamarine
    {0.90f, 0.80f, 0.15f},  // 16 Charm — gold
    {1.00f, 0.25f, 0.25f},  // 17 Top — crimson
    {0.45f, 0.25f, 0.75f},  // 18 Bottom — plum
    {1.0f, 0.7f, 0.45f},    // 19 Anti-up
    {0.65f, 0.90f, 0.45f},  // 20 Anti-down
    {0.45f, 0.95f, 0.75f},  // 21 Anti-strange
    {1.0f, 0.90f, 0.45f},   // 22 Anti-charm
    {1.0f, 0.55f, 0.55f},   // 23 Anti-top
    {0.65f, 0.55f, 0.95f},  // 24 Anti-bottom
    {0.25f, 0.90f, 0.25f},  // 25 Gluon — emerald
    {0.85f, 0.85f, 1.0f},   // 26 W+ — ice blue
    {0.65f, 0.65f, 1.0f},   // 27 W- — periwinkle
    {0.75f, 0.75f, 0.88f},  // 28 Z0 — silver-blue
    {1.0f, 0.82f, 0.25f},   // 29 Higgs — pure gold
    {0.65f, 0.75f, 1.0f},   // 30 Graviton — pale blue
    {0.25f, 0.08f, 0.45f},  // 31 Dark Matter — deep purple
    {0.55f, 0.08f, 0.15f},  // 32 Dark Energy — dark crimson
    {0.35f, 0.18f, 0.55f},  // 33 Axino
    {0.18f, 0.05f, 0.30f},  // 34 WIMPzilla
    {0.30f, 0.22f, 0.50f},  // 35 SIMP
    {0.40f, 0.50f, 0.40f},  // 36 Sterile Neutrino
    {0.45f, 0.18f, 0.65f},  // 37 Dark Photon
    {0.50f, 0.28f, 0.70f},  // 38 Q-Ball
    {0.45f, 0.75f, 1.0f},   // 39 Selectron
    {0.65f, 0.55f, 1.0f},   // 40 Smuon
    {0.55f, 0.45f, 0.85f},  // 41 Stau
    {1.0f, 0.65f, 0.35f},   // 42 Squark
    {0.35f, 0.95f, 0.45f},  // 43 Gluino
    {1.0f, 1.0f, 0.75f},    // 44 Photino
    {0.85f, 0.85f, 1.0f},   // 45 Wino
    {0.75f, 0.75f, 0.90f},  // 46 Zino
    {1.0f, 0.85f, 0.55f},   // 47 Higgsino
    {0.35f, 0.18f, 0.60f},  // 48 Neutralino
    {0.50f, 0.80f, 0.50f},  // 49 Sneutrino
    {0.55f, 0.65f, 1.0f},   // 50 Gravitino
    {1.0f, 0.35f, 0.35f},   // 51 X Boson
    {1.0f, 0.45f, 0.25f},   // 52 Y Boson
    {0.90f, 0.90f, 0.92f},  // 53 Monopole — near white
    {0.65f, 0.55f, 0.35f},  // 54 Radion
    {0.55f, 0.50f, 0.40f},  // 55 Dilaton
    {0.0f, 1.0f, 1.0f},     // 56 Tachyon — cyan
    {1.0f, 0.0f, 0.45f},    // 57 Preon — hot magenta
    {1.0f, 0.65f, 0.08f},   // 58 Inflaton — deep gold
    {0.50f, 0.55f, 0.50f},  // 59 Majoron
    {0.65f, 0.25f, 1.0f},   // 60 Odderon
    {0.45f, 1.0f, 0.25f},   // 61 Glueball — lime green
    {0.85f, 0.35f, 0.15f},  // 62 Skyrmion — burnt orange
    {0.15f, 0.85f, 0.75f},  // 63 X17 — turquoise
    {0.65f, 0.45f, 0.25f},  // 64 Chameleon
    {0.85f, 0.15f, 1.0f},   // 65 Paraparticle — magenta
    {0.35f, 0.65f, 0.90f},  // 66 Dyn. Axion QP — sky blue
};

int main() {
    std::filesystem::create_directories("assets/particles");

    Image img;
    int count = 0;

    for (int t = 0; t < 67; ++t) {
        img.clear();
        Color c = TYPE_COLORS[t];
        std::string filename = type_to_filename(PHYS_TYPE_NAMES[t]);
        std::string path = std::string("assets/particles/") + filename;
        float seed = (float)t;

        switch (t) {
        case 0: // Proton: ruby crystal with uud quark swirls
            render_crystal_nucleon(img, c,
                {1.0f, 0.6f, 0.15f},   // u — gold
                {1.0f, 0.6f, 0.15f},   // u — gold
                {0.3f, 0.8f, 0.3f},    // d — green
                seed);
            break;
        case 1: // Neutron: grey-green crystal with udd quark swirls
            render_crystal_nucleon(img, c,
                {1.0f, 0.6f, 0.15f},   // u — gold
                {0.3f, 0.8f, 0.3f},    // d — green
                {0.3f, 0.8f, 0.3f},    // d — green
                seed);
            break;
        case 2: // Electron: sapphire blue crystal with bright core
            render_crystal(img, c, 44.0f, {0.5f, 0.8f, 1.0f}, 0.11f, seed, 0.65f);
            break;
        case 3: // Photon: golden crystal starburst
            render_crystal_starburst(img, c, 8, 55.0f, 11.0f, seed);
            break;
        case 4: // Positron: pink crystal with antimatter ring
            render_crystal_antimatter(img, c, {1.0f, 0.75f, 1.0f}, seed);
            break;
        case 5: // Antiproton: teal crystal with quark swirls + antimatter ring
            render_crystal_nucleon(img, c,
                {0.5f, 1.0f, 0.8f}, {0.7f, 0.9f, 0.5f}, {0.7f, 0.9f, 0.5f}, seed);
            // Overlay antimatter ring
            for (int y = 0; y < SZ; ++y)
                for (int x = 0; x < SZ; ++x) {
                    float dx = (float)x - CX, dy = (float)y - CY;
                    float d = sqrtf(dx * dx + dy * dy);
                    float angle = atan2f(dy, dx);
                    float ring_d = fabsf(d - 52.0f);
                    if (ring_d > 5.0f) continue;
                    float ring = expf(-ring_d * ring_d * 0.45f) * 0.5f;
                    float shimmer = 0.6f + 0.4f * sinf(angle * 7.0f + seed * 3.0f);
                    ring *= shimmer;
                    int i = (y * SZ + x) * 4;
                    float cr = img.data[i + 0] / 255.0f + ring * 0.4f;
                    float cg = img.data[i + 1] / 255.0f + ring * 0.9f;
                    float cb = img.data[i + 2] / 255.0f + ring * 0.8f;
                    float ca = std::max(img.data[i + 3] / 255.0f, ring);
                    img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
                }
            break;
        case 6: case 11: case 12: // Neutrinos: crystal ghost
            render_crystal_ghost(img, c, seed);
            break;
        case 7: // Muon: amethyst crystal
            render_crystal(img, c, 40.0f, {0.8f, 0.5f, 1.0f}, 0.12f, seed, 0.45f);
            break;
        case 8: // Anti-muon: lavender with antimatter ring
            render_crystal_antimatter(img, c, {1.0f, 0.7f, 1.0f}, seed);
            break;
        case 9: // Tau: deep violet crystal
            render_crystal(img, c, 42.0f, {0.6f, 0.3f, 0.9f}, 0.11f, seed, 0.45f);
            break;
        case 10: // Anti-tau: with ring
            render_crystal_antimatter(img, c, {0.8f, 0.6f, 1.0f}, seed);
            break;
        case 13: render_crystal_quark(img, c, {1.0f, 0.3f, 0.2f}, seed); break; // Up
        case 14: render_crystal_quark(img, c, {0.2f, 0.8f, 0.3f}, seed); break; // Down
        case 15: render_crystal_quark(img, c, {0.2f, 0.3f, 1.0f}, seed); break; // Strange
        case 16: render_crystal_quark(img, c, {1.0f, 0.8f, 0.1f}, seed); break; // Charm
        case 17: render_crystal_quark(img, c, {1.0f, 0.2f, 0.2f}, seed); break; // Top
        case 18: render_crystal_quark(img, c, {0.4f, 0.2f, 0.9f}, seed); break; // Bottom
        case 19: case 20: case 21: case 22: case 23: case 24: // Anti-quarks
            render_crystal_antimatter(img, c, {1.0f, 1.0f, 1.0f}, seed);
            break;
        case 25: // Gluon: emerald crystal starburst
            render_crystal_starburst(img, c, 6, 48.0f, 13.0f, seed);
            break;
        case 26: render_crystal_wave(img, c, 6, seed); break; // W+
        case 27: render_crystal_wave(img, c, 6, seed); break; // W-
        case 28: render_crystal_wave(img, c, 8, seed); break; // Z0
        case 29: render_crystal_higgs(img, c, seed); break;   // Higgs
        case 30: // Graviton: pale blue crystal starburst
            render_crystal_starburst(img, c, 4, 50.0f, 9.0f, seed);
            break;
        case 31: // Dark matter: deep purple crystal halo
            render_crystal_halo(img, c, 42.0f, 12.0f, seed);
            break;
        case 32: // Dark energy: dark crimson crystal halo
            render_crystal_halo(img, c, 46.0f, 16.0f, seed);
            break;
        case 33: case 34: case 35: // DM candidates: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 36: // Sterile neutrino: crystal ghost
            render_crystal_ghost(img, c, seed); break;
        case 37: // Dark photon: purple crystal starburst
            render_crystal_starburst(img, c, 6, 45.0f, 10.0f, seed);
            break;
        case 38: // Q-Ball: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 39: case 40: case 41: case 42: case 43: case 44: // SUSY
            render_crystal_susy(img, c, seed); break;
        case 45: case 46: case 47: // Wino, Zino, Higgsino
            render_crystal_susy(img, c, seed); break;
        case 48: // Neutralino: dark crystal SUSY
            render_crystal_susy(img, c, seed); break;
        case 49: // Sneutrino: crystal SUSY
            render_crystal_susy(img, c, seed); break;
        case 50: // Gravitino: crystal SUSY
            render_crystal_susy(img, c, seed); break;
        case 51: case 52: // X, Y bosons: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 53: // Monopole: crystal with field lines
            render_crystal_monopole(img, c, seed); break;
        case 54: case 55: // Radion, Dilaton: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 56: // Tachyon: crystal speed-streak
            render_crystal_tachyon(img, c, seed); break;
        case 57: // Preon: hot magenta crystal
            render_crystal(img, c, 30.0f, {1.0f, 0.5f, 0.8f}, 0.14f, seed, 0.55f);
            break;
        case 58: // Inflaton: deep gold crystal radiant
            render_crystal_higgs(img, c, seed); break;
        case 59: // Majoron: crystal ghost
            render_crystal_ghost(img, c, seed); break;
        case 60: // Odderon: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 61: // Glueball: lime green crystal starburst
            render_crystal_starburst(img, c, 10, 42.0f, 15.0f, seed);
            break;
        case 62: // Skyrmion: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 63: // X17: turquoise crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 64: // Chameleon: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 65: // Paraparticle: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        case 66: // Dyn. Axion QP: crystal exotic
            render_crystal_exotic(img, c, seed); break;
        default:
            render_crystal(img, c);
            break;
        }

        if (img.save(path.c_str())) {
            printf("[%2d/67] %s\n", t + 1, path.c_str());
            ++count;
        } else {
            fprintf(stderr, "FAILED: %s\n", path.c_str());
        }
    }

    printf("\nGenerated %d crystal particle textures in assets/particles/\n", count);
    return 0;
}
