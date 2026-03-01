// Procedural particle texture generator — creates 128x128 RGBA PNGs for all 67 types.
// Build: g++ -O2 -o gen_textures tools/gen_particle_textures.cpp -lm
// Run:   ./gen_textures   (outputs to assets/particles/)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../src/stb_image_write.h"
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

// Simple hash-based noise
static float hash(float x, float y) {
    float h = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return fract(h);
}

// Value noise with smooth interpolation
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

// Fractal noise (4 octaves)
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

    bool save(const char* path) {
        return stbi_write_png(path, SZ, SZ, 4, data, SZ * 4) != 0;
    }
};

// ── Per-pixel rendering callbacks ────────────────────────────────────────────

// Standard solid sphere with inner glow, specular, rim lighting
static void render_sphere(Image& img, Color col, float radius = 50.0f,
                           float spec_strength = 0.5f, float inner_glow = 0.3f,
                           Color glow_col = {1, 1, 1}) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            if (d > radius + 3.0f) continue;

            float norm_d = d / radius;
            float alpha = 1.0f - smoothstep(0.9f, 1.0f, norm_d);
            if (alpha < 0.01f) continue;

            // Base shading: lit from top-left
            float nz = sqrtf(std::max(0.0f, 1.0f - norm_d * norm_d));
            float nx = dx / radius, ny = dy / radius;
            float diffuse = clampf(0.3f + 0.7f * (nx * -0.4f + ny * -0.4f + nz * 0.82f), 0.0f, 1.0f);

            // Inner glow
            float ig = (1.0f - norm_d);
            ig *= ig;
            Color c = { col.r * diffuse, col.g * diffuse, col.b * diffuse };
            c = lerp_col(c, glow_col, ig * inner_glow);

            // Specular highlight (top-left)
            float sdx = dx + radius * 0.3f;
            float sdy = dy + radius * 0.3f;
            float sd = sqrtf(sdx * sdx + sdy * sdy) / (radius * 0.35f);
            if (sd < 1.0f) {
                float spec = (1.0f - sd);
                spec *= spec;
                spec *= spec;
                c.r += spec * spec_strength;
                c.g += spec * spec_strength;
                c.b += spec * spec_strength;
            }

            // Rim lighting
            float rim = smoothstep(0.6f, 1.0f, norm_d) * (1.0f - smoothstep(0.95f, 1.0f, norm_d));
            c.r += rim * col.r * 0.3f;
            c.g += rim * col.g * 0.3f;
            c.b += rim * col.b * 0.3f;

            // Outer glow
            if (d > radius) {
                float glow = 1.0f - (d - radius) / 3.0f;
                glow *= glow;
                alpha = glow * 0.4f;
                c = col;
            }

            img.set(x, y, clampf(c.r, 0, 1), clampf(c.g, 0, 1), clampf(c.b, 0, 1), alpha);
        }
    }
}

