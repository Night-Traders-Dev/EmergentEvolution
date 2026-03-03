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
    vec4 color_emit;    // rgb = base color (0–1), a = emissive intensity (>0 for stars)
};

layout(std430, set = 0, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

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

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 background(vec3 rd) {
    // Dark space with procedural stars
    vec3 col = vec3(0.008, 0.012, 0.03);

    // Star layers at different scales
    for (int layer = 0; layer < 3; layer++) {
        float scale = 800.0 + float(layer) * 400.0;
        vec2 cell = floor(rd.xz * scale / (abs(rd.y) + 0.3));
        float h = hash(cell + float(layer) * 137.0);
        if (h > 0.97) {
            float brightness = (h - 0.97) / 0.03;
            brightness *= brightness;
            // Twinkle
            float twinkle = 0.7 + 0.3 * sin(screen_info.w * 2.0 + h * 100.0);
            // Slight color variation
            vec3 star_col = mix(vec3(0.8, 0.85, 1.0), vec3(1.0, 0.95, 0.8), hash(cell + 73.0));
            col += star_col * brightness * twinkle * 0.4;
        }
    }

    return col;
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
    ndc.y = -ndc.y;  // flip Y (Vulkan convention)

    // Near and far points in clip space
    vec4 near_clip = vec4(ndc, 0.0, 1.0);
    vec4 far_clip  = vec4(ndc, 1.0, 1.0);

    // Unproject to world space
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

    // Stars and other emissive bodies — render self-lit with corona
    if (emissive > 0.0) {
        // Bright emissive surface
        vec3 col = base_color * emissive;

        // Limb darkening for stars (brighter at center, darker at edge)
        float ndv = max(dot(normal, -rd), 0.0);
        col *= 0.6 + 0.4 * ndv;

        // Corona glow: check how close the ray passes to the sphere center
        // (already hit, so show inner glow gradient)
        col += base_color * 0.3 * pow(ndv, 0.5);

        outColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 final_color = vec3(0.0);

    // ── Uniform lighting mode ──────────────────────────────────────────────
    if (use_uniform_lighting) {
        // Hemispheric lighting: blend between dark side (down) and lit side (up)
        float hemi = 0.5 + 0.5 * normal.y;
        vec3 sky_color = vec3(0.6, 0.65, 0.75);
        vec3 ground_color = vec3(0.15, 0.12, 0.1);
        vec3 light = mix(ground_color, sky_color, hemi);
        final_color = base_color * light;

        // Add a subtle directional fill from upper-right
        vec3 fill_dir = normalize(vec3(0.5, 0.8, 0.3));
        float fill_ndl = max(dot(normal, fill_dir), 0.0);
        final_color += base_color * fill_ndl * 0.3;
    }

    // ── Star lighting mode ─────────────────────────────────────────────────
    if (use_star_lighting) {
        // Ambient
        final_color += base_color * ambient;

        // Accumulate light from all emissive bodies (stars)
        for (int i = 0; i < body_count && i < 512; i++) {
            if (spheres[i].color_emit.a <= 0.0) continue;  // not a light source
            if (i == closest_idx) continue;

            vec3 light_pos = spheres[i].pos_radius.xyz;
            float light_radius = spheres[i].pos_radius.w;
            vec3 light_color = spheres[i].color_emit.rgb * spheres[i].color_emit.a;

            vec3 to_light = light_pos - hit_pos;
            float light_dist = length(to_light);
            vec3 L = to_light / light_dist;

            // Shadow ray: offset origin slightly to avoid self-intersection
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
                // Attenuation: gentle falloff based on star radius
                float atten = 1.0 / (1.0 + (light_dist * light_dist) /
                    (light_radius * light_radius * 400.0));

                // Diffuse (Lambertian)
                float ndl = max(dot(normal, L), 0.0);
                final_color += base_color * light_color * ndl * atten;

                // Specular (Blinn-Phong)
                vec3 V = normalize(eye_pos.xyz - hit_pos);
                vec3 H = normalize(L + V);
                float spec = pow(max(dot(normal, H), 0.0), 32.0);
                final_color += light_color * spec * atten * 0.3;
            }
        }
    }

    // If neither mode is on, just show base color dimly
    if (!use_star_lighting && !use_uniform_lighting) {
        final_color = base_color * 0.1;
    }

    // Black hole special: dark center with faint accretion ring
    // (type encoded in emissive: stars have emissive > 0, black holes have emissive = -1)
    if (emissive < -0.5) {
        // Near-black surface with purple edge glow
        float edge = 1.0 - abs(dot(normal, -rd));
        final_color = vec3(0.01, 0.0, 0.02) + vec3(0.4, 0.1, 0.6) * pow(edge, 4.0) * 0.5;
    }

    // Gamma correction
    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
