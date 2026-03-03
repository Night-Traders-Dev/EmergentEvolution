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

// ── Hash / noise functions ─────────────────────────────────────────────────

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

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // smoothstep

    float n = i.x + i.y * 157.0 + 113.0 * i.z;
    float a = hash11(n +   0.0);
    float b = hash11(n +   1.0);
    float c = hash11(n + 157.0);
    float d = hash11(n + 158.0);
    float e = hash11(n + 113.0);
    float ff = hash11(n + 114.0);
    float g = hash11(n + 270.0);
    float h = hash11(n + 271.0);

    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y),
               mix(mix(e, ff, f.x), mix(g, h, f.x), f.y), f.z);
}

float fbm(vec3 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < octaves; i++) {
        val += amp * noise3D(p * freq);
        freq *= 2.0;
        amp *= 0.5;
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

vec3 planet_surface(vec3 normal, Sphere hit, float time, vec3 primary_light_dir) {
    float seed = hit.planet_data.x;
    float surface_type = hit.planet_data.y;   // 0=rocky, 1=liquid, 2=frozen, 3=gas, 4=mixed
    float ocean_cov = hit.planet_data.z;       // 0-1
    float temperature = hit.planet_data.w;

    float cloud_cov = hit.atmo_data.x;        // 0-1
    float atm_press = hit.atmo_data.y;
    float vegetation = hit.atmo_data.z;        // 0-1
    float body_flags = hit.atmo_data.w;

    // Decode ocean type from body_flags
    float ocean_type = floor(mod(body_flags / 8.0, 8.0)); // 0=none,1=water,2=methane,3=ammonia,4=lava

    // Spherical coordinates for noise seeding
    vec3 np = normal * 4.0 + seed * 0.01;

    // ── Gas giant path ──────────────────────────────────────────────────
    if (surface_type > 2.5 && surface_type < 3.5) {
        float lat = asin(normal.y);
        float band_count = 8.0 + mod(seed, 6.0);
        float band = sin(lat * band_count + fbm(normal * 2.0 + seed * 0.01, 3) * 1.5);

        vec3 col1 = vec3(0.8, 0.65, 0.4);  // warm band
        vec3 col2 = vec3(0.6, 0.45, 0.3);  // dark band
        if (temperature < 150.0) { // ice giant
            col1 = vec3(0.3, 0.5, 0.7);
            col2 = vec3(0.15, 0.3, 0.55);
        } else if (temperature > 1000.0) { // hot jupiter
            col1 = vec3(0.9, 0.5, 0.2);
            col2 = vec3(0.7, 0.2, 0.1);
        }

        vec3 col = mix(col1, col2, band * 0.5 + 0.5);

        // Great spot feature
        float spot_lat = sin(seed * 1.23) * 0.5;
        float spot_lon = cos(seed * 2.34) * 3.14;
        vec3 spot_dir = vec3(cos(spot_lat)*cos(spot_lon), sin(spot_lat), cos(spot_lat)*sin(spot_lon));
        float spot = smoothstep(0.92, 0.96, dot(normal, spot_dir));
        col = mix(col, col * 1.4, spot);

        // Swirling detail
        float swirl = fbm(normal * 8.0 + vec3(time * 0.02, 0, 0) + seed * 0.01, 4);
        col += (swirl - 0.5) * 0.15;

        return col;
    }

    // ── Solid body path ─────────────────────────────────────────────────
    float elevation = fbm(np, 5);
    float sea_level = 1.0 - ocean_cov;

    // Base terrain color
    vec3 terrain_col;
    if (surface_type < 0.5) {
        // Rocky
        terrain_col = mix(vec3(0.35, 0.28, 0.22), vec3(0.55, 0.50, 0.42), elevation);
        terrain_col = mix(terrain_col, vec3(0.7, 0.65, 0.6), smoothstep(0.7, 0.85, elevation)); // peaks
    } else if (surface_type > 0.5 && surface_type < 1.5) {
        // Liquid world
        terrain_col = vec3(0.1, 0.25, 0.5);
    } else if (surface_type > 1.5 && surface_type < 2.5) {
        // Frozen
        terrain_col = mix(vec3(0.7, 0.75, 0.85), vec3(0.9, 0.92, 0.95), elevation);
        terrain_col = mix(terrain_col, vec3(0.5, 0.55, 0.65), smoothstep(0.3, 0.1, elevation)); // valleys
    } else {
        // Mixed
        terrain_col = mix(vec3(0.4, 0.32, 0.25), vec3(0.6, 0.55, 0.48), elevation);
    }

    // Ocean layer
    vec3 col = terrain_col;
    if (elevation < sea_level && ocean_cov > 0.01) {
        float depth_factor = smoothstep(sea_level, sea_level - 0.15, elevation);
        vec3 ocean_col;
        if (ocean_type < 1.5) {
            // Water
            ocean_col = mix(vec3(0.05, 0.2, 0.45), vec3(0.1, 0.35, 0.6), 1.0 - depth_factor);
            float waves = noise3D(normal * 20.0 + vec3(time * 0.3, 0, time * 0.2)) * 0.05;
            ocean_col += waves;
        } else if (ocean_type < 2.5) {
            // Methane
            ocean_col = mix(vec3(0.05, 0.15, 0.2), vec3(0.1, 0.25, 0.3), 1.0 - depth_factor);
        } else if (ocean_type < 3.5) {
            // Ammonia
            ocean_col = mix(vec3(0.25, 0.22, 0.1), vec3(0.35, 0.3, 0.15), 1.0 - depth_factor);
        } else {
            // Lava
            float lava_pulse = 0.8 + 0.2 * sin(time * 2.0 + noise3D(normal * 5.0) * 6.0);
            ocean_col = vec3(0.9, 0.3, 0.05) * lava_pulse;
        }
        col = mix(col, ocean_col, depth_factor);
    }

    // Temperature-reactive effects
    if (temperature < 200.0 && surface_type > 3.5) {
        // Ice caps at poles for mixed worlds
        float polar = abs(normal.y);
        float ice = smoothstep(0.5, 0.8, polar);
        col = mix(col, vec3(0.85, 0.9, 0.95), ice);
    }
    if (temperature > 800.0 && surface_type < 0.5) {
        // Lava fissures on hot rocky worlds
        float cracks = smoothstep(0.48, 0.5, fbm(normal * 12.0 + seed * 0.02, 3));
        col = mix(col, vec3(0.9, 0.3, 0.05), cracks * 0.6);
    }

    // Vegetation
    if (vegetation > 0.01 && elevation >= sea_level) {
        float veg_noise = fbm(normal * 6.0 + seed * 0.03, 3);
        float veg_mask = smoothstep(0.3, 0.6, veg_noise) * vegetation;
        // Less vegetation at poles and peaks
        veg_mask *= smoothstep(0.7, 0.4, abs(normal.y));
        veg_mask *= smoothstep(0.85, 0.65, elevation);
        col = mix(col, vec3(0.15, 0.4, 0.1), veg_mask);
    }

    // Cloud layer
    if (cloud_cov > 0.01) {
        float cloud_noise = fbm(normal * 5.0 + vec3(time * 0.05, time * 0.02, 0) + seed * 0.04, 4);
        float cloud_mask = smoothstep(1.0 - cloud_cov, 1.0 - cloud_cov * 0.5, cloud_noise);
        vec3 cloud_col = vec3(0.9, 0.92, 0.95);
        col = mix(col, cloud_col, cloud_mask * 0.7);
    }

    // City lights on dark side
    bool has_planet_flag = mod(body_flags, 2.0) > 0.5;
    if (has_planet_flag && vegetation > 0.05) {
        float ndl = dot(normal, primary_light_dir);
        if (ndl < -0.1) {
            float darkness = smoothstep(0.0, -0.3, ndl);
            float city_noise = noise3D(normal * 40.0 + seed * 0.05);
            float cities = step(0.92, city_noise) * darkness;
            // Only on land
            if (elevation >= sea_level) {
                col += vec3(1.0, 0.9, 0.6) * cities * 0.4;
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
        atm_col = vec3(0.4, 0.5, 0.7); // cold haze
    else if (temperature > 500.0)
        atm_col = vec3(0.7, 0.4, 0.2); // hot CO2-heavy
    else
        atm_col = vec3(0.3, 0.5, 0.9); // earthlike blue

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
    bool is_planet_body = hit.atmo_data.w > 0.5; // body_flags > 0 means planet or moon
    if (is_planet_body) {
        base_color = planet_surface(normal, hit, time, primary_light_dir);
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