// Radial glow / starburst (for photon, gluon, etc.)
static void render_starburst(Image& img, Color col, int rays, float ray_len,
                              float core_radius = 8.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            // Core glow
            float core = expf(-d * d / (core_radius * core_radius * 2.0f));

            // Rays
            float ray = 0.0f;
            for (int r = 0; r < rays; ++r) {
                float ra = (float)r * PI * 2.0f / (float)rays;
                float diff = fabsf(fmodf(angle - ra + PI * 3.0f, PI * 2.0f) - PI);
                float width = 0.06f + 0.03f * (1.0f - d / ray_len);
                float ray_intensity = expf(-diff * diff / (width * width)) *
                                     expf(-d / ray_len) * 0.7f;
                ray += ray_intensity;
            }

            float intensity = core + ray;
            if (intensity < 0.01f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white_mix = core * 0.6f;  // white-hot center
            float r = lerp(col.r, 1.0f, white_mix) * intensity;
            float g = lerp(col.g, 1.0f, white_mix) * intensity;
            float b = lerp(col.b, 1.0f, white_mix) * intensity;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Diffuse halo (dark matter, dark energy)
static void render_halo(Image& img, Color col, float falloff = 0.03f,
                         float turbulence = 5.0f) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            float noise = fbm((float)x * 0.06f, (float)y * 0.06f);
            float perturbed_d = d + (noise - 0.5f) * turbulence;

            float intensity = expf(-perturbed_d * perturbed_d * falloff);
            if (intensity < 0.01f) continue;

            float alpha = clampf(intensity * 0.8f, 0, 1);
            float r = col.r * intensity;
            float g = col.g * intensity;
            float b = col.b * intensity;

            // Subtle inner brightness
            float inner = expf(-d * d * 0.005f);
            r += inner * 0.15f;
            g += inner * 0.15f;
            b += inner * 0.15f;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Nucleon: sphere with internal quarks visible as colored spots
static void render_nucleon(Image& img, Color col, Color q1, Color q2, Color q3) {
    // First render base sphere
    render_sphere(img, col, 50.0f, 0.4f, 0.25f, {1, 0.9f, 0.8f});

    // Then overlay quark spots
    float qr = 12.0f;
    struct { float x, y; Color c; } quarks[] = {
        { CX - 14.0f, CY - 10.0f, q1 },
        { CX + 16.0f, CY - 8.0f,  q2 },
        { CX + 0.0f,  CY + 16.0f, q3 },
    };

    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX, dy = (float)y - CY;
            float d_center = sqrtf(dx * dx + dy * dy);
            if (d_center > 50.0f) continue;  // inside sphere only

            for (auto& q : quarks) {
                float qdx = (float)x - q.x;
                float qdy = (float)y - q.y;
                float qd = sqrtf(qdx * qdx + qdy * qdy);
                if (qd < qr) {
                    float t = (1.0f - qd / qr);
                    t *= t;
                    float blend = t * 0.6f;
                    int i = (y * SZ + x) * 4;
                    float cr = img.data[i + 0] / 255.0f;
                    float cg = img.data[i + 1] / 255.0f;
                    float cb = img.data[i + 2] / 255.0f;
                    float ca = img.data[i + 3] / 255.0f;
                    cr = lerp(cr, q.c.r, blend);
                    cg = lerp(cg, q.c.g, blend);
                    cb = lerp(cb, q.c.b, blend);
                    img.set(x, y, cr, cg, cb, ca);
                }
            }
        }
    }
}

// Ghost particle (neutrino-like): translucent, wispy
static void render_ghost(Image& img, Color col) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            if (d > 55.0f) continue;

            float noise = fbm((float)x * 0.08f, (float)y * 0.08f, 3);
            float norm_d = d / 50.0f;
            float base = expf(-norm_d * norm_d * 2.0f) * 0.6f;
            float wisp = base * (0.5f + noise * 0.5f);

            if (wisp < 0.02f) continue;

            float alpha = clampf(wisp, 0, 0.65f);
            float white = expf(-d * d * 0.003f) * 0.4f;
            float r = col.r * wisp + white;
            float g = col.g * wisp + white;
            float b = col.b * wisp + white;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Quark: small sphere with color charge glow ring
