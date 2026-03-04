#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── Camera / scene uniforms ────────────────────────────────────────────────

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;            // inverse view-projection matrix
    vec4 eye_pos;           // xyz = camera position
    vec4 screen_info;       // x = width, y = height, z = body_count, w = time
    vec4 lighting_params;   // x = star_lighting, y = uniform_lighting, z = ambient, w = unused
};

// ── Sphere data ────────────────────────────────────────────────────────────

struct Sphere {
    vec4 pos_radius;    // xyz = world position, w = radius
    vec4 color_emit;    // rgb = base color (0-1), a = emissive intensity
    vec4 planet_data;   // x = seed, y = surface_type, z = ocean_coverage, w = temperature
    vec4 atmo_data;     // x = cloud_coverage, y = atm_pressure, z = vegetation, w = body_flags
};

layout(std430, set = 0, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

// ── Noise functions (quintic interpolation, gradient-quality) ──────────────

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453);
}

// Gradient noise with quintic interpolation (C2 continuous — no grid artifacts)
float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    // Quintic interpolation: f(t) = 6t^5 - 15t^4 + 10t^3 (C2 continuous)
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

float fbm(vec3 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < octaves; i++) {
        val += amp * noise3D(p * freq);
        freq *= 2.03;   // slight frequency jitter prevents axis-aligned patterns
        amp *= 0.49;
    }
    return val * 0.5 + 0.5; // remap [-1,1] gradient noise to [0,1]
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
    for (int i = 0; i < octaves; i++) {
        val += amp * ridged_noise(p * freq);
        freq *= 2.1;
        amp *= 0.45;
    }
    return val;
}

// ── Ray-sphere intersection ────────────────────────────────────────────────

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

// ── Procedural starfield background ────────────────────────────────────────

vec3 background(vec3 rd) {
    vec3 col = vec3(0.008, 0.012, 0.03);

    for (int layer = 0; layer < 3; layer++) {
        float scale = 800.0 + float(layer) * 400.0;
        vec2 cell = floor(rd.xz * scale / (abs(rd.y) + 0.3));
        float h = hash(cell + float(layer) * 137.0);
        if (h > 0.97) {
            float brightness = (h - 0.97) / 0.03;
            brightness *= brightness;
            float twinkle = 0.7 + 0.3 * sin(screen_info.w * 2.0 + h * 100.0);
            vec3 star_col = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.95, 0.8), hash(cell + 73.0));
            col += star_col * brightness * twinkle * 0.4;
        }
    }

    return col;
}

// ── Procedural planet surface ──────────────────────────────────────────────

