#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;
    vec4 eye_pos;
    vec4 screen_info;
    vec4 lighting_params; // x=star, y=uniform, z=ambient, w=fastStar
    vec4 quality_params;  // x=quality, y=hq
    vec4 render_flags;    // x=background, y=corona, z=comet tails, w=bh lensing
};

struct Sphere {
    vec4 pos_radius;
    vec4 base_emit;
    vec4 class_seed_temp;
    vec4 terrain_params;
    vec4 material_params;
    vec4 composition_params;
    vec4 atmosphere_params;
    vec4 activity_params;
};

layout(std430, set = 0, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

const int RENDER_STAR = 0;
const int RENDER_PLANET = 1;
const int RENDER_MOON = 2;
const int RENDER_ASTEROID = 3;
const int RENDER_COMET = 4;
const int RENDER_BLACK_HOLE = 5;

const int SURF_ROCKY = 0;
const int SURF_LIQUID = 1;
const int SURF_FROZEN = 2;
const int SURF_GAS = 3;
const int SURF_MIXED = 4;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

vec3 hash31(float p) {
    vec3 p3 = fract(vec3(p) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    return mix(mix(mix(dot(hash33(i + vec3(0,0,0)), f - vec3(0,0,0)),
                       dot(hash33(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                   mix(dot(hash33(i + vec3(0,1,0)), f - vec3(0,1,0)),
                       dot(hash33(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash33(i + vec3(0,0,1)), f - vec3(0,0,1)),
                       dot(hash33(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                   mix(dot(hash33(i + vec3(0,1,1)), f - vec3(0,1,1)),
                       dot(hash33(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}

int adjusted_octaves(int base_octaves) {
    int quality = int(quality_params.x + 0.5);
    if (quality <= 0) return max(1, base_octaves - 2);
    if (quality == 1) return max(1, base_octaves - 1);
    return base_octaves;
}

float fbm(vec3 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    int octs = adjusted_octaves(octaves);
    for (int i = 0; i < octs; i++) {
        val += amp * noise3D(p * freq);
        freq *= 2.03;
        amp *= 0.49;
    }
    return val * 0.5 + 0.5;
}

float ridged_noise(vec3 p) {
    float n = noise3D(p);
    n = 1.0 - abs(n);
    return n * n;
}

float ridged_fbm(vec3 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    int octs = adjusted_octaves(octaves);
    for (int i = 0; i < octs; i++) {
        val += amp * ridged_noise(p * freq);
        freq *= 2.1;
        amp *= 0.45;
    }
    return val;
}

vec3 blackbody_tint(float temperature) {
    float t = clamp((temperature - 1200.0) / 32000.0, 0.0, 1.0);
    return vec3(
        clamp(1.25 - t * 0.95, 0.0, 1.0),
        clamp(0.45 + t * 0.60, 0.0, 1.0),
        clamp(-0.10 + t * 1.25, 0.0, 1.0)
    );
}

float phase_hg(float g, float cos_theta) {
    float g2 = g * g;
    return (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1.0e-3), 1.5));
}

void build_basis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = normalize(cross(n, t));
}

float intersect_sphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float sq = sqrt(disc);
    float t = -b - sq;
    if (t > 0.001) return t;
    t = -b + sq;
    if (t > 0.001) return t;
    return -1.0;
}

float irregular_radius_scale(vec3 dir, float seed, float roughness) {
    float n = noise3D(dir * 6.0 + vec3(seed * 0.021, seed * 0.017, seed * 0.013));
    return clamp(1.0 + n * roughness, 0.65, 1.45);
}

float irregular_sdf(vec3 p, vec3 center, float base_radius, float seed, float roughness) {
    vec3 d = normalize(p - center);
    float target_r = base_radius * irregular_radius_scale(d, seed, roughness);
    return length(p - center) - target_r;
}

float intersect_irregular_body(vec3 ro, vec3 rd, vec3 center, float base_radius,
                               float seed, float roughness) {
    float bound_r = base_radius * (1.0 + roughness * 1.3);
    float t0 = intersect_sphere(ro, rd, center, bound_r);
    if (t0 < 0.0) return -1.0;

    float t = max(t0 - base_radius * roughness, 0.001);
    for (int i = 0; i < 28; i++) {
        vec3 p = ro + rd * t;
        float d = irregular_sdf(p, center, base_radius, seed, roughness);
        if (d < 0.001) return t;
        t += max(d * 0.65, 0.002);
        if (t > t0 + base_radius * (2.0 + roughness * 3.0)) break;
    }
    return -1.0;
}

vec3 irregular_normal(vec3 p, vec3 center, float base_radius, float seed, float roughness) {
    float e = max(base_radius * 0.008, 0.02);
    vec2 h = vec2(e, 0.0);
    float dx = irregular_sdf(p + vec3(h.x, h.y, h.y), center, base_radius, seed, roughness)
             - irregular_sdf(p - vec3(h.x, h.y, h.y), center, base_radius, seed, roughness);
    float dy = irregular_sdf(p + vec3(h.y, h.x, h.y), center, base_radius, seed, roughness)
             - irregular_sdf(p - vec3(h.y, h.x, h.y), center, base_radius, seed, roughness);
    float dz = irregular_sdf(p + vec3(h.y, h.y, h.x), center, base_radius, seed, roughness)
             - irregular_sdf(p - vec3(h.y, h.y, h.x), center, base_radius, seed, roughness);
    return normalize(vec3(dx, dy, dz));
}

float crater_mask(vec3 normal, float seed, float density) {
    vec3 p = normal * (6.0 + density * 14.0) + hash31(seed) * 9.0;
    float base = fbm(p, 4);
    float pits = smoothstep(0.62, 0.88, base);
    float rims = smoothstep(0.48, 0.60, base) - smoothstep(0.60, 0.72, base);
    return clamp(pits * density + rims * density * 0.5, 0.0, 1.0);
}

float starfield_layer(vec3 rd, float scale, float threshold) {
    vec3 p = normalize(rd) * scale;
    float n = fbm(p + vec3(17.0, 31.0, 47.0), 3);
    float s = smoothstep(threshold, 1.0, n);
    return s * s;
}

vec3 sample_starfield(vec3 rd) {
    float stars1 = starfield_layer(rd, 48.0, 0.86);
    float stars2 = starfield_layer(rd * 1.7 + 0.3, 110.0, 0.90);
    float twinkle = 0.9 + 0.1 * sin(screen_info.w * 0.2 + rd.x * 100.0);
    vec3 tint = mix(vec3(0.7, 0.75, 0.85), vec3(1.0, 0.95, 0.8), hash11(rd.x * 71.0 + rd.y * 39.0 + rd.z * 113.0));
    return tint * (stars1 * 1.1 + stars2 * 0.8) * twinkle;
}

vec3 background(vec3 rd) {
    if (render_flags.x < 0.5) return vec3(0.0);

    float galactic = pow(clamp(1.0 - abs(rd.y + noise3D(rd * 2.0) * 0.18), 0.0, 1.0), 4.0);
    float nebula = fbm(rd * 7.0 + vec3(1.3, -2.1, 0.7), 4);
    vec3 bg = vec3(0.0008, 0.0011, 0.0018);
    bg += vec3(0.035, 0.030, 0.045) * galactic * (0.25 + nebula * 0.75);
    bg += vec3(0.020, 0.022, 0.028) * pow(galactic, 2.0);
    bg += sample_starfield(rd);
    return bg;
}

float ocean_type_from_temp(float temperature) {
    if (temperature < 115.0) return 2.0;
    if (temperature < 210.0) return 3.0;
    if (temperature > 650.0) return 4.0;
    return 1.0;
}

vec3 atmosphere_scatter(vec3 normal, vec3 view_dir, vec3 light_dir, vec3 base_col,
                        float pressure, float haze_density, float rayleigh_strength, float mie_strength,
                        float temperature) {
    if (pressure < 0.01) return vec3(0.0);
    float fresnel = pow(clamp(1.0 - max(dot(normal, -view_dir), 0.0), 0.0, 1.0), 2.5);
    float forward = phase_hg(clamp(0.25 + mie_strength * 0.45, 0.0, 0.85), dot(-view_dir, light_dir));
    vec3 ray_col = mix(vec3(0.55, 0.70, 1.0), vec3(0.90, 0.55, 0.35), clamp((temperature - 240.0) / 600.0, 0.0, 1.0));
    ray_col *= 0.25 + rayleigh_strength * 0.9;
    vec3 mie_col = mix(vec3(1.0), base_col * 1.1, 0.35);
    float density = clamp(pressure * 0.05 + haze_density * 0.9, 0.0, 1.2);
    return ray_col * fresnel * density + mie_col * forward * mie_strength * density * 0.8;
}

vec3 shade_star(vec3 normal, vec3 rd, Sphere hit) {
    float seed = hit.class_seed_temp.x;
    float temperature = hit.class_seed_temp.w;
    float gran_amp = hit.terrain_params.x;
    float gran_freq = hit.terrain_params.y;
    float spot_strength = hit.activity_params.w;
    float flare_activity = hit.activity_params.x;
    float corona_strength = hit.activity_params.y;

    vec3 tint = blackbody_tint(temperature);
    vec3 nwarp = normal * gran_freq + hash31(seed) * 15.0;
    float granulation = fbm(nwarp, 4);
    float cells = smoothstep(0.44, 0.64, granulation);
    vec3 gran_col = mix(tint * 0.85, tint * 1.22, cells);

    float cool_star = clamp((6500.0 - temperature) / 4000.0, 0.0, 1.0);
    float spots = smoothstep(0.58, 0.78, fbm(normal * (gran_freq * 0.55) + hash31(seed * 1.9) * 11.0, 3));
    float facula = smoothstep(0.42, 0.58, fbm(normal * (gran_freq * 0.7) + hash31(seed * 2.7) * 9.0, 3));
    gran_col = mix(gran_col, gran_col * (0.42 + 0.2 * cool_star), spots * spot_strength * cool_star);
    gran_col += tint * facula * spot_strength * cool_star * 0.14;

    float mu = max(dot(normal, -rd), 0.0);
    float limb_dark = mix(0.45, 0.75, clamp(temperature / 18000.0, 0.0, 1.0));
    vec3 col = gran_col * mix(limb_dark, 1.0, pow(mu, 0.65));

    float flare = 0.92 + flare_activity * 0.08 * sin(screen_info.w * (1.2 + flare_activity * 1.8) + seed * 0.13);
    col *= flare;

    if (render_flags.y > 0.5) {
        float edge = pow(clamp(1.0 - mu, 0.0, 1.0), 2.0);
        col += tint * corona_strength * edge * 0.65;
    }

    return col * (1.1 + hit.base_emit.a * 0.35);
}

vec3 shade_gas_giant(vec3 normal, Sphere hit, vec3 light_dir) {
    float seed = hit.class_seed_temp.x;
    float temperature = hit.class_seed_temp.w;
    float band_freq = 6.0 + hit.terrain_params.y;
    vec3 seed_offset = hash31(seed) * 80.0;
    float lat = asin(clamp(normal.y, -1.0, 1.0));
    float warp = noise3D(normal * 3.0 + seed_offset * 0.13) * 0.8;
    float band = sin(lat * band_freq + warp + fbm(normal * 2.2 + seed_offset * 0.05, 4) * 1.4);

    vec3 warm;
    vec3 cool;
    if (temperature < 170.0) {
        warm = vec3(0.35, 0.58, 0.72);
        cool = vec3(0.10, 0.22, 0.45);
    } else if (temperature > 900.0) {
        warm = vec3(0.95, 0.48, 0.20);
        cool = vec3(0.56, 0.15, 0.08);
    } else {
        warm = vec3(0.82, 0.68, 0.45);
        cool = vec3(0.54, 0.40, 0.25);
    }

    vec3 col = mix(warm, cool, band * 0.5 + 0.5);
    float storm = fbm(normal * 8.0 + vec3(screen_info.w * 0.02, screen_info.w * -0.014, 0.0) + seed_offset * 0.04, 5);
    col += (storm - 0.5) * 0.15;

    vec3 spot_dir = normalize(vec3(cos(seed * 0.01), sin(seed * 0.003) * 0.35, sin(seed * 0.01)));
    float spot = smoothstep(0.91, 0.97, dot(normal, spot_dir));
    col = mix(col, col * vec3(1.3, 0.7, 0.6), spot * 0.9);

    float ndl = max(dot(normal, light_dir), 0.0);
    return col * (0.28 + 0.72 * ndl);
}

vec3 shade_planet_surface(vec3 normal, Sphere hit, vec3 rd, vec3 light_dir,
                          out float roughness_out, out vec3 surf_normal) {
    float seed = hit.class_seed_temp.x;
    float surface_type = hit.class_seed_temp.z;
    float temperature = hit.class_seed_temp.w;
    float terrain_amp = hit.terrain_params.x;
    float terrain_freq = hit.terrain_params.y;
    float ridge_amp = hit.terrain_params.z;
    float rock_frac = hit.composition_params.x;
    float ice_frac = hit.composition_params.y;
    float metal_frac = hit.composition_params.z;
    float ocean_cov = hit.composition_params.w;
    float cloud_cov = hit.atmosphere_params.x;
    float volcanic = hit.activity_params.z;

    vec3 seed_offset = hash31(seed) * 120.0;
    vec3 np = normal * max(terrain_freq, 1.0) + seed_offset;
    vec3 warp = vec3(
        noise3D(np + vec3(0.0, 5.2, 1.3)),
        noise3D(np + vec3(5.2, 1.3, 0.0)),
        noise3D(np + vec3(1.3, 0.0, 5.2))
    );
    vec3 warped_np = np + warp * terrain_amp * 0.85;
    float elev = clamp(fbm(warped_np, 6) * (1.0 - ridge_amp * 0.35) + ridged_fbm(warped_np * 0.8, 4) * ridge_amp * 0.55, 0.0, 1.0);

    vec3 tangent;
    vec3 bitangent;
    build_basis(normal, tangent, bitangent);
    float eps = 0.004;
    float e_du = fbm((normal + tangent * eps) * terrain_freq + seed_offset, 4) - elev;
    float e_dv = fbm((normal + bitangent * eps) * terrain_freq + seed_offset, 4) - elev;
    surf_normal = normalize(normal + (tangent * e_du + bitangent * e_dv) * hit.material_params.w * 10.0);

    if (surface_type > 2.5 && surface_type < 3.5)
        return shade_gas_giant(normal, hit, light_dir);

    float sea_level = 1.0 - clamp(ocean_cov, 0.0, 1.0);
    vec3 rock_dark = mix(vec3(0.20, 0.17, 0.15), vec3(0.38, 0.29, 0.20), rock_frac);
    vec3 rock_mid = mix(vec3(0.42, 0.35, 0.28), vec3(0.58, 0.46, 0.32), rock_frac);
    vec3 rock_bright = mix(vec3(0.72, 0.68, 0.60), vec3(0.82, 0.74, 0.55), metal_frac + 0.2);
    vec3 ice_col = vec3(0.72, 0.82, 0.95);
    vec3 col = mix(rock_dark, rock_mid, smoothstep(0.15, 0.5, elev));
    col = mix(col, rock_bright, smoothstep(0.60, 0.85, elev));

    if (surface_type > 1.5 && surface_type < 2.5)
        col = mix(col, ice_col, 0.68 + ice_frac * 0.22);
    else if (surface_type > 3.5)
        col = mix(col, vec3(0.16, 0.32, 0.12), 0.22);

    if (temperature < 240.0)
        col = mix(col, ice_col, smoothstep(0.55, 0.92, abs(normal.y)) * clamp(ice_frac + 0.25, 0.0, 1.0));

    bool is_ocean = ocean_cov > 0.01 && elev < sea_level;
    roughness_out = hit.material_params.x;
    if (is_ocean) {
        float ocean_type = ocean_type_from_temp(temperature);
        float depth = smoothstep(sea_level, sea_level - 0.25, elev);
        vec3 ocean_col;
        if (ocean_type < 1.5) {
            ocean_col = mix(vec3(0.03, 0.12, 0.34), vec3(0.06, 0.30, 0.55), 1.0 - depth);
            roughness_out = 0.10;
        } else if (ocean_type < 2.5) {
            ocean_col = mix(vec3(0.02, 0.08, 0.12), vec3(0.08, 0.20, 0.24), 1.0 - depth);
            roughness_out = 0.12;
        } else if (ocean_type < 3.5) {
            ocean_col = mix(vec3(0.18, 0.18, 0.10), vec3(0.30, 0.26, 0.15), 1.0 - depth);
            roughness_out = 0.14;
        } else {
            float glow = 0.7 + 0.3 * sin(screen_info.w * 1.7 + fbm(warped_np * 1.2, 3) * 6.0);
            ocean_col = mix(vec3(0.45, 0.08, 0.02), vec3(1.00, 0.55, 0.08), fbm(warped_np * 1.8, 4)) * glow;
            roughness_out = 0.22;
        }
        col = mix(col, ocean_col, depth);
    }

    if (volcanic > 0.01 && temperature > 650.0 && surface_type < 0.5) {
        float cracks = smoothstep(0.46, 0.54, fbm(warped_np * 0.7 + 4.0, 4));
        float lava_glow = 0.65 + 0.35 * sin(screen_info.w * 1.3 + seed * 0.1 + cracks * 5.0);
        col = mix(col, vec3(1.0, 0.42, 0.08) * lava_glow, cracks * volcanic * 0.7);
    }

    if (cloud_cov > 0.01 && surface_type < 3.5) {
        float shadow = smoothstep(0.55, 0.85, fbm(normal * 4.0 + seed_offset * 0.03 + vec3(screen_info.w * 0.02), 5));
        col *= 1.0 - shadow * cloud_cov * 0.18;
    }

    float ndl = max(dot(surf_normal, light_dir), 0.0);
    return col * (0.25 + 0.75 * ndl);
}

vec3 shade_moon_surface(vec3 normal, Sphere hit, vec3 rd, vec3 light_dir,
                        out float roughness_out, out vec3 surf_normal) {
    vec3 col = shade_planet_surface(normal, hit, rd, light_dir, roughness_out, surf_normal);
    float seed = hit.class_seed_temp.x;
    float crater = crater_mask(normal, seed, hit.terrain_params.w);
    vec3 regolith = mix(vec3(0.18, 0.18, 0.20), vec3(0.70, 0.72, 0.78), hit.composition_params.y);
    col = mix(col, regolith, crater * 0.55);
    col *= 0.92 - crater * 0.12;
    if (hit.composition_params.y > 0.35) {
        float fractures = ridged_fbm(normal * 18.0 + hash31(seed) * 8.0, 4);
        col += vec3(0.10, 0.14, 0.18) * smoothstep(0.35, 0.65, fractures) * 0.35;
        roughness_out = max(0.22, roughness_out - 0.08);
    }
    return col;
}

vec3 shade_asteroid_surface(vec3 normal, Sphere hit, vec3 light_dir, bool is_comet_body, out float roughness_out) {
    float seed = hit.class_seed_temp.x;
    float subtype = hit.class_seed_temp.z;
    float crater = crater_mask(normal, seed, hit.terrain_params.w);
    float chip = fbm(normal * 9.0 + hash31(seed) * 6.0, 4);
    vec3 col;

    if (is_comet_body) {
        col = mix(vec3(0.10, 0.09, 0.08), vec3(0.72, 0.78, 0.86), smoothstep(0.55, 0.9, chip) * hit.composition_params.y);
        col = mix(col, vec3(0.20, 0.18, 0.16), crater * 0.35);
        roughness_out = 0.75;
    } else if (subtype < 0.5) {
        col = vec3(0.14, 0.12, 0.11);
        roughness_out = 0.92;
    } else if (subtype < 1.5) {
        col = vec3(0.46, 0.39, 0.31);
        roughness_out = 0.82;
    } else if (subtype < 2.5) {
        col = vec3(0.56, 0.58, 0.60);
        roughness_out = 0.42;
    } else {
        col = vec3(0.68, 0.74, 0.82);
        roughness_out = 0.62;
    }

    col *= mix(0.7, 1.15, chip);
    col = mix(col, col * 0.55, crater * 0.5);
    float weathering = 0.5 + 0.5 * dot(normal, normalize(vec3(0.7, 0.2, -0.4)));
    col *= mix(0.88, 1.06, weathering);
    float ndl = max(dot(normal, light_dir), 0.0);
    return col * (0.22 + 0.78 * ndl);
}

vec3 specular_term(vec3 n, vec3 l, vec3 v, float roughness, float specular, float metallic, vec3 light_color) {
    vec3 h = normalize(l + v);
    float shininess = mix(96.0, 10.0, clamp(roughness, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), shininess);
    return light_color * spec * (specular + metallic * 0.35);
}

bool compute_primary_light(vec3 hit_pos, int body_count, int skip_idx,
                           out int primary_idx, out vec3 light_dir, out vec3 light_color) {
    float best_power = 0.0;
    primary_idx = -1;
    light_dir = normalize(vec3(0.5, 0.8, 0.3));
    light_color = vec3(1.0);

    for (int i = 0; i < body_count && i < 512; i++) {
        if (i == skip_idx) continue;
        if (spheres[i].base_emit.a <= 0.0) continue;
        vec3 to_light = spheres[i].pos_radius.xyz - hit_pos;
        float dist2 = max(dot(to_light, to_light), 1.0);
        float power = spheres[i].base_emit.a * spheres[i].pos_radius.w / dist2;
        if (power > best_power) {
            best_power = power;
            primary_idx = i;
            light_dir = normalize(to_light);
            light_color = spheres[i].base_emit.rgb * spheres[i].base_emit.a;
        }
    }
    return primary_idx >= 0;
}

bool in_shadow(vec3 origin, vec3 light_dir, float light_dist, int body_count, int self_idx, int light_idx) {
    for (int j = 0; j < body_count && j < 512; j++) {
        if (j == self_idx || j == light_idx) continue;
        float st = intersect_sphere(origin, light_dir, spheres[j].pos_radius.xyz, spheres[j].pos_radius.w);
        if (st > 0.0 && st < light_dist) return true;
    }
    return false;
}

vec3 black_hole_effect(vec3 ro, vec3 rd, Sphere bh, bool hit_horizon, out float alpha_out) {
    vec3 center = bh.pos_radius.xyz;
    float radius = bh.pos_radius.w;
    vec3 to_center = center - ro;
    float t_closest = max(dot(to_center, rd), 0.0);
    vec3 closest = ro + rd * t_closest;
    vec3 rel = closest - center;
    float influence = length(rel);
    float influence_radius = radius * 6.0;

    alpha_out = 0.0;
    if (!hit_horizon && influence > influence_radius) return vec3(0.0);

    float lens = (render_flags.w > 0.5) ? bh.activity_params.w : 0.0;
    vec3 center_dir = normalize(to_center);
    float bend = clamp((influence_radius - influence) / max(influence_radius, 0.001), 0.0, 1.0) * lens * 0.55;
    int warp_steps = int(3 + max(0.0, quality_params.x - 1.0) * 2.0);
    vec3 lensed_dir = rd;
    for (int i = 0; i < 5; i++) {
        if (i >= warp_steps) break;
        lensed_dir = normalize(mix(lensed_dir, center_dir, bend * 0.35));
    }

    vec3 col = background(lensed_dir);
    float disk_plane = abs(rel.y);
    float disk_rad = length(rel.xz);
    float inner = smoothstep(radius * 1.15, radius * 1.7, disk_rad);
    float outer = 1.0 - smoothstep(radius * 3.9, radius * 4.8, disk_rad);
    float plane = 1.0 - smoothstep(radius * 0.04, radius * 0.34, disk_plane);
    float disk_mask = inner * outer * plane;
    vec3 disk_tangent = normalize(vec3(-rel.z, 0.0, rel.x) + vec3(1.0e-4));
    float doppler = 0.65 + 0.55 * clamp(dot(disk_tangent, -rd), -1.0, 1.0);
    vec3 disk_col = mix(vec3(1.00, 0.30, 0.04), vec3(1.00, 0.95, 0.82), smoothstep(radius * 1.5, radius * 3.8, disk_rad));
    disk_col *= bh.activity_params.y * doppler;
    col += disk_col * disk_mask * 1.8;

    float ring = exp(-pow((influence - radius * 1.18) / max(radius * 0.14, 0.001), 2.0));
    col += vec3(1.0, 0.92, 0.82) * ring * (0.8 + bh.activity_params.y);

    float jet_axis = 1.0 - smoothstep(radius * 0.10, radius * 0.7, length(rel.xz));
    float jet_height = smoothstep(radius * 1.2, radius * 4.0, abs(rel.y));
    col += vec3(0.42, 0.72, 1.0) * jet_axis * jet_height * bh.activity_params.z * 0.55;

    if (hit_horizon) {
        float edge = 1.0 - max(dot(normalize(closest - center), -rd), 0.0);
        col = mix(col, vec3(0.0), 0.92);
        col += vec3(0.3, 0.18, 0.5) * pow(edge, 4.0) * 0.25;
        alpha_out = 1.0;
    } else {
        alpha_out = clamp((influence_radius - influence) / max(influence_radius - radius, 0.001), 0.0, 1.0);
    }
    return col;
}

void main() {
    float W = screen_info.x;
    float H = screen_info.y;
    int body_count = int(screen_info.z);

    bool use_star_lighting = lighting_params.x > 0.5;
    bool use_uniform_lighting = lighting_params.y > 0.5;
    float ambient = lighting_params.z;
    bool use_fast_star_lighting = lighting_params.w > 0.5;

    vec2 ndc = fragUV * 2.0 - 1.0;
    ndc.y = -ndc.y;

    vec4 near_clip = vec4(ndc, 0.0, 1.0);
    vec4 far_clip = vec4(ndc, 1.0, 1.0);
    vec4 near_world = inv_vp * near_clip;
    vec4 far_world = inv_vp * far_clip;
    near_world /= near_world.w;
    far_world /= far_world.w;

    vec3 ro = eye_pos.xyz;
    vec3 rd = normalize(far_world.xyz - near_world.xyz);

    float closest_t = 1.0e30;
    int closest_idx = -1;

    for (int i = 0; i < body_count && i < 512; i++) {
        int render_class = int(spheres[i].class_seed_temp.y + 0.5);
        float t = -1.0;
        if (render_class == RENDER_ASTEROID || render_class == RENDER_COMET) {
            float roughness = (render_class == RENDER_COMET) ? 0.34 : 0.22;
            t = intersect_irregular_body(ro, rd, spheres[i].pos_radius.xyz, spheres[i].pos_radius.w,
                                         spheres[i].class_seed_temp.x, roughness);
        } else {
            t = intersect_sphere(ro, rd, spheres[i].pos_radius.xyz, spheres[i].pos_radius.w);
        }
        if (t > 0.0 && t < closest_t) {
            closest_t = t;
            closest_idx = i;
        }
    }

    if (closest_idx < 0) {
        vec3 miss_col = background(rd);
        for (int i = 0; i < body_count && i < 512; i++) {
            if (int(spheres[i].class_seed_temp.y + 0.5) != RENDER_BLACK_HOLE) continue;
            float effect_alpha = 0.0;
            vec3 effect = black_hole_effect(ro, rd, spheres[i], false, effect_alpha);
            miss_col = mix(miss_col, effect, effect_alpha);
        }
        outColor = vec4(pow(max(miss_col, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    Sphere hit = spheres[closest_idx];
    int render_class = int(hit.class_seed_temp.y + 0.5);
    vec3 hit_pos = ro + rd * closest_t;
    vec3 normal = normalize(hit_pos - hit.pos_radius.xyz);
    if (render_class == RENDER_ASTEROID || render_class == RENDER_COMET) {
        float irregularity = (render_class == RENDER_COMET) ? 0.34 : 0.22;
        normal = irregular_normal(hit_pos, hit.pos_radius.xyz, hit.pos_radius.w, hit.class_seed_temp.x, irregularity);
    }

    if (render_class == RENDER_BLACK_HOLE || hit.base_emit.a < -0.5) {
        float alpha = 1.0;
        vec3 bh = black_hole_effect(ro, rd, hit, true, alpha);
        outColor = vec4(pow(max(bh, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 primary_light_dir = normalize(vec3(0.5, 0.8, 0.3));
    vec3 primary_light_color = vec3(1.0);
    int primary_light_idx = -1;
    bool has_primary_light = compute_primary_light(hit_pos, body_count, closest_idx,
                                                   primary_light_idx, primary_light_dir, primary_light_color);

    if (render_class == RENDER_STAR || hit.base_emit.a > 0.0) {
        vec3 star_col = shade_star(normal, rd, hit);
        outColor = vec4(pow(max(star_col, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 base_color = hit.base_emit.rgb;
    float roughness = hit.material_params.x;
    vec3 surf_normal = normal;

    if (render_class == RENDER_PLANET) {
        base_color = shade_planet_surface(normal, hit, rd, primary_light_dir, roughness, surf_normal);
    } else if (render_class == RENDER_MOON) {
        base_color = shade_moon_surface(normal, hit, rd, primary_light_dir, roughness, surf_normal);
    } else if (render_class == RENDER_ASTEROID || render_class == RENDER_COMET) {
        base_color = shade_asteroid_surface(normal, hit, primary_light_dir, render_class == RENDER_COMET, roughness);
    }

    vec3 final_color = vec3(0.0);
    vec3 view_dir = normalize(eye_pos.xyz - hit_pos);

    if (use_uniform_lighting) {
        float hemi = 0.5 + 0.5 * surf_normal.y;
        vec3 sky_color = vec3(0.62, 0.68, 0.78);
        vec3 ground_color = vec3(0.14, 0.12, 0.10);
        final_color = base_color * mix(ground_color, sky_color, hemi);
        final_color += specular_term(surf_normal, normalize(vec3(0.4, 0.8, 0.2)), view_dir,
                                     roughness, hit.material_params.z, hit.material_params.y, vec3(0.45));
    }

    if (use_star_lighting) {
        final_color += base_color * ambient;
        if (has_primary_light && primary_light_idx != closest_idx) {
            vec3 to_light = spheres[primary_light_idx].pos_radius.xyz - hit_pos;
            float light_dist = length(to_light);
            vec3 L = to_light / max(light_dist, 0.001);
            vec3 shadow_origin = hit_pos + surf_normal * 0.1;
            bool shadowed = in_shadow(shadow_origin, L, light_dist, body_count, closest_idx, primary_light_idx);
            if (!shadowed) {
                float atten = 1.0 / (1.0 + (light_dist * light_dist) /
                    (spheres[primary_light_idx].pos_radius.w * spheres[primary_light_idx].pos_radius.w * 400.0));
                float ndl = max(dot(surf_normal, L), 0.0);
                final_color += base_color * primary_light_color * ndl * atten;
                final_color += specular_term(surf_normal, L, view_dir, roughness,
                                             hit.material_params.z, hit.material_params.y,
                                             primary_light_color) * atten;
            }
        } else if (!use_fast_star_lighting && quality_params.x >= 2.0) {
            for (int i = 0; i < body_count && i < 512; i++) {
                if (spheres[i].base_emit.a <= 0.0 || i == closest_idx) continue;
                vec3 to_light = spheres[i].pos_radius.xyz - hit_pos;
                float light_dist = length(to_light);
                vec3 L = to_light / max(light_dist, 0.001);
                vec3 shadow_origin = hit_pos + surf_normal * 0.1;
                if (in_shadow(shadow_origin, L, light_dist, body_count, closest_idx, i)) continue;
                float atten = 1.0 / (1.0 + (light_dist * light_dist) /
                    (spheres[i].pos_radius.w * spheres[i].pos_radius.w * 400.0));
                vec3 light_col = spheres[i].base_emit.rgb * spheres[i].base_emit.a;
                float ndl = max(dot(surf_normal, L), 0.0);
                final_color += base_color * light_col * ndl * atten;
            }
        }
    }

    if (!use_star_lighting && !use_uniform_lighting)
        final_color = base_color * 0.12;

    if (render_class == RENDER_PLANET || render_class == RENDER_MOON) {
        final_color += atmosphere_scatter(normal, rd, primary_light_dir, base_color,
                                          hit.atmosphere_params.y, hit.atmosphere_params.z,
                                          hit.atmosphere_params.w, hit.activity_params.w,
                                          hit.class_seed_temp.w);
        final_color += vec3(0.18, 0.6, 0.42) * hit.activity_params.y *
                       pow(clamp(1.0 - abs(normal.y), 0.0, 1.0), 6.0);
    }

    if (render_class == RENDER_COMET) {
        float edge = pow(clamp(1.0 - max(dot(normal, -rd), 0.0), 0.0, 1.0), 2.0);
        float phase = phase_hg(0.55, dot(-rd, primary_light_dir));
        final_color += vec3(0.65, 0.78, 0.95) * hit.activity_params.y * edge * phase * 6.0;
        if (render_flags.z > 0.5 && has_primary_light) {
            vec3 anti_sun = -primary_light_dir;
            float align = pow(max(dot(normalize(hit_pos - hit.pos_radius.xyz), anti_sun), 0.0), 2.0);
            float tail = pow(clamp(1.0 - max(dot(-rd, anti_sun), 0.0), 0.0, 1.0), 1.5);
            final_color += vec3(0.55, 0.72, 0.95) * hit.activity_params.z * align * tail * 0.9;
        }
    }

    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