static void render_quark(Image& img, Color col, Color charge_col) {
    render_sphere(img, col, 38.0f, 0.55f, 0.5f, charge_col);

    // Color charge glow ring
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            float ring_d = fabsf(d - 42.0f);
            if (ring_d > 8.0f) continue;

            float ring = expf(-ring_d * ring_d * 0.2f) * 0.45f;
            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f;
            float cg = img.data[i + 1] / 255.0f;
            float cb = img.data[i + 2] / 255.0f;
            float ca = img.data[i + 3] / 255.0f;
            cr = lerp(cr, charge_col.r, ring);
            cg = lerp(cg, charge_col.g, ring);
            cb = lerp(cb, charge_col.b, ring);
            ca = std::max(ca, ring);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Wave-pattern ring (for W/Z bosons)
static void render_wave_ring(Image& img, Color col, int wave_n = 8) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            if (d > 58.0f) continue;

            float core = expf(-d * d * 0.002f);
            float ring1 = expf(-(d - 30.0f) * (d - 30.0f) * 0.03f) * 0.5f;
            float ring2 = expf(-(d - 45.0f) * (d - 45.0f) * 0.05f) * 0.3f;
            float wave = sinf(angle * wave_n) * 0.1f * expf(-d * 0.02f);

            float intensity = core + ring1 + ring2 + wave;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white = core * 0.5f;
            float r = lerp(col.r, 1.0f, white) * intensity;
            float g = lerp(col.g, 1.0f, white) * intensity;
            float b = lerp(col.b, 1.0f, white) * intensity;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// Higgs / special boson: golden radiant sphere with pulsing field
static void render_higgs(Image& img, Color col) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            if (d > 60.0f) continue;

            float norm_d = d / 45.0f;
            float core = expf(-norm_d * norm_d * 3.0f);
            // Concentric field rings
            float rings = sinf(d * 0.3f) * 0.12f * expf(-d * 0.03f);
            rings = std::max(0.0f, rings);

            float intensity = core + rings;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float hot = core * 0.7f;
            float r = lerp(col.r, 1.0f, hot) * intensity + hot * 0.3f;
            float g = lerp(col.g, 1.0f, hot) * intensity + hot * 0.2f;
            float b = col.b * intensity;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// SUSY sparticle: sphere with tilde-wave distortion
static void render_susy(Image& img, Color col, float seed) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float angle = atan2f(dy, dx);
            float d = sqrtf(dx * dx + dy * dy);

            // Wobbled radius (tilde visual)
            float wobble = 3.0f * sinf(angle * 5.0f + seed * 7.13f) +
                           2.0f * sinf(angle * 3.0f + seed * 11.7f);
            float eff_radius = 42.0f + wobble;
            float norm_d = d / eff_radius;

            if (d > eff_radius + 8.0f) continue;

            float alpha = 1.0f - smoothstep(0.85f, 1.0f, norm_d);
            if (alpha < 0.01f) continue;

            // 3D-ish shading
            float nz = sqrtf(std::max(0.0f, 1.0f - clampf(norm_d, 0, 1) * clampf(norm_d, 0, 1)));
            float diffuse = clampf(0.3f + 0.7f * nz, 0.0f, 1.0f);

            float r = col.r * diffuse;
            float g = col.g * diffuse;
            float b = col.b * diffuse;

            // Shimmer
            float shimmer = 0.15f * sinf(d * 0.4f + angle * 3.0f + seed * 5.0f);
            r += shimmer; g += shimmer; b += shimmer;

            // Specular
            float sdx = dx + eff_radius * 0.3f;
            float sdy = dy + eff_radius * 0.3f;
            float sd = sqrtf(sdx * sdx + sdy * sdy) / (eff_radius * 0.35f);
            if (sd < 1.0f) {
                float spec = 1.0f - sd;
                spec *= spec; spec *= spec;
                r += spec * 0.5f; g += spec * 0.5f; b += spec * 0.5f;
            }

            // Outer glow
            if (d > eff_radius) {
                float glow_t = (d - eff_radius) / 8.0f;
                alpha = (1.0f - glow_t) * 0.3f;
                r = col.r * 0.5f; g = col.g * 0.5f; b = col.b * 0.5f;
            }

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), clampf(alpha, 0, 1));
        }
    }
}