vec3 planet_surface(vec3 normal, Sphere hit, float time, vec3 primary_light_dir,
                     out float surface_roughness, out float surface_elevation) {
    float seed = hit.planet_data.x;
    float surface_type = hit.planet_data.y;   // 0=rocky, 1=liquid, 2=frozen, 3=gas, 4=mixed
    float ocean_cov = hit.planet_data.z;       // 0-1
    float temperature = hit.planet_data.w;

    float cloud_cov = hit.atmo_data.x;        // 0-1
    float atm_press = hit.atmo_data.y;
    float vegetation = hit.atmo_data.z;        // 0-1
    float body_flags = hit.atmo_data.w;

    // Decode ocean type from body_flags
    float ocean_type = floor(mod(body_flags / 8.0, 8.0));

    // ── Per-planet unique properties derived from seed ──
    // Each planet gets a unique position in noise space + unique color palette
    vec3 seed_offset = hash31(seed) * 200.0;
    float hue_var  = hash11(seed * 1.37) * 0.4 - 0.2;   // color hue shift -0.2 to +0.2
    float sat_var  = 0.6 + hash11(seed * 2.71) * 0.4;    // saturation 0.6-1.0
    float freq_var = 5.0 + hash11(seed * 3.14) * 6.0;    // terrain frequency 5-11
    float rough_var = 0.2 + hash11(seed * 4.56) * 0.8;   // terrain roughness 0.2-1.0

    // Noise sampling point — unique per planet, no grid artifacts
    vec3 np = normal * freq_var + seed_offset;

    surface_roughness = 0.8;
    surface_elevation = 0.5;

    // ── Gas giant path ──────────────────────────────────────────────────
    if (surface_type > 2.5 && surface_type < 3.5) {
        float lat = asin(normal.y);
        float band_count = 7.0 + hash11(seed * 5.0) * 8.0; // 7-15 bands

        // Domain-warped bands for more organic look
        float warp = noise3D(normal * 3.0 + seed_offset * 0.1) * 0.8;
        float band = sin(lat * band_count + warp + fbm(normal * 2.5 + seed_offset * 0.05, 3) * 1.2);

        // Per-planet gas giant colors from seed
        vec3 col_warm, col_cool;
        float color_sel = hash11(seed * 6.78);
        if (temperature < 150.0) {
            // Ice giant (Uranus/Neptune) — blues and greens
            col_warm = vec3(0.25 + color_sel * 0.15, 0.45 + color_sel * 0.15, 0.65);
            col_cool = vec3(0.10, 0.25 + color_sel * 0.10, 0.50 + color_sel * 0.10);
        } else if (temperature > 1000.0) {
            // Hot jupiter — reds and oranges
            col_warm = vec3(0.85 + color_sel * 0.1, 0.40 + color_sel * 0.15, 0.15);
            col_cool = vec3(0.65, 0.15 + color_sel * 0.1, 0.05);
        } else {
            // Warm gas giant — varied palettes from seed
            if (color_sel < 0.33) {
                // Jupiter-like (orange/cream/brown)
                col_warm = vec3(0.80, 0.65, 0.40);
                col_cool = vec3(0.55, 0.40, 0.25);
            } else if (color_sel < 0.66) {
                // Saturn-like (gold/tan)
                col_warm = vec3(0.85, 0.75, 0.50);
                col_cool = vec3(0.65, 0.55, 0.35);
            } else {
                // Exotic (purple/rose tints)
                col_warm = vec3(0.70, 0.55, 0.60);
                col_cool = vec3(0.45, 0.35, 0.50);
            }
        }

        vec3 col = mix(col_warm, col_cool, band * 0.5 + 0.5);

        // Great spot feature
        float spot_lat = hash11(seed * 1.23) * 0.8 - 0.4;
        float spot_lon = hash11(seed * 2.34) * 6.28;
        vec3 spot_dir = vec3(cos(spot_lat)*cos(spot_lon), sin(spot_lat), cos(spot_lat)*sin(spot_lon));
        float spot_size = 0.88 + hash11(seed * 3.45) * 0.08; // 0.88-0.96
        float spot = smoothstep(spot_size, spot_size + 0.04, dot(normal, spot_dir));
        vec3 spot_col = mix(col * 0.7, col * 1.8, hash11(seed * 4.56));
        col = mix(col, spot_col, spot);

        // Swirling detail with domain warping
        vec3 swirl_offset = vec3(time * 0.015, time * 0.008, time * -0.01);
        float swirl = fbm(normal * 8.0 + swirl_offset + seed_offset * 0.05, 5);
        col += (swirl - 0.5) * 0.18;

        // Turbulent eddies
        float eddy = fbm(normal * 18.0 + vec3(time * 0.01, time * -0.012, 0) + seed_offset * 0.03, 3);
        col += (eddy - 0.5) * 0.06;

        surface_roughness = 1.0;
        return col;
    }

    // ── Solid body path ─────────────────────────────────────────────────

    // Domain warping for organic terrain shapes
    vec3 warp = vec3(
        noise3D(np + vec3(0.0, 5.2, 1.3)),
        noise3D(np + vec3(5.2, 1.3, 0.0)),
        noise3D(np + vec3(1.3, 0.0, 5.2))
    );
    vec3 warped_np = np + warp * 0.7 * rough_var;

    // Multi-octave terrain with domain warping
    float base_elev = fbm(warped_np, 6);

    // Ridged noise for mountain ranges (intensity varies per planet)
    float ridged = ridged_fbm(warped_np * 0.8, 4) * rough_var;

    // Combine base terrain with ridged mountains
    float elevation = base_elev * (1.0 - rough_var * 0.4) + ridged * rough_var * 0.5;

    // Add fine detail at multiple scales
    float fine = noise3D(warped_np * 3.0) * 0.06 + noise3D(warped_np * 6.0) * 0.03;
    elevation += fine;
    elevation = clamp(elevation, 0.0, 1.0);

    surface_elevation = elevation;
    float sea_level = 1.0 - ocean_cov;

    // Normal perturbation for terrain lighting
    float eps = 0.004;
    vec3 tangent_u = normalize(cross(normal, vec3(0.0, 1.0, 0.1)));
    vec3 tangent_v = normalize(cross(normal, tangent_u));
    float elev_du = fbm((normal + tangent_u * eps) * freq_var + seed_offset, 6) - base_elev;
    float elev_dv = fbm((normal + tangent_v * eps) * freq_var + seed_offset, 6) - base_elev;
    vec3 perturbed_normal = normalize(normal + (tangent_u * elev_du + tangent_v * elev_dv) * 10.0);

    // Terrain self-shadowing
    float terrain_ndl = max(dot(perturbed_normal, primary_light_dir), 0.0);
    float terrain_shadow = 0.25 + 0.75 * terrain_ndl;

    // ── Base terrain color (seed-derived palettes for variety) ──
    vec3 terrain_col;

    if (surface_type < 0.5) {
        // ROCKY — many visual subtypes from seed
        float rocky_style = hash11(seed * 7.89);
        vec3 valley_col, mid_col, peak_col;
        if (rocky_style < 0.25) {
            // Mars-like (rust red/orange)
            valley_col = vec3(0.35 + hue_var, 0.18, 0.10);
            mid_col    = vec3(0.55 + hue_var, 0.30, 0.18);
            peak_col   = vec3(0.70, 0.55, 0.42);
        } else if (rocky_style < 0.5) {
            // Slate/Moon-like (gray)
            valley_col = vec3(0.18, 0.18, 0.20 + hue_var * 0.3);
            mid_col    = vec3(0.38, 0.37, 0.40 + hue_var * 0.2);
            peak_col   = vec3(0.62, 0.60, 0.65);
        } else if (rocky_style < 0.75) {
            // Sandy/desert (tan/gold)
            valley_col = vec3(0.40 + hue_var, 0.32, 0.15);
            mid_col    = vec3(0.60 + hue_var, 0.50, 0.28);
            peak_col   = vec3(0.78, 0.72, 0.55);
        } else {
            // Dark volcanic (charcoal with hints)
            valley_col = vec3(0.12, 0.10, 0.10 + hue_var * 0.3);
            mid_col    = vec3(0.22, 0.20, 0.22 + hue_var * 0.2);
            peak_col   = vec3(0.40, 0.38, 0.42);
        }

        terrain_col = mix(valley_col, mid_col, smoothstep(0.15, 0.45, elevation));
        terrain_col = mix(terrain_col, peak_col, smoothstep(0.55, 0.78, elevation));

        // Snow on highest peaks (if cold enough)
        if (temperature < 450.0) {
            vec3 snow_col = vec3(0.90, 0.92, 0.95);
            float snow_line = 0.70 + (temperature - 200.0) / 600.0 * 0.2;
            terrain_col = mix(terrain_col, snow_col, smoothstep(snow_line, snow_line + 0.12, elevation));
        }

        // Cliff striations (subtle)
        float strata = sin(elevation * 30.0 + noise3D(warped_np * 2.0) * 4.0) * 0.5 + 0.5;
        terrain_col *= 0.92 + strata * 0.08;

    } else if (surface_type > 0.5 && surface_type < 1.5) {
        // LIQUID WORLD — deep ocean everywhere
        float water_style = hash11(seed * 8.12);
        if (water_style < 0.5) {
            terrain_col = vec3(0.04 + hue_var * 0.1, 0.12, 0.35); // deep blue
        } else {
            terrain_col = vec3(0.06, 0.18 + hue_var * 0.1, 0.28);  // teal
        }
        surface_roughness = 0.1;

    } else if (surface_type > 1.5 && surface_type < 2.5) {
        // FROZEN — varied ice types from seed
        float ice_style = hash11(seed * 9.34);
        vec3 ice_deep, ice_mid, ice_bright;
        if (ice_style < 0.33) {
            // Europa-like (smooth cream/tan ice)
            ice_deep   = vec3(0.50 + hue_var * 0.2, 0.45, 0.35);
            ice_mid    = vec3(0.70, 0.65, 0.55);
            ice_bright = vec3(0.85, 0.82, 0.75);
        } else if (ice_style < 0.66) {
            // Classic blue-white ice
            ice_deep   = vec3(0.35, 0.45 + hue_var * 0.2, 0.65);
            ice_mid    = vec3(0.70, 0.78, 0.90);
            ice_bright = vec3(0.90, 0.93, 0.97);
        } else {
            // Enceladus-like (bright white with blue hints)
            ice_deep   = vec3(0.55, 0.58, 0.68 + hue_var * 0.2);
            ice_mid    = vec3(0.80, 0.83, 0.90);
            ice_bright = vec3(0.95, 0.96, 0.98);
        }

        terrain_col = mix(ice_deep, ice_mid, smoothstep(0.15, 0.5, elevation));
        terrain_col = mix(terrain_col, ice_bright, smoothstep(0.65, 0.88, elevation));

        // Crevasses and fracture lines
        float crack_n = noise3D(warped_np * 2.5);
        float crevasse = smoothstep(0.52, 0.48, crack_n);
        vec3 crack_col = ice_deep * 0.5;
        terrain_col = mix(terrain_col, crack_col, crevasse * 0.5 * (1.0 - elevation));

        surface_roughness = 0.3;

    } else {
        // MIXED — earth-like with varied palettes
        float mix_style = hash11(seed * 10.56);
        vec3 lowland, highland, peak;
        if (mix_style < 0.33) {
            // Green temperate
            lowland  = vec3(0.22 + hue_var * 0.1, 0.28, 0.12);
            highland = vec3(0.42, 0.38 + hue_var * 0.1, 0.28);
            peak     = vec3(0.60, 0.58, 0.52);
        } else if (mix_style < 0.66) {
            // Arid brown/tan
            lowland  = vec3(0.38 + hue_var * 0.15, 0.30, 0.18);
            highland = vec3(0.55, 0.45 + hue_var * 0.1, 0.30);
            peak     = vec3(0.72, 0.66, 0.55);
        } else {
            // Cool/gray with green valleys
            lowland  = vec3(0.20, 0.24 + hue_var * 0.1, 0.18);
            highland = vec3(0.38, 0.36, 0.35 + hue_var * 0.15);
            peak     = vec3(0.58, 0.56, 0.60);
        }

        terrain_col = mix(lowland, highland, smoothstep(0.25, 0.55, elevation));
        terrain_col = mix(terrain_col, peak, smoothstep(0.70, 0.88, elevation));
    }

    // Apply terrain lighting
    vec3 col = terrain_col * terrain_shadow;

    // ── Ocean layer ──
    bool is_ocean = (elevation < sea_level && ocean_cov > 0.01);
    if (is_ocean) {
        float depth_factor = smoothstep(sea_level, sea_level - 0.25, elevation);
        vec3 ocean_col;
        float wave_spec = 0.0;

        if (ocean_type < 1.5) {
            // Water — dynamic waves with specular
            float water_hue = hash11(seed * 11.1);
            vec3 deep_water    = mix(vec3(0.01, 0.06, 0.22), vec3(0.02, 0.10, 0.18), water_hue);
            vec3 shallow_water = mix(vec3(0.04, 0.22, 0.50), vec3(0.06, 0.28, 0.42), water_hue);
            vec3 shore_water   = vec3(0.12 + water_hue * 0.08, 0.38, 0.58);

            ocean_col = mix(shallow_water, deep_water, depth_factor);

            // Shore foam — narrow band near coastline
            float shore_dist = smoothstep(sea_level - 0.015, sea_level, elevation);
            float foam_noise = noise3D(normal * 25.0 + vec3(time * 0.3, 0, time * 0.2)) * 0.5 + 0.5;
            ocean_col = mix(ocean_col, shore_water + vec3(0.2) * foam_noise, shore_dist);

            // Animated waves (multi-scale)
            float wave1 = noise3D(normal * 18.0 + vec3(time * 0.35, time * 0.12, time * -0.18));
            float wave2 = noise3D(normal * 35.0 + vec3(time * -0.25, time * 0.4, time * 0.08));
            float wave3 = noise3D(normal * 70.0 + vec3(time * 0.5, time * -0.35, 0));
            float waves = wave1 * 0.05 + wave2 * 0.025 + wave3 * 0.012;
            ocean_col += waves;

            // Specular highlight on water
            vec3 wave_normal = normalize(perturbed_normal +
                tangent_u * (wave1 * 0.12) + tangent_v * (wave2 * 0.12));
            vec3 R = reflect(-primary_light_dir, wave_normal);
            vec3 V = normalize(eye_pos.xyz - (hit.pos_radius.xyz + normal * hit.pos_radius.w));
            float spec = pow(max(dot(R, V), 0.0), 80.0);
            wave_spec = spec * 0.6 * (1.0 - depth_factor * 0.4);

        } else if (ocean_type < 2.5) {
            // Methane — dark teal with slow ripples
            float meth_var = hash11(seed * 12.2);
            vec3 deep_meth = vec3(0.02, 0.08 + meth_var * 0.04, 0.12);
            vec3 shal_meth = vec3(0.06, 0.16 + meth_var * 0.06, 0.22);
            ocean_col = mix(shal_meth, deep_meth, depth_factor);
            float ripple = noise3D(normal * 14.0 + vec3(time * 0.08, 0, time * 0.06)) * 0.035;
            ocean_col += ripple;

        } else if (ocean_type < 3.5) {
            // Ammonia — pale yellow-green
            float amm_var = hash11(seed * 13.3);
            ocean_col = mix(vec3(0.22, 0.20 + amm_var * 0.05, 0.08),
                            vec3(0.30, 0.26, 0.10 + amm_var * 0.04), 1.0 - depth_factor);
            float ripple = noise3D(normal * 10.0 + vec3(time * 0.05, time * 0.03, 0)) * 0.025;
            ocean_col += ripple;

        } else {
            // Lava — animated glow with fissures
            float lava_flow = fbm(normal * 5.0 + vec3(time * 0.06, time * 0.04, 0) + seed_offset * 0.02, 4);
            float lava_pulse = 0.7 + 0.3 * sin(time * 1.8 + lava_flow * 7.0);
            vec3 lava_cool = vec3(0.50, 0.08, 0.0);
            vec3 lava_hot  = vec3(1.00, 0.50, 0.05);
            ocean_col = mix(lava_cool, lava_hot, lava_flow) * lava_pulse;

            // Bright fissure lines
            float fissure = smoothstep(0.47, 0.53, fbm(normal * 10.0 + seed_offset * 0.03, 3));
            ocean_col = mix(ocean_col, vec3(1.0, 0.80, 0.25) * 1.5, fissure * 0.45);
        }

        col = mix(col, ocean_col, depth_factor);
        col += vec3(1.0, 0.95, 0.9) * wave_spec;
        surface_roughness = 0.15;
    }

    // ── Temperature-reactive effects ──
    if (temperature < 260.0 && (surface_type > 3.5 || surface_type < 0.5)) {
        // Ice caps at poles
        float polar = abs(normal.y);
        float ice_line = mix(0.25, 0.80, clamp((temperature - 80.0) / 180.0, 0.0, 1.0));
        float ice = smoothstep(ice_line, ice_line + 0.12, polar);
        vec3 ice_col = vec3(0.86, 0.90, 0.95);
        float ice_detail = noise3D(normal * 22.0 + seed_offset * 0.1) * 0.08;
        col = mix(col, ice_col + ice_detail, ice * 0.85);
    }
    if (temperature > 700.0 && surface_type < 0.5) {
        // Lava fissures on hot rocky worlds
        float crack_noise = fbm(warped_np * 0.7, 4);
        float cracks = smoothstep(0.45, 0.53, crack_noise);
        float glow = 0.7 + 0.3 * sin(time * 1.5 + crack_noise * 5.0);
        col = mix(col, vec3(1.0, 0.40, 0.05) * glow, cracks * 0.65);
        col += vec3(0.25, 0.04, 0.0) * cracks * 0.35;
    }

    // ── Vegetation ──
    if (vegetation > 0.01 && elevation >= sea_level && !is_ocean) {
        float veg_noise = fbm(warped_np * 0.7 + vec3(3.7), 4);
        float veg_detail = noise3D(warped_np * 2.5 + vec3(7.1));
        float veg_mask = smoothstep(0.20, 0.55, veg_noise) * vegetation;

        // Less vegetation at poles and on peaks
        veg_mask *= smoothstep(0.78, 0.30, abs(normal.y));
        veg_mask *= smoothstep(0.82, 0.50, elevation);
        // More vegetation in lowlands near water
        float near_water = 1.0 - smoothstep(sea_level, sea_level + 0.12, elevation);
        veg_mask *= 0.6 + near_water * 0.6;

        // Varied greens from seed
        float green_var = hash11(seed * 14.4);
        vec3 forest_dark  = vec3(0.06 + green_var * 0.04, 0.22 + green_var * 0.08, 0.04);
        vec3 forest_light = vec3(0.15, 0.42 + green_var * 0.1, 0.08 + green_var * 0.04);
        vec3 veg_col = mix(forest_dark, forest_light, veg_detail * 0.5 + 0.5);
        col = mix(col, veg_col, clamp(veg_mask, 0.0, 1.0));
        surface_roughness = 0.9;
    }

    // ── Cloud layer ──
    if (cloud_cov > 0.01) {
        // Domain-warped cloud patterns
        vec3 cloud_warp = vec3(
            noise3D(normal * 2.0 + vec3(time * 0.02) + seed_offset * 0.03),
            noise3D(normal * 2.0 + vec3(0, time * 0.015, 0) + seed_offset * 0.04),
            0.0
        );
        vec3 cloud_np = normal * 4.0 + cloud_warp * 0.5 + seed_offset * 0.02;

        float cloud_base = fbm(cloud_np + vec3(time * 0.025, time * 0.012, 0), 5);
        float cloud_mid  = fbm(cloud_np * 2.0 + vec3(time * 0.05, time * -0.025, time * 0.015), 4);
        float cloud_fine = noise3D(cloud_np * 5.0 + vec3(time * 0.08, time * 0.04, 0));

        float cloud_density = cloud_base * 0.50 + cloud_mid * 0.35 + (cloud_fine * 0.5 + 0.5) * 0.15;
        float threshold = 1.0 - cloud_cov;
        float cloud_mask = smoothstep(threshold - 0.08, threshold + 0.18, cloud_density);

        // Cloud shading
        float cloud_ndl = max(dot(normal, primary_light_dir), 0.0);
        vec3 cloud_lit   = vec3(0.95, 0.96, 0.98);
        vec3 cloud_shade = vec3(0.50, 0.53, 0.60);
        vec3 cloud_col = mix(cloud_shade, cloud_lit, cloud_ndl * 0.7 + 0.3);
        cloud_col *= 1.0 - cloud_mask * 0.12; // self-shadowing

        // Cloud shadow on surface
        col *= 1.0 - cloud_mask * 0.25 * max(cloud_ndl, 0.0);

        col = mix(col, cloud_col, cloud_mask * 0.88);
    }

    // ── City lights on dark side ──
    bool has_planet_flag = mod(body_flags, 2.0) > 0.5;
    if (has_planet_flag && vegetation > 0.05) {
        float ndl = dot(normal, primary_light_dir);
        if (ndl < -0.1) {
            float darkness = smoothstep(0.0, -0.3, ndl);
            float city_n1 = noise3D(normal * 45.0 + seed_offset * 0.04);
            float city_n2 = noise3D(normal * 90.0 + seed_offset * 0.06);
            float cities = step(0.45, city_n1) * darkness;     // gradient noise is [-1,1] remapped
            float city_clusters = step(0.38, city_n2) * step(0.35, city_n1) * darkness * 0.5;
            if (elevation >= sea_level) {
                col += vec3(1.0, 0.90, 0.50) * cities * 0.4;
                col += vec3(1.0, 0.80, 0.40) * city_clusters * 0.25;
            }
        }
    }

    return col;
}

