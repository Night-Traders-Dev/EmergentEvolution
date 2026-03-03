#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── Camera / scene uniforms ────────────────────────────────────────────────

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;            // inverse view-projection matrix
    vec4 eye_pos;           // xyz = camera position
    vec4 screen_info;       // x = width, y = height, z = entity_count, w = time
    vec4 lighting_params;   // x = unused, y = unused, z = ambient, w = unused
};

// ── Sphere data ────────────────────────────────────────────────────────────

struct Sphere {
    vec4 pos_radius;    // xyz = world position, w = radius
    vec4 color_emit;    // rgb = base color (0-1), a = entity type (encoded)
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

// ── Procedural noise ──────────────────────────────────────────────────────

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash3d(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

// ── Background: dark biological fluid ─────────────────────────────────────

vec3 background(vec3 rd, float time) {
    // Deep dark aqueous/cellular background
    vec3 col = vec3(0.010, 0.025, 0.035);

    // Subtle caustic patterns (like light through water)
    vec2 uv = rd.xz / (abs(rd.y) + 0.5);
    float c1 = sin(uv.x * 8.0 + time * 0.3) * sin(uv.y * 6.0 + time * 0.2);
    float c2 = sin(uv.x * 5.0 - time * 0.4 + 2.0) * sin(uv.y * 9.0 - time * 0.15);
    float caustic = max(0.0, c1 + c2) * 0.012;
    col += vec3(caustic * 0.3, caustic * 0.8, caustic * 0.6);

    // Floating debris particles
    for (int layer = 0; layer < 2; layer++) {
        float scale = 300.0 + float(layer) * 200.0;
        vec2 cell = floor(rd.xz * scale / (abs(rd.y) + 0.3));
        float h = hash(cell + float(layer) * 71.0);
        if (h > 0.96) {
            float brightness = (h - 0.96) / 0.04;
            brightness *= brightness * 0.15;
            vec3 debris_col = mix(vec3(0.2, 0.5, 0.3), vec3(0.3, 0.6, 0.7), hash(cell + 37.0));
            col += debris_col * brightness;
        }
    }

    return col;
}

// ── Subsurface scattering approximation ───────────────────────────────────

vec3 subsurface(vec3 normal, vec3 light_dir, vec3 view_dir, vec3 color, float radius) {
    // Wrap lighting for translucent organic material
    float wrap = max(0.0, dot(normal, -light_dir) * 0.5 + 0.5);
    // Through-scatter: light passing through thin parts
    float back_scatter = max(0.0, dot(view_dir, light_dir));
    back_scatter = pow(back_scatter, 3.0) * 0.3;
    // Scale effect by radius (smaller entities are more translucent)
    float translucency = clamp(8.0 / radius, 0.1, 1.0);
    return color * (wrap * 0.2 + back_scatter) * translucency;
}

// ── Main raytracing ────────────────────────────────────────────────────────

void main() {
    float W = screen_info.x;
    float H = screen_info.y;
    int entity_count = int(screen_info.z);
    float time = screen_info.w;
    float ambient = lighting_params.z;

    // ── Reconstruct ray from pixel ─────────────────────────────────────────
    vec2 ndc = fragUV * 2.0 - 1.0;
    ndc.y = -ndc.y;  // flip Y (Vulkan convention)

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

    for (int i = 0; i < entity_count && i < 1024; i++) {
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
        outColor = vec4(background(rd, time), 1.0);
        return;
    }

    // ── Hit — compute shading ──────────────────────────────────────────────
    Sphere hit = spheres[closest_idx];
    vec3 hit_pos = ro + rd * closest_t;
    vec3 normal = normalize(hit_pos - hit.pos_radius.xyz);
    vec3 base_color = hit.color_emit.rgb;
    float entity_type = hit.color_emit.a;
    float radius = hit.pos_radius.w;

    vec3 V = normalize(eye_pos.xyz - hit_pos);

    // Key light — slightly warm, from upper right
    vec3 key_dir = normalize(vec3(0.6, 0.8, 0.4));
    vec3 key_color = vec3(0.85, 0.9, 1.0);

    // Fill light — cool blue-green from below-left
    vec3 fill_dir = normalize(vec3(-0.4, -0.3, 0.6));
    vec3 fill_color = vec3(0.15, 0.3, 0.35);

    // Rim light — from behind for edge glow
    vec3 rim_dir = normalize(vec3(-0.2, 0.1, -0.8));

    // ── Diffuse — wrap lighting for organic feel ───────────────────────────
    float key_ndl = max(dot(normal, key_dir), 0.0);
    float fill_ndl = max(dot(normal, fill_dir), 0.0);

    // Wrap diffuse (softer shadows, more organic)
    float key_wrap = max(0.0, (dot(normal, key_dir) + 0.3) / 1.3);
    float fill_wrap = max(0.0, (dot(normal, fill_dir) + 0.3) / 1.3);

    vec3 diffuse = base_color * key_color * key_wrap * 0.65
                 + base_color * fill_color * fill_wrap * 0.4;

    // ── Ambient ────────────────────────────────────────────────────────────
    // Hemispheric ambient (warmer above, cooler below)
    float hemi = 0.5 + 0.5 * normal.y;
    vec3 ambient_light = mix(vec3(0.04, 0.06, 0.08), vec3(0.08, 0.1, 0.12), hemi);
    vec3 amb = base_color * (ambient + 0.08) * ambient_light / 0.1;

    // ── Specular — soft, wet-looking ───────────────────────────────────────
    vec3 H_key = normalize(key_dir + V);
    float spec_key = pow(max(dot(normal, H_key), 0.0), 16.0);
    // Wet/membrane sheen — use Schlick fresnel
    float fresnel = pow(1.0 - max(dot(normal, V), 0.0), 3.0);
    float spec = spec_key * 0.25 + fresnel * 0.15;
    vec3 spec_color = mix(vec3(0.5, 0.7, 0.6), vec3(0.8, 0.85, 0.9), fresnel);

    // ── Subsurface scattering ──────────────────────────────────────────────
    vec3 sss = subsurface(normal, key_dir, V, base_color, radius);

    // ── Rim/edge glow ──────────────────────────────────────────────────────
    float rim = pow(1.0 - max(dot(normal, V), 0.0), 2.5);
    vec3 rim_color = base_color * 0.5 + vec3(0.1, 0.2, 0.15);

    // ── Membrane detail (procedural surface noise) ─────────────────────────
    // Scale based on surface position for organic texture
    vec3 surface_pt = (hit_pos - hit.pos_radius.xyz) / radius;
    float noise1 = hash3d(surface_pt * 8.0 + vec3(time * 0.1));
    float noise2 = hash3d(surface_pt * 16.0 - vec3(time * 0.05));
    float membrane = 0.85 + 0.15 * (noise1 * 0.6 + noise2 * 0.4);

    // ── Virus spike hint ───────────────────────────────────────────────────
    // Type 2 = virus — add spiky surface bump
    float spike = 0.0;
    if (entity_type > 1.5 && entity_type < 2.5) {
        // Angular spike pattern
        float theta = atan(surface_pt.z, surface_pt.x);
        float phi = asin(clamp(surface_pt.y, -1.0, 1.0));
        float s = sin(theta * 6.0 + time) * sin(phi * 5.0 + time * 0.7);
        spike = max(0.0, s) * 0.2;
    }

    // ── Combine ────────────────────────────────────────────────────────────
    vec3 final_color = (diffuse + amb + sss) * membrane
                     + spec_color * spec
                     + rim_color * rim * 0.35
                     + base_color * spike;

    // ── Nutrient glow (type 3 = nutrient) ──────────────────────────────────
    if (entity_type > 2.5 && entity_type < 3.5) {
        float glow = 0.3 + 0.2 * sin(time * 3.0 + hash3d(hit.pos_radius.xyz) * 6.28);
        final_color += base_color * glow * 0.3;
    }

    // ── Shadow from nearby entities ────────────────────────────────────────
    // Simple soft shadow from key light
    vec3 shadow_origin = hit_pos + normal * 0.2;
    float shadow = 1.0;
    for (int i = 0; i < entity_count && i < 1024; i++) {
        if (i == closest_idx) continue;
        float st = intersect_sphere(shadow_origin, key_dir,
            spheres[i].pos_radius.xyz,
            spheres[i].pos_radius.w);
        if (st > 0.0 && st < 500.0) {
            shadow *= 0.5;  // partial shadow for translucent feel
            break;
        }
    }
    final_color *= (0.4 + 0.6 * shadow);

    // Gamma correction
    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