// Exotic particle: fractal-edge turbulent sphere
static void render_exotic(Image& img, Color col, float seed) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            float noise = fbm((float)x * 0.05f + seed * 3.0f,
                               (float)y * 0.05f + seed * 7.0f, 5);
            float eff_radius = 44.0f + (noise - 0.5f) * 16.0f;

            if (d > eff_radius + 6.0f) continue;

            float norm_d = d / eff_radius;
            float alpha = 1.0f - smoothstep(0.8f, 1.05f, norm_d);
            if (alpha < 0.01f) continue;

            float nz = sqrtf(std::max(0.0f, 1.0f - clampf(norm_d, 0, 1) * clampf(norm_d, 0, 1)));
            float diffuse = clampf(0.25f + 0.75f * nz, 0.0f, 1.0f);

            // Color variation from noise
            float n2 = fbm((float)x * 0.1f + seed, (float)y * 0.1f + seed * 2.0f, 3);
            Color c = { col.r * diffuse + n2 * 0.12f,
                        col.g * diffuse + n2 * 0.08f,
                        col.b * diffuse + n2 * 0.15f };

            // Inner glow
            float ig = (1.0f - norm_d); ig *= ig;
            c.r += ig * 0.25f; c.g += ig * 0.2f; c.b += ig * 0.25f;

            // Specular
            float sdx = dx + 18.0f, sdy = dy + 18.0f;
            float sd = sqrtf(sdx * sdx + sdy * sdy) / 16.0f;
            if (sd < 1.0f) {
                float spec = (1.0f - sd); spec *= spec; spec *= spec;
                c.r += spec * 0.4f; c.g += spec * 0.4f; c.b += spec * 0.4f;
            }

            img.set(x, y, clampf(c.r, 0, 1), clampf(c.g, 0, 1), clampf(c.b, 0, 1), clampf(alpha, 0, 1));
        }
    }
}

// Antimatter: sphere + distinctive halo ring
static void render_antimatter(Image& img, Color col, Color ring_col) {
    render_sphere(img, col, 42.0f, 0.5f, 0.35f, ring_col);

    // Antimatter halo ring
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);

            float ring_d = fabsf(d - 48.0f);
            if (ring_d > 6.0f) continue;

            float ring = expf(-ring_d * ring_d * 0.4f) * 0.55f;
            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f;
            float cg = img.data[i + 1] / 255.0f;
            float cb = img.data[i + 2] / 255.0f;
            float ca = img.data[i + 3] / 255.0f;
            cr = lerp(cr, ring_col.r, ring);
            cg = lerp(cg, ring_col.g, ring);
            cb = lerp(cb, ring_col.b, ring);
            ca = std::max(ca, ring);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Monopole: sphere with magnetic field lines
static void render_monopole(Image& img, Color col) {
    render_sphere(img, col, 40.0f, 0.6f, 0.3f, {1, 1, 1});

    // Radial field lines emanating from center
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;
            float d = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            if (d < 38.0f || d > 60.0f) continue;

            // 12 field lines
            float line = 0.0f;
            for (int n = 0; n < 12; ++n) {
                float la = (float)n * PI * 2.0f / 12.0f;
                float diff = fabsf(fmodf(angle - la + PI * 3.0f, PI * 2.0f) - PI);
                line += expf(-diff * diff * 80.0f) * expf(-(d - 38.0f) * 0.06f);
            }

            if (line < 0.02f) continue;
            line = clampf(line, 0, 0.6f);

            int i = (y * SZ + x) * 4;
            float cr = img.data[i + 0] / 255.0f;
            float cg = img.data[i + 1] / 255.0f;
            float cb = img.data[i + 2] / 255.0f;
            float ca = img.data[i + 3] / 255.0f;
            cr += line * 0.7f; cg += line * 0.7f; cb += line * 0.9f;
            ca = std::max(ca, line);
            img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
        }
    }
}

// Tachyon: speed-streaked asymmetric glow
static void render_tachyon(Image& img, Color col) {
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = (float)x - CX;
            float dy = (float)y - CY;

            // Elongated along x-axis (motion blur effect)
            float stretch_x = dx * 0.7f;
            float d = sqrtf(stretch_x * stretch_x + dy * dy);

            float core = expf(-d * d * 0.004f);
            float trail = expf(-dy * dy * 0.01f) * expf(dx * 0.04f) * 0.3f;  // rightward trail
            trail = std::max(0.0f, trail);

            float intensity = core + trail;
            if (intensity < 0.02f) continue;

            float alpha = clampf(intensity, 0, 1);
            float white = core * 0.6f;
            float r = lerp(col.r, 1.0f, white) * intensity;
            float g = lerp(col.g, 1.0f, white) * intensity;
            float b = lerp(col.b, 1.0f, white) * intensity;

            img.set(x, y, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), alpha);
        }
    }
}