// ── Atmospheric rim glow ───────────────────────────────────────────────────

vec3 atmosphere_glow(vec3 normal, vec3 view_dir, float atm_pressure, float temperature) {
    if (atm_pressure < 0.01) return vec3(0.0);

    float fresnel = 1.0 - abs(dot(normal, -view_dir));
    fresnel = pow(fresnel, 3.0);
    float intensity = min(atm_pressure * 0.3, 1.0) * fresnel;

    vec3 atm_col;
    if (temperature < 200.0)
        atm_col = vec3(0.4, 0.5, 0.7);   // cold haze
    else if (temperature > 500.0)
        atm_col = vec3(0.7, 0.4, 0.2);   // hot CO2-heavy
    else
        atm_col = vec3(0.3, 0.5, 0.9);   // earthlike blue

    return atm_col * intensity;
}

// ── Main raytracing ────────────────────────────────────────────────────────

void main() {
    float W = screen_info.x;
    float H = screen_info.y;
    int body_count = int(screen_info.z);
    float time = screen_info.w;

    bool use_star_lighting    = lighting_params.x > 0.5;
    bool use_uniform_lighting = lighting_params.y > 0.5;
    float ambient = lighting_params.z;

    // ── Reconstruct ray from pixel ─────────────────────────────────────────
    vec2 ndc = fragUV * 2.0 - 1.0;
    ndc.y = -ndc.y;

    vec4 near_clip = vec4(ndc, 0.0, 1.0);
    vec4 far_clip  = vec4(ndc, 1.0, 1.0);

    vec4 near_world = inv_vp * near_clip;
    vec4 far_world  = inv_vp * far_clip;
    near_world /= near_world.w;
    far_world  /= far_world.w;

    vec3 ro = eye_pos.xyz;
    vec3 rd = normalize(far_world.xyz - near_world.xyz);

    // ── Trace all spheres ──────────────────────────────────────────────────
    float closest_t = 1e30;
    int closest_idx = -1;

    for (int i = 0; i < body_count && i < 512; i++) {
        float t = intersect_sphere(ro, rd,
            spheres[i].pos_radius.xyz,
            spheres[i].pos_radius.w);
        if (t > 0.0 && t < closest_t) {
            closest_t = t;
            closest_idx = i;
        }
    }

    // ── No hit — background ────────────────────────────────────────────────
    if (closest_idx < 0) {
        outColor = vec4(background(rd), 1.0);
        return;
    }

    // ── Hit — compute shading ──────────────────────────────────────────────
    Sphere hit = spheres[closest_idx];
    vec3 hit_pos = ro + rd * closest_t;
    vec3 normal = normalize(hit_pos - hit.pos_radius.xyz);
    vec3 base_color = hit.color_emit.rgb;
    float emissive = hit.color_emit.a;

    // Find primary light direction (nearest/brightest star)
    vec3 primary_light_dir = normalize(vec3(0.5, 0.8, 0.3));
    float best_light_power = 0.0;
    for (int i = 0; i < body_count && i < 512; i++) {
        if (spheres[i].color_emit.a <= 0.0) continue;
        vec3 to_light = spheres[i].pos_radius.xyz - hit_pos;
        float dist2 = dot(to_light, to_light);
        float power = spheres[i].color_emit.a * spheres[i].pos_radius.w / max(dist2, 1.0);
        if (power > best_light_power) {
            best_light_power = power;
            primary_light_dir = normalize(to_light);
        }
    }

    // Stars and other emissive bodies — render self-lit with corona
    if (emissive > 0.0) {
        vec3 col = base_color * emissive;
        float ndv = max(dot(normal, -rd), 0.0);
        col *= 0.6 + 0.4 * ndv;
        col += base_color * 0.3 * pow(ndv, 0.5);
        outColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    // ── Check if this is a planet/moon with procedural texturing ──────────
    bool is_planet_body = hit.atmo_data.w > 0.5;
    float planet_roughness = 0.8;
    float planet_elev = 0.5;
    if (is_planet_body) {
        base_color = planet_surface(normal, hit, time, primary_light_dir,
                                     planet_roughness, planet_elev);
    }

    vec3 final_color = vec3(0.0);

    // ── Uniform lighting mode ──────────────────────────────────────────────
    if (use_uniform_lighting) {
        float hemi = 0.5 + 0.5 * normal.y;
        vec3 sky_color = vec3(0.6, 0.65, 0.75);
        vec3 ground_color = vec3(0.15, 0.12, 0.1);
        vec3 light = mix(ground_color, sky_color, hemi);
        final_color = base_color * light;

        vec3 fill_dir = normalize(vec3(0.5, 0.8, 0.3));
        float fill_ndl = max(dot(normal, fill_dir), 0.0);
        final_color += base_color * fill_ndl * 0.3;
    }

    // ── Star lighting mode ─────────────────────────────────────────────────
    if (use_star_lighting) {
        final_color += base_color * ambient;

        for (int i = 0; i < body_count && i < 512; i++) {
            if (spheres[i].color_emit.a <= 0.0) continue;
            if (i == closest_idx) continue;

            vec3 light_pos = spheres[i].pos_radius.xyz;
            float light_radius = spheres[i].pos_radius.w;
            vec3 light_color = spheres[i].color_emit.rgb * spheres[i].color_emit.a;

            vec3 to_light = light_pos - hit_pos;
            float light_dist = length(to_light);
            vec3 L = to_light / light_dist;

            vec3 shadow_origin = hit_pos + normal * 0.1;
            bool in_shadow = false;
            for (int j = 0; j < body_count && j < 512; j++) {
                if (j == closest_idx || j == i) continue;
                float st = intersect_sphere(shadow_origin, L,
                    spheres[j].pos_radius.xyz,
                    spheres[j].pos_radius.w);
                if (st > 0.0 && st < light_dist) {
                    in_shadow = true;
                    break;
                }
            }

            if (!in_shadow) {
                float atten = 1.0 / (1.0 + (light_dist * light_dist) /
                    (light_radius * light_radius * 400.0));

                float ndl = max(dot(normal, L), 0.0);
                final_color += base_color * light_color * ndl * atten;

                vec3 V = normalize(eye_pos.xyz - hit_pos);
                vec3 H = normalize(L + V);
                float spec = pow(max(dot(normal, H), 0.0), 32.0);
                final_color += light_color * spec * atten * 0.3;
            }
        }
    }

    if (!use_star_lighting && !use_uniform_lighting) {
        final_color = base_color * 0.1;
    }

    // Black hole special
    if (emissive < -0.5) {
        float edge = 1.0 - abs(dot(normal, -rd));
        final_color = vec3(0.01, 0.0, 0.02) + vec3(0.4, 0.1, 0.6) * pow(edge, 4.0) * 0.5;
    }

    // Atmospheric rim glow for planets/moons
    if (is_planet_body) {
        final_color += atmosphere_glow(normal, rd, hit.atmo_data.y, hit.planet_data.w);
    }

    // Gamma correction
    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