// ── Particle type definitions ────────────────────────────────────────────────

// Match PHYS_TYPE_NAMES exactly from ui_data.h
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

// Replicate type_to_filename() from particle_textures.cpp exactly
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
    {0.9f, 0.2f, 0.2f},   // Proton
    {0.7f, 0.7f, 0.7f},   // Neutron
    {0.2f, 0.5f, 1.0f},   // Electron
    {1.0f, 1.0f, 0.6f},   // Photon
    {1.0f, 0.3f, 0.8f},   // Positron
    {0.2f, 0.85f, 0.7f},  // Antiproton
    {0.6f, 0.9f, 0.6f},   // Neutrino_e
    {0.6f, 0.3f, 0.9f},   // Muon
    {0.8f, 0.5f, 1.0f},   // Anti-muon
    {0.4f, 0.2f, 0.7f},   // Tau
    {0.6f, 0.4f, 0.9f},   // Anti-tau
    {0.5f, 0.8f, 0.5f},   // Neutrino_mu
    {0.4f, 0.7f, 0.4f},   // Neutrino_tau
    {0.9f, 0.5f, 0.2f},   // Up
    {0.4f, 0.7f, 0.2f},   // Down
    {0.2f, 0.8f, 0.6f},   // Strange
    {0.9f, 0.8f, 0.2f},   // Charm
    {1.0f, 0.3f, 0.3f},   // Top
    {0.5f, 0.3f, 0.8f},   // Bottom
    {1.0f, 0.7f, 0.5f},   // Anti-up
    {0.7f, 0.9f, 0.5f},   // Anti-down
    {0.5f, 1.0f, 0.8f},   // Anti-strange
    {1.0f, 0.9f, 0.5f},   // Anti-charm
    {1.0f, 0.6f, 0.6f},   // Anti-top
    {0.7f, 0.6f, 1.0f},   // Anti-bottom
    {0.3f, 0.9f, 0.3f},   // Gluon
    {0.9f, 0.9f, 1.0f},   // W+
    {0.7f, 0.7f, 1.0f},   // W-
    {0.8f, 0.8f, 0.9f},   // Z0
    {1.0f, 0.85f, 0.3f},  // Higgs
    {0.7f, 0.8f, 1.0f},   // Graviton
    {0.3f, 0.1f, 0.5f},   // Dark Matter
    {0.6f, 0.1f, 0.2f},   // Dark Energy
    {0.4f, 0.2f, 0.6f},   // Axino
    {0.2f, 0.05f, 0.35f}, // WIMPzilla
    {0.35f, 0.25f, 0.55f},// SIMP
    {0.45f, 0.55f, 0.45f},// Sterile Neutrino
    {0.5f, 0.2f, 0.7f},   // Dark Photon
    {0.55f, 0.3f, 0.75f}, // Q-Ball
    {0.5f, 0.8f, 1.0f},   // Selectron
    {0.7f, 0.6f, 1.0f},   // Smuon
    {0.6f, 0.5f, 0.9f},   // Stau
    {1.0f, 0.7f, 0.4f},   // Squark
    {0.4f, 1.0f, 0.5f},   // Gluino
    {1.0f, 1.0f, 0.8f},   // Photino
    {0.9f, 0.9f, 1.0f},   // Wino
    {0.8f, 0.8f, 0.95f},  // Zino
    {1.0f, 0.9f, 0.6f},   // Higgsino
    {0.4f, 0.2f, 0.65f},  // Neutralino
    {0.55f, 0.85f, 0.55f},// Sneutrino
    {0.6f, 0.7f, 1.0f},   // Gravitino
    {1.0f, 0.4f, 0.4f},   // X Boson
    {1.0f, 0.5f, 0.3f},   // Y Boson
    {0.95f, 0.95f, 0.95f},// Monopole
    {0.7f, 0.6f, 0.4f},   // Radion
    {0.6f, 0.55f, 0.45f}, // Dilaton
    {0.0f, 1.0f, 1.0f},   // Tachyon
    {1.0f, 0.0f, 0.5f},   // Preon
    {1.0f, 0.7f, 0.1f},   // Inflaton
    {0.55f, 0.6f, 0.55f}, // Majoron
    {0.7f, 0.3f, 1.0f},   // Odderon
    {0.5f, 1.0f, 0.3f},   // Glueball
    {0.9f, 0.4f, 0.2f},   // Skyrmion
    {0.2f, 0.9f, 0.8f},   // X17
    {0.7f, 0.5f, 0.3f},   // Chameleon
    {0.9f, 0.2f, 1.0f},   // Paraparticle
    {0.4f, 0.7f, 0.95f},  // Dyn. Axion QP
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

        switch (t) {
        case 0: // Proton: red with uud quark structure
            render_nucleon(img, c, {1.0f, 0.5f, 0.2f}, {1.0f, 0.5f, 0.2f}, {0.4f, 0.7f, 0.2f});
            break;
        case 1: // Neutron: grey with udd quark structure
            render_nucleon(img, c, {1.0f, 0.5f, 0.2f}, {0.4f, 0.7f, 0.2f}, {0.4f, 0.7f, 0.2f});
            break;
        case 2: // Electron: bright blue sphere
            render_sphere(img, c, 36.0f, 0.65f, 0.5f, {0.5f, 0.8f, 1.0f});
            break;
        case 3: // Photon: golden starburst
            render_starburst(img, c, 8, 55.0f, 10.0f);
            break;
        case 4: // Positron: pink with antimatter ring
            render_antimatter(img, c, {1.0f, 0.8f, 1.0f});
            break;
        case 5: // Antiproton: cyan with antimatter ring and quark spots
            render_nucleon(img, c, {0.5f, 1.0f, 0.8f}, {0.7f, 0.9f, 0.5f}, {0.7f, 0.9f, 0.5f});
            // Overlay antimatter ring
            for (int y = 0; y < SZ; ++y)
                for (int x = 0; x < SZ; ++x) {
                    float dx = (float)x - CX, dy = (float)y - CY;
                    float d = sqrtf(dx * dx + dy * dy);
                    float ring_d = fabsf(d - 52.0f);
                    if (ring_d > 5.0f) continue;
                    float ring = expf(-ring_d * ring_d * 0.5f) * 0.5f;
                    int i = (y * SZ + x) * 4;
                    float cr = img.data[i + 0] / 255.0f + ring * 0.5f;
                    float cg = img.data[i + 1] / 255.0f + ring * 1.0f;
                    float cb = img.data[i + 2] / 255.0f + ring * 0.9f;
                    float ca = std::max(img.data[i + 3] / 255.0f, ring);
                    img.set(x, y, clampf(cr, 0, 1), clampf(cg, 0, 1), clampf(cb, 0, 1), clampf(ca, 0, 1));
                }
            break;
        case 6: case 11: case 12: // Neutrinos: ghostly
            render_ghost(img, c);
            break;
        case 7: // Muon: purple sphere
            render_sphere(img, c, 40.0f, 0.5f, 0.4f, {0.8f, 0.5f, 1.0f});
            break;
        case 8: // Anti-muon: pink-purple with ring
            render_antimatter(img, c, {1.0f, 0.7f, 1.0f});
            break;
        case 9: // Tau: dark purple sphere
            render_sphere(img, c, 44.0f, 0.5f, 0.4f, {0.6f, 0.3f, 0.9f});
            break;
        case 10: // Anti-tau: with ring
            render_antimatter(img, c, {0.8f, 0.6f, 1.0f});
            break;
        case 13: // Up quark
            render_quark(img, c, {1.0f, 0.3f, 0.2f}); break;
        case 14: // Down quark
            render_quark(img, c, {0.2f, 0.8f, 0.3f}); break;
        case 15: // Strange quark
            render_quark(img, c, {0.2f, 0.3f, 1.0f}); break;
        case 16: // Charm quark
            render_quark(img, c, {1.0f, 0.8f, 0.1f}); break;
        case 17: // Top quark
            render_quark(img, c, {1.0f, 0.2f, 0.2f}); break;
        case 18: // Bottom quark
            render_quark(img, c, {0.4f, 0.2f, 0.9f}); break;
        case 19: case 20: case 21: case 22: case 23: case 24: // Anti-quarks
            render_antimatter(img, c, {1.0f, 1.0f, 1.0f});
            break;
        case 25: // Gluon: green starburst with fewer rays
            render_starburst(img, c, 6, 48.0f, 12.0f);
            break;
        case 26: // W+: bright wave ring
            render_wave_ring(img, c, 6); break;
        case 27: // W-: wave ring
            render_wave_ring(img, c, 6); break;
        case 28: // Z0: wave ring
            render_wave_ring(img, c, 8); break;
        case 29: // Higgs: golden radiant
            render_higgs(img, c); break;
        case 30: // Graviton: faint blue starburst
            render_starburst(img, c, 4, 50.0f, 8.0f); break;
        case 31: // Dark matter: dark purple halo
            render_halo(img, c, 0.025f, 8.0f); break;
        case 32: // Dark energy: dark red expanding halo
            render_halo(img, c, 0.015f, 12.0f); break;
        case 33: case 34: case 35: // Axino, WIMPzilla, SIMP: dark exotic
            render_exotic(img, c, (float)t); break;
        case 36: // Sterile neutrino: ghost
            render_ghost(img, c); break;
        case 37: // Dark photon: purple starburst
            render_starburst(img, c, 6, 45.0f, 9.0f); break;
        case 38: // Q-Ball: exotic sphere
            render_exotic(img, c, (float)t); break;
        case 39: case 40: case 41: case 42: case 43: case 44: // SUSY: selectron through photino
            render_susy(img, c, (float)t); break;
        case 45: case 46: case 47: // Wino, Zino, Higgsino
            render_susy(img, c, (float)t); break;
        case 48: // Neutralino: dark SUSY
            render_susy(img, c, (float)t); break;
        case 49: // Sneutrino: ghostly SUSY
            render_susy(img, c, (float)t); break;
        case 50: // Gravitino: faint SUSY
            render_susy(img, c, (float)t); break;
        case 51: case 52: // X, Y bosons: bright exotic
            render_exotic(img, c, (float)t); break;
        case 53: // Monopole: with field lines
            render_monopole(img, c); break;
        case 54: case 55: // Radion, Dilaton: exotic spheres
            render_exotic(img, c, (float)t); break;
        case 56: // Tachyon: speed-streak
            render_tachyon(img, c); break;
        case 57: // Preon: hot pink sphere
            render_sphere(img, c, 30.0f, 0.7f, 0.5f, {1.0f, 0.5f, 0.8f}); break;
        case 58: // Inflaton: golden radiant like Higgs but bigger
            render_higgs(img, c); break;
        case 59: // Majoron: ghost
            render_ghost(img, c); break;
        case 60: // Odderon: exotic
            render_exotic(img, c, (float)t); break;
        case 61: // Glueball: green starburst
            render_starburst(img, c, 10, 42.0f, 14.0f); break;
        case 62: // Skyrmion: exotic sphere
            render_exotic(img, c, (float)t); break;
        case 63: // X17: cyan exotic
            render_exotic(img, c, (float)t); break;
        case 64: // Chameleon: exotic
            render_exotic(img, c, (float)t); break;
        case 65: // Paraparticle: exotic
            render_exotic(img, c, (float)t); break;
        case 66: // Dyn. Axion QP: exotic
            render_exotic(img, c, (float)t); break;
        default:
            render_sphere(img, c);
            break;
        }

        if (img.save(path.c_str())) {
            printf("[%2d/67] %s\n", t + 1, path.c_str());
            ++count;
        } else {
            fprintf(stderr, "FAILED: %s\n", path.c_str());
        }
    }

    printf("\nGenerated %d particle textures in assets/particles/\n", count);
    return 0;
}
