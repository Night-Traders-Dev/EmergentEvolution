#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── Camera / scene uniforms ────────────────────────────────────────────────

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;            // inverse view-projection matrix
    vec4 eye_pos;           // xyz = camera position
    vec4 screen_info;       // x = width, y = height, z = entity_count, w = time
    vec4 lighting_params;   // x = unused, y = unused, z = ambient, w = unused
    vec4 environment_color; // rgb = environment tint, a = haze density
    vec4 environment_factors; // x = oxygen, y = nutrients, z = pH, w = toxicity
};

// ── Sphere data ────────────────────────────────────────────────────────────

struct Sphere {
    vec4 pos_radius;    // xyz = world position, w = bounding radius
    vec4 axis_morph;    // xyz = orientation axis, w = morphology
    vec4 color_type;    // rgb = base color (0-1), a = entity type
    vec4 shape_params;  // x = aspect, y = noise, z = phase, w = mitosis progress
    vec4 life_params;   // x = organelle health, y = nutrient reserve, z = corpse flag, w = telomere state
    vec4 signal_params; // x = infection progress, y = infection load, z = infection morph, w = antibiotic film
    vec4 gene_params;   // x = antibiotic type, y = antibiotic diversity, z = antibiotic yield, w = unused
    vec4 aux_params;    // xyz = infection axis, w = unused
};

layout(std430, set = 0, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

struct EnvFeature {
    vec4 pos_radius;
    vec4 axis_strength;
    vec4 tint_type;
    vec4 meta;
};

layout(std430, set = 0, binding = 2) readonly buffer FeatureBuffer {
    EnvFeature features[];
};

float hash3d(vec3 p);

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

float sd_sphere(vec3 p, float r) {
    return length(p) - r;
}

float sd_capsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-5), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sd_ellipsoid(vec3 p, vec3 r) {
    vec3 pr = p / r;
    vec3 pr2 = p / (r * r);
    float k0 = length(pr);
    float k1 = length(pr2);
    return k0 * (k0 - 1.0) / max(k1, 1e-5);
}

float sd_octahedron(vec3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    vec3 q;
    if (3.0 * p.x < m) q = p.xyz;
    else if (3.0 * p.y < m) q = p.yzx;
    else if (3.0 * p.z < m) q = p.zxy;
    else return m * 0.57735027;
    float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(vec3(q.x, q.y - s + k, q.z - k));
}

float sd_torus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

mat3 basis_from_axis(vec3 axis) {
    vec3 up = normalize(length(axis) > 1e-5 ? axis : vec3(0.0, 1.0, 0.0));
    vec3 tangent = normalize(abs(up.y) < 0.95 ? cross(vec3(0.0, 1.0, 0.0), up)
                                              : cross(vec3(1.0, 0.0, 0.0), up));
    vec3 bitangent = cross(up, tangent);
    return mat3(tangent, bitangent, up);
}

float stage_band(float progress, float start, float end) {
    return smoothstep(start, start + 0.06, progress) *
           (1.0 - smoothstep(end - 0.06, end, progress));
}

float lifecycle_age(float organelle_health, float telomere_state, float corpse) {
    if (corpse > 0.5)
        return 1.0;
    return clamp(1.0 - (clamp(organelle_health, 0.0, 1.0) * 0.56 +
                        clamp(telomere_state, 0.0, 1.0) * 0.44), 0.0, 1.0);
}

vec3 spectrum_color(float signature, float diversity) {
    float a = signature * 6.2831853;
    vec3 base = 0.52 + 0.38 * cos(a + vec3(0.0, 2.1, 4.2));
    vec3 accent = 0.45 + 0.35 * cos(a * 1.7 + vec3(1.2, 3.6, 5.5));
    return mix(base, accent, clamp(diversity, 0.0, 1.0) * 0.35);
}

float sd_cell_shape(vec3 p, int morph, float aspect, float noise, float phase, float mitosis) {
    float jitter = sin(p.x * 7.0 + phase) * sin(p.y * 6.0 - phase * 1.3) * sin(p.z * 8.0 + phase * 0.7);
    float base_shape = 0.0;
    vec3 radii = vec3(0.76, 0.76 * clamp(aspect, 0.9, 1.1), 0.76);

    if (morph == 1) {
        radii = vec3(0.90, 0.48 + aspect * 0.08, 0.90);
        base_shape = sd_ellipsoid(p, radii);
    } else if (morph == 2) {
        float base = sd_sphere(p, 0.64);
        vec3 bulgeA = vec3(sin(phase), 0.25 * cos(phase * 1.3), cos(phase)) * 0.24;
        vec3 bulgeB = vec3(cos(phase * 1.7), sin(phase * 0.9), -sin(phase)) * 0.18;
        float bulges = min(sd_sphere(p - bulgeA, 0.34), sd_sphere(p + bulgeB, 0.29));
        base_shape = min(base, bulges);
    } else {
        base_shape = sd_ellipsoid(p, radii);
    }

    if (mitosis > 0.01) {
        float round_up = smoothstep(0.0, 0.18, mitosis);
        float split = smoothstep(0.58, 0.98, mitosis);
        vec3 rounded_radii = mix(radii, vec3(0.82, 0.82, 0.82), round_up);
        float rounded = sd_ellipsoid(p, rounded_radii);
        float daughter_offset = 0.08 + split * 0.34;
        vec3 daughter_radii = mix(rounded_radii, vec3(0.58, 0.68, 0.66), split);
        float two_body = min(
            sd_ellipsoid(p - vec3(daughter_offset, 0.0, 0.0), daughter_radii),
            sd_ellipsoid(p + vec3(daughter_offset, 0.0, 0.0), daughter_radii));
        base_shape = mix(mix(base_shape, rounded, round_up), two_body, split);
    }

    return base_shape - jitter * (0.03 + noise * 0.06);
}

float sd_bacteria_base_shape(vec3 p, int morph, float aspect, float noise, float phase) {
    if (morph == 0) {
        float offset = 0.20 + noise * 0.08;
        return min(sd_sphere(p - vec3(offset, 0.0, 0.0), 0.42),
                   sd_sphere(p + vec3(offset, 0.0, 0.0), 0.42));
    }

    if (morph == 2) {
        float turns = 5.5 + aspect * 0.7;
        float amp = 0.11 + noise * 0.07;
        float z = clamp(p.z, -0.62, 0.62);
        vec2 offset = vec2(cos(z * turns + phase), sin(z * turns + phase)) * amp;
        vec2 q = p.xy - offset;
        return length(vec3(q, max(abs(p.z) - 0.62, 0.0))) - 0.18;
    }

    float half_len = clamp(0.18 + aspect * 0.16, 0.36, 0.56);
    return sd_capsule(p, vec3(0.0, 0.0, -half_len), vec3(0.0, 0.0, half_len), 0.23);
}

float sd_bacteria_shape(vec3 p, int morph, float aspect, float noise, float phase, float division) {
    float prep = smoothstep(0.04, 0.36, division);
    float split = smoothstep(0.56, 0.98, division);
    vec3 split_axis = (morph == 0) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);

    vec3 q = p;
    if (morph == 0) {
        float stretch = 1.0 + prep * 0.34;
        q.x /= stretch;
        q.yz *= 1.0 + prep * 0.06;
    } else {
        float stretch = 1.0 + prep * 0.40;
        q.z /= stretch;
        q.xy *= 1.0 + prep * 0.08;
    }
    float elongated = sd_bacteria_base_shape(q, morph, aspect, noise, phase);
    float base = mix(sd_bacteria_base_shape(p, morph, aspect, noise, phase), elongated, prep);

    float daughter_offset = (morph == 0 ? 0.08 : 0.10) + split * (morph == 0 ? 0.26 : 0.34);
    float daughter_scale = mix(1.0, 0.94, split);
    float daughter_a = sd_bacteria_base_shape((p - split_axis * daughter_offset) / daughter_scale,
                                              morph, aspect, noise, phase + 0.10) * daughter_scale;
    float daughter_b = sd_bacteria_base_shape((p + split_axis * daughter_offset) / daughter_scale,
                                              morph, aspect, noise, phase - 0.10) * daughter_scale;
    float daughters = min(daughter_a, daughter_b);

    vec3 neck_radii = (morph == 0) ? vec3(0.12, 0.17, 0.17) : vec3(0.18, 0.18, 0.12);
    float neck = sd_ellipsoid(p, neck_radii);
    float constricted = max(base, -neck);
    return mix(constricted, daughters, split);
}

float sd_antibiotic_cloud(vec3 p, int morph, float aspect, float noise, float phase,
                          float division, float spread, float diversity) {
    float scale = 1.16 + spread * (0.80 + diversity * 0.45);
    vec3 q = p / scale;
    float cloud = sd_bacteria_shape(q, morph, aspect, noise + 0.08, phase + spread * 2.8, division) * scale;
    float turbulence = sin(dot(p, vec3(13.0, 9.0, 11.0)) + phase * 2.2) *
                       sin(dot(p, vec3(-7.0, 12.0, 8.0)) - phase * 1.6);
    cloud -= turbulence * (0.03 + spread * 0.07);
    cloud -= 0.05 + spread * 0.24;
    return cloud;
}

float sd_corona(vec3 p, float noise, float phase) {
    float d = sd_sphere(p, 0.55);
    for (int i = 0; i < 10; ++i) {
        float fi = float(i);
        float z = 1.0 - 2.0 * (fi + 0.5) / 10.0;
        float r = sqrt(max(1.0 - z * z, 0.0));
        float a = fi * 2.39996323 + phase;
        vec3 dir = normalize(vec3(cos(a) * r, sin(a) * r, z));
        float spike = sd_capsule(p, dir * 0.44, dir * (0.78 + noise * 0.10), 0.05 + noise * 0.03);
        d = min(d, spike);
    }
    return d;
}

float sd_phage(vec3 p, float noise, float phase) {
    vec3 head_p = p - vec3(0.0, 0.0, 0.30);
    float head = mix(sd_sphere(head_p, 0.34), sd_octahedron(head_p, 0.56) * 0.72, 0.7);
    float tail = sd_capsule(p, vec3(0.0, 0.0, 0.12), vec3(0.0, 0.0, -0.62), 0.055);
    float base = sd_capsule(p, vec3(-0.14, 0.0, -0.58), vec3(0.14, 0.0, -0.58), 0.028);

    float legs = 1e5;
    for (int i = 0; i < 6; ++i) {
        float a = phase + float(i) / 6.0 * 6.2831853;
        vec3 root = vec3(cos(a) * 0.08, sin(a) * 0.08, -0.56);
        vec3 tip = vec3(cos(a) * 0.34, sin(a) * 0.34, -0.86 - noise * 0.08);
        legs = min(legs, sd_capsule(p, root, tip, 0.018));
    }

    return min(min(head, tail), min(base, legs));
}

float sd_virus_shape(vec3 p, int morph, float aspect, float noise, float phase) {
    if (morph == 1)
        return sd_corona(p, noise, phase);
    if (morph == 2)
        return sd_phage(vec3(p.xy, p.z * clamp(aspect / 2.8, 0.8, 1.15)), noise, phase);

    float capsid = mix(sd_sphere(p, 0.68), sd_octahedron(p, 1.08) * 0.74, 0.78);
    float ridge = sin((p.x + p.y + p.z) * 18.0 + phase * 2.0) * (0.02 + noise * 0.04);
    return capsid - ridge;
}

float entity_sdf_local(vec3 p, Sphere s) {
    int entity_type = int(s.color_type.a + 0.5);
    int morph = int(s.axis_morph.a + 0.5);
    float aspect = s.shape_params.x;
    float noise = s.shape_params.y;
    float phase = s.shape_params.z;
    float division = s.shape_params.w;
    float corpse = s.life_params.z;
    float antibiotic_film = s.signal_params.w;
    float antibiotic_diversity = s.gene_params.y;

    if (corpse > 0.5) {
        p *= 1.15;
        noise += 0.08;
        division = 0.0;
    }

    if (entity_type == 0)
        return sd_cell_shape(p, morph, aspect, noise, phase, division);
    if (entity_type == 1) {
        float core = sd_bacteria_shape(p, morph, aspect, noise, phase, division);
        if (antibiotic_film > 0.02 && corpse < 0.5) {
            float cloud = sd_antibiotic_cloud(p, morph, aspect, noise, phase, division,
                                              antibiotic_film, antibiotic_diversity);
            return min(core, cloud);
        }
        return core;
    }
    if (entity_type == 2)
        return sd_virus_shape(p, morph, aspect, noise, phase);
    if (entity_type == 6)
        return max(sd_ellipsoid(p, vec3(0.76, 0.36, 0.76)), -(sd_ellipsoid(p, vec3(0.30, 0.12, 0.30))));
    if (entity_type == 7)
        return sd_cell_shape(p, 2, 1.0, 0.28, phase, division);
    if (entity_type == 8)
        return sd_cell_shape(p, 2, 1.0, 0.22, phase, 0.0);

    return sd_sphere(p, 0.72);
}

vec3 entity_normal_local(vec3 p, Sphere s) {
    vec2 e = vec2(0.0035, 0.0);
    return normalize(vec3(
        entity_sdf_local(p + vec3(e.x, e.y, e.y), s) - entity_sdf_local(p - vec3(e.x, e.y, e.y), s),
        entity_sdf_local(p + vec3(e.y, e.x, e.y), s) - entity_sdf_local(p - vec3(e.y, e.x, e.y), s),
        entity_sdf_local(p + vec3(e.y, e.y, e.x), s) - entity_sdf_local(p - vec3(e.y, e.y, e.x), s)
    ));
}

bool refine_entity_hit(vec3 ro, vec3 rd, Sphere s, float bound_t,
                       out float refined_t, out vec3 refined_pos, out vec3 refined_normal,
                       out vec3 local_surface) {
    mat3 basis = basis_from_axis(s.axis_morph.xyz);
    float radius = max(s.pos_radius.w, 0.001);
    float start_t = max(bound_t - radius * 1.35, 0.0);
    float end_t = bound_t + radius * 1.65;
    float t = start_t;

    for (int step = 0; step < 40; ++step) {
        vec3 pos = ro + rd * t;
        vec3 local = transpose(basis) * ((pos - s.pos_radius.xyz) / radius);
        float dist = entity_sdf_local(local, s) * radius;
        if (dist < radius * 0.004) {
            vec3 normal_local = entity_normal_local(local, s);
            refined_t = t;
            refined_pos = pos;
            refined_normal = normalize(basis * normal_local);
            local_surface = local;
            return true;
        }

        t += clamp(dist, radius * 0.01, radius * 0.18);
        if (t > end_t)
            break;
    }

    refined_t = bound_t;
    refined_pos = ro + rd * bound_t;
    refined_normal = normalize(refined_pos - s.pos_radius.xyz);
    local_surface = transpose(basis) * ((refined_pos - s.pos_radius.xyz) / radius);
    return false;
}

float mask_from_sdf(float sdf, float blur) {
    return 1.0 - smoothstep(0.0, blur, sdf);
}

float sphere_mask(vec3 p, vec3 center, float radius, float blur) {
    return mask_from_sdf(sd_sphere(p - center, radius), blur);
}

float ellipsoid_mask(vec3 p, vec3 center, vec3 radii, float blur) {
    return mask_from_sdf(sd_ellipsoid(p - center, radii), blur);
}

float capsule_mask(vec3 p, vec3 a, vec3 b, float radius, float blur) {
    return mask_from_sdf(sd_capsule(p, a, b, radius), blur);
}

float torus_mask(vec3 p, vec3 center, vec2 radii, float blur) {
    return mask_from_sdf(sd_torus(p - center, radii), blur);
}

float virus_mask(vec3 p, vec3 center, float scale, int morph, float noise, float phase, float blur) {
    vec3 q = (p - center) / max(scale, 1e-4);
    float sdf = sd_virus_shape(q, morph, 1.0, noise, phase) * scale;
    return mask_from_sdf(sdf, blur);
}

vec4 sample_nonfunctional_organelles(vec3 p0, vec3 p1, vec3 p2,
                                     int morph, float phase, float noise,
                                     float life_age, float corpse) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    vec3 stage_a = mix(vec3(0.34, 0.66, 0.88), vec3(0.58, 0.74, 0.92), smoothstep(0.15, 0.45, life_age));
    vec3 stage_b = mix(vec3(0.92, 0.54, 0.64), vec3(0.78, 0.58, 0.34), smoothstep(0.35, 0.82, life_age));
    vec3 stage_c = mix(vec3(0.52, 0.90, 0.70), vec3(0.44, 0.46, 0.40), smoothstep(0.45, 0.95, life_age));
    if (corpse > 0.5) {
        stage_a = vec3(0.28, 0.30, 0.32);
        stage_b = vec3(0.30, 0.26, 0.22);
        stage_c = vec3(0.24, 0.25, 0.22);
    }

    for (int i = 0; i < 2; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            sin(phase * 0.7 + fi * 2.9) * 0.26,
            cos(phase * 1.1 + fi * 2.2) * (morph == 1 ? 0.09 : 0.16),
            sin(phase * 0.9 + fi * 3.4) * 0.20);
        float r = 0.11 + fi * 0.025 + noise * 0.03;
        float vacuole = max(sphere_mask(p0, center, r, 0.06),
                            sphere_mask(p1, center, r, 0.06));
        color += stage_a * vacuole * mix(0.12, 0.07, life_age);
        density += vacuole * mix(0.038, 0.020, life_age);
    }

    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase * 1.7 + fi * 1.3) * 0.24,
            sin(phase * 0.8 + fi * 2.7) * 0.18,
            cos(phase * 1.2 + fi * 1.9) * 0.22);
        float lysosome = max(sphere_mask(p1, center, 0.045 + fi * 0.004, 0.035),
                             sphere_mask(p2, center, 0.045 + fi * 0.004, 0.035));
        color += stage_b * lysosome * mix(0.12, 0.08, life_age);
        density += lysosome * mix(0.026, 0.016, life_age);
    }

    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            sin(phase * 1.4 + fi * 2.4) * 0.20,
            cos(phase * 1.0 + fi * 1.6) * 0.15,
            sin(phase * 0.6 + fi * 2.8) * 0.24);
        vec3 axis = normalize(vec3(
            cos(fi + phase * 0.7),
            sin(fi * 1.7 - phase),
            cos(fi * 0.8 + phase * 1.4)));
        float body = max(capsule_mask(p0, center - axis * 0.08, center + axis * 0.08, 0.03, 0.03),
                         capsule_mask(p1, center - axis * 0.08, center + axis * 0.08, 0.03, 0.03));
        color += mix(vec3(0.82, 0.78, 0.36), vec3(0.50, 0.42, 0.22), life_age) * body * 0.08;
        density += body * mix(0.020, 0.010, life_age);
    }

    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase * 0.9 + fi * 1.5) * 0.17,
            sin(phase * 1.8 + fi * 1.2) * 0.13,
            cos(phase * 1.5 + fi * 2.5) * 0.16);
        float peroxisome = max(sphere_mask(p0, center, 0.034, 0.028),
                               sphere_mask(p2, center, 0.034, 0.028));
        color += stage_c * peroxisome * 0.10;
        density += peroxisome * mix(0.022, 0.012, life_age);
    }

    return vec4(color, clamp(density, 0.0, 0.22));
}

vec4 sample_viral_replication_overlay(vec3 p0, vec3 p1, vec3 p2, float progress,
                                      float load, int virus_morph, float phase,
                                      vec3 entry_axis) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    entry_axis = length(entry_axis) > 1e-4 ? normalize(entry_axis) : vec3(1.0, 0.0, 0.0);
    float entry = 1.0 - smoothstep(0.14, 0.28, progress);
    vec3 virus_col = mix(vec3(0.90, 0.20, 0.24), vec3(1.0, 0.56, 0.24), float(virus_morph == 1));
    if (virus_morph == 2)
        virus_col = vec3(0.56, 0.94, 0.82);

    vec3 entry_tangent = normalize(abs(entry_axis.y) < 0.92
        ? cross(entry_axis, vec3(0.0, 1.0, 0.0))
        : cross(entry_axis, vec3(1.0, 0.0, 0.0)));
    vec3 entry_bitangent = normalize(cross(entry_axis, entry_tangent));
    float ingress_mix = smoothstep(0.0, 0.22, progress);
    vec3 ingress_center = mix(entry_axis * 0.78,
                              entry_axis * 0.04 + entry_tangent * sin(phase * 0.9) * 0.06 +
                              entry_bitangent * cos(phase * 1.2) * 0.04,
                              ingress_mix);
    float ingress = max(virus_mask(p0, ingress_center, 0.088, virus_morph, 0.06, phase, 0.026),
                        virus_mask(p1, ingress_center, 0.088, virus_morph, 0.06, phase, 0.026));
    float pore = torus_mask(p0, entry_axis * 0.52, vec2(0.13, 0.016), 0.028) *
                 (1.0 - smoothstep(0.14, 0.36, progress));
    color += virus_col * ingress * entry * 0.62;
    color += mix(vec3(0.92, 0.64, 0.26), virus_col, 0.35) * pore * entry * 0.28;
    density += ingress * entry * 0.18 + pore * entry * 0.05;

    float swarm = smoothstep(0.16, 0.92, progress);
    float crowding = smoothstep(3.0, 10.0, load) * smoothstep(0.48, 0.96, progress);
    float max_particles = clamp(load, 1.0, 12.0);
    for (int i = 0; i < 12; ++i) {
        float fi = float(i);
        float is_active = smoothstep(fi - 0.5, fi + 0.4, max_particles);
        if (is_active <= 0.0)
            continue;
        vec3 center = vec3(
            sin(phase * 1.2 + fi * 1.7) * mix(0.18, 0.34, crowding),
            cos(phase * 0.8 + fi * 2.1) * mix(0.16, 0.26, crowding),
            sin(phase * 1.5 + fi * 2.7) * mix(0.18, 0.34, crowding));
        center += entry_axis * mix(-0.06, 0.10, crowding);
        float scale = 0.050 + 0.010 * sin(fi + phase * 1.3) + crowding * 0.010;
        float virion = max(virus_mask(p1, center, scale, virus_morph, 0.08, phase + fi * 0.4, 0.025),
                           virus_mask(p2, center, scale, virus_morph, 0.08, phase + fi * 0.4, 0.025));
        color += virus_col * virion * swarm * is_active * (0.16 + crowding * 0.08);
        density += virion * swarm * is_active * (0.048 + crowding * 0.030);
    }

    float rupture = smoothstep(0.76, 1.02, progress) + crowding * 0.45;
    float stress = smoothstep(0.78, 0.98, hash3d(p1 * 18.0 + vec3(phase * 1.6)));
    float crack = smoothstep(0.78, 0.96, abs(sin(dot(p1, entry_tangent * 11.0 + entry_bitangent * 9.0) + phase * 2.7)));
    color += vec3(0.92, 0.22, 0.18) * rupture * stress * 0.20;
    color += vec3(1.0, 0.74, 0.18) * rupture * crack * 0.10;
    density += rupture * stress * 0.08 + rupture * crack * 0.03;

    return vec4(color, clamp(density, 0.0, 0.78));
}

vec4 sample_binary_fission_sequence(vec3 p0, vec3 p1, vec3 p2, float progress, int morph, float phase) {
    float replication = stage_band(progress, 0.02, 0.30);
    float elongation = stage_band(progress, 0.24, 0.58);
    float septation = stage_band(progress, 0.54, 0.84);
    float separation = smoothstep(0.82, 0.98, progress);
    vec3 split_axis = (morph == 0) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);

    vec3 color = vec3(0.0);
    float density = 0.0;

    vec3 nuc_a = split_axis * (0.12 + elongation * 0.08);
    vec3 nuc_b = -nuc_a;
    float nuc = max(
        max(capsule_mask(p1, nuc_a - split_axis * 0.15, nuc_a + split_axis * 0.15, 0.07, 0.04),
            capsule_mask(p2, nuc_a - split_axis * 0.13, nuc_a + split_axis * 0.13, 0.06, 0.04)),
        max(capsule_mask(p1, nuc_b - split_axis * 0.15, nuc_b + split_axis * 0.15, 0.07, 0.04),
            capsule_mask(p2, nuc_b - split_axis * 0.13, nuc_b + split_axis * 0.13, 0.06, 0.04)));
    color += vec3(0.96, 0.74, 0.28) * nuc * (replication * 0.14 + elongation * 0.10);
    density += nuc * (replication * 0.06 + elongation * 0.05);

    vec3 septum_radii = (morph == 0) ? vec3(0.08, 0.19, 0.19) : vec3(0.19, 0.19, 0.08);
    float septum = max(ellipsoid_mask(p1, vec3(0.0), septum_radii, 0.03),
                       ellipsoid_mask(p2, vec3(0.0), septum_radii * 0.92, 0.03));
    float ring = torus_mask(p1, vec3(0.0), vec2(morph == 0 ? 0.20 : 0.24, 0.018), 0.028);
    color += vec3(0.84, 0.92, 0.58) * septum * septation * 0.10;
    color += vec3(0.60, 0.96, 0.88) * ring * septation * 0.08;
    density += septum * septation * 0.04 + ring * septation * 0.03;

    float split_glow = smoothstep(0.0, 1.0, separation) *
                       smoothstep(0.78, 0.96, abs(dot(p1, split_axis)));
    color += vec3(0.56, 0.86, 0.98) * split_glow * 0.08;
    density += split_glow * 0.02;

    return vec4(color, clamp(density, 0.0, 0.32));
}

vec4 sample_mitosis_sequence(vec3 p0, vec3 p1, vec3 p2, float progress, float phase) {
    float prophase = stage_band(progress, 0.02, 0.28);
    float metaphase = stage_band(progress, 0.24, 0.55);
    float anaphase = stage_band(progress, 0.52, 0.82);
    float telophase = smoothstep(0.78, 0.96, progress);

    vec3 color = vec3(0.0);
    float density = 0.0;
    vec3 pole_a = vec3(-0.44, 0.0, 0.0);
    vec3 pole_b = vec3(0.44, 0.0, 0.0);

    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec3 axis = normalize(vec3(cos(fi * 1.7 + phase), sin(fi * 1.4 - phase), cos(fi * 0.9 + phase)));

        vec3 pro_center = vec3(
            sin(phase * 1.1 + fi * 1.5) * 0.14,
            cos(phase * 0.7 + fi * 1.8) * 0.14,
            sin(phase * 0.9 + fi * 2.1) * 0.12);
        float pro_chr = max(capsule_mask(p1, pro_center - axis * 0.10, pro_center + axis * 0.10, 0.028, 0.03),
                            capsule_mask(p2, pro_center - axis * 0.10, pro_center + axis * 0.10, 0.028, 0.03));
        color += vec3(0.90, 0.42, 0.76) * pro_chr * prophase * 0.18;
        density += pro_chr * prophase * 0.06;

        vec3 meta_center = vec3(0.0, -0.20 + fi * 0.10, 0.04 * sin(phase + fi));
        float meta_chr = max(capsule_mask(p1, meta_center - vec3(0.0, 0.055, 0.0), meta_center + vec3(0.0, 0.055, 0.0), 0.024, 0.03),
                             capsule_mask(p2, meta_center - vec3(0.0, 0.055, 0.0), meta_center + vec3(0.0, 0.055, 0.0), 0.024, 0.03));
        float left_fiber = capsule_mask(p1, pole_a, meta_center, 0.012, 0.02);
        float right_fiber = capsule_mask(p1, pole_b, meta_center, 0.012, 0.02);
        color += vec3(0.92, 0.56, 0.82) * meta_chr * metaphase * 0.18;
        color += vec3(0.54, 0.82, 0.96) * (left_fiber + right_fiber) * metaphase * 0.07;
        density += (meta_chr * 0.07 + (left_fiber + right_fiber) * 0.02) * metaphase;

        float split_offset = 0.12 + fi * 0.04;
        vec3 ana_left = vec3(-split_offset, -0.16 + fi * 0.08, 0.03 * cos(phase + fi));
        vec3 ana_right = vec3(split_offset, -0.16 + fi * 0.08, -0.03 * cos(phase + fi));
        float ana_chr = max(
            max(capsule_mask(p1, ana_left - axis * 0.08, ana_left + axis * 0.08, 0.022, 0.028),
                capsule_mask(p2, ana_left - axis * 0.08, ana_left + axis * 0.08, 0.022, 0.028)),
            max(capsule_mask(p1, ana_right - axis * 0.08, ana_right + axis * 0.08, 0.022, 0.028),
                capsule_mask(p2, ana_right - axis * 0.08, ana_right + axis * 0.08, 0.022, 0.028)));
        float ana_fibers = capsule_mask(p1, pole_a, ana_left, 0.010, 0.02) +
                           capsule_mask(p1, pole_b, ana_right, 0.010, 0.02);
        color += vec3(0.94, 0.62, 0.86) * ana_chr * anaphase * 0.17;
        color += vec3(0.56, 0.86, 0.98) * ana_fibers * anaphase * 0.06;
        density += (ana_chr * 0.06 + ana_fibers * 0.02) * anaphase;
    }

    float tel_nucleus_a = max(ellipsoid_mask(p1, vec3(-0.24, 0.0, 0.0), vec3(0.18, 0.15, 0.16), 0.05),
                              ellipsoid_mask(p2, vec3(-0.24, 0.0, 0.0), vec3(0.18, 0.15, 0.16), 0.05));
    float tel_nucleus_b = max(ellipsoid_mask(p1, vec3(0.24, 0.0, 0.0), vec3(0.18, 0.15, 0.16), 0.05),
                              ellipsoid_mask(p2, vec3(0.24, 0.0, 0.0), vec3(0.18, 0.15, 0.16), 0.05));
    float cleavage_ring = torus_mask(p1, vec3(0.0), vec2(0.28, 0.020), 0.03) +
                          torus_mask(p2, vec3(0.0), vec2(0.26, 0.020), 0.03);
    color += vec3(0.52, 0.28, 0.70) * (tel_nucleus_a + tel_nucleus_b) * telophase * 0.22;
    color += vec3(0.62, 0.90, 0.96) * cleavage_ring * telophase * 0.07;
    density += ((tel_nucleus_a + tel_nucleus_b) * 0.09 + cleavage_ring * 0.02) * telophase;

    return vec4(color, clamp(density, 0.0, 0.42));
}

vec4 sample_cell_interior(vec3 surface_pt, vec3 local_view, int morph,
                          float aspect, float noise, float phase, float mitosis,
                          float infection_progress, float infection_load, int infection_morphology,
                          vec3 infection_axis,
                          float organelle_health, float nutrient_reserve,
                          float telomere_state, float corpse) {
    vec3 p0 = surface_pt + local_view * (0.14 + noise * 0.03);
    vec3 p1 = surface_pt + local_view * (0.30 + noise * 0.05);
    vec3 p2 = surface_pt + local_view * (0.48 + noise * 0.06);
    float life_age = lifecycle_age(organelle_health, telomere_state, corpse);
    float mitosis_mix = smoothstep(0.04, 0.22, mitosis) * (1.0 - corpse);

    vec3 cytoplasm = vec3(0.18, 0.28, 0.34);
    if (morph == 1)
        cytoplasm = vec3(0.26, 0.34, 0.24);
    else if (morph == 2)
        cytoplasm = vec3(0.16, 0.34, 0.28);

    vec3 young_cytoplasm = cytoplasm;
    vec3 mature_cytoplasm = mix(cytoplasm, cytoplasm + vec3(0.06, 0.05, 0.01), 0.5);
    vec3 senescent_cytoplasm = mix(cytoplasm, vec3(0.42, 0.34, 0.20), 0.75);
    vec3 dead_cytoplasm = vec3(0.20, 0.18, 0.16);
    vec3 aged_cytoplasm = mix(mix(young_cytoplasm, mature_cytoplasm, smoothstep(0.10, 0.35, life_age)),
                              senescent_cytoplasm, smoothstep(0.40, 0.82, life_age));
    vec3 cytoplasm_tint = mix(aged_cytoplasm, dead_cytoplasm, corpse);
    vec3 color = cytoplasm_tint * (0.20 + hash3d(p1 * 7.0 + vec3(phase)) * 0.10);
    float density = mix(0.16, 0.11, corpse);

    vec3 nucleus_center = vec3(0.12 * sin(phase * 1.1),
                               0.07 * cos(phase * 0.8),
                               0.08 * sin(phase * 1.6));
    if (morph == 1)
        nucleus_center += vec3(-0.05, -0.12, 0.02);
    else if (morph == 2)
        nucleus_center += vec3(0.10 * cos(phase * 0.7), 0.03, -0.05);

    vec3 nucleus_radii = vec3(0.26, 0.22 + (1.0 - aspect) * 0.03, 0.24);
    if (morph == 1)
        nucleus_radii = vec3(0.23, 0.18, 0.21);
    if (morph == 2)
        nucleus_radii = vec3(0.24, 0.20, 0.28);

    float nucleus = max(ellipsoid_mask(p1, nucleus_center, nucleus_radii, 0.08),
                        ellipsoid_mask(p2, nucleus_center, nucleus_radii * 0.96, 0.08));
    float nucleolus = sphere_mask(
        p2,
        nucleus_center + vec3(0.05 * cos(phase * 2.1), -0.03, 0.05 * sin(phase * 1.7)),
        0.08, 0.05) * nucleus;

    vec3 nucleus_col = mix(vec3(0.40, 0.22, 0.52), vec3(0.58, 0.40, 0.20), smoothstep(0.35, 0.85, life_age));
    vec3 nucleolus_col = mix(vec3(0.82, 0.56, 0.86), vec3(0.42, 0.36, 0.30), corpse);
    color += nucleus_col * nucleus * mix(0.44, 0.24, corpse);
    color += nucleolus_col * nucleolus * mix(0.40, 0.12, life_age);
    density += nucleus * mix(0.30, 0.18, corpse) + nucleolus * mix(0.12, 0.04, corpse);

    float mitochondria = 0.0;
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            sin(phase + fi * 1.7) * 0.30,
            cos(phase * 1.3 + fi * 1.8) * (morph == 1 ? 0.08 : 0.18),
            sin(phase * 0.9 + fi * 1.2) * 0.22);
        vec3 axis = normalize(vec3(
            cos(fi * 2.0 + phase),
            sin(fi * 1.4 - phase * 0.7),
            cos(fi * 0.9 + phase * 1.3)));
        float mito = max(capsule_mask(p0, center - axis * 0.12, center + axis * 0.12, 0.055, 0.05),
                         capsule_mask(p1, center - axis * 0.12, center + axis * 0.12, 0.055, 0.05));
        float cristae = 0.5 + 0.5 * sin(dot(p1 - center, axis) * 42.0 + fi * 1.6 + phase * 2.7);
        vec3 mito_col = mix(vec3(0.74, 0.40, 0.16), vec3(0.96, 0.60, 0.22), cristae);
        mito_col = mix(mito_col, vec3(0.48, 0.34, 0.22), smoothstep(0.35, 0.85, life_age));
        mito_col = mix(mito_col, vec3(0.26, 0.24, 0.22), corpse);
        color += mito_col * mito * mix(0.18, 0.08, corpse);
        mitochondria += mito;
    }
    density += mitochondria * mix(0.10, 0.05, corpse);

    float vesicles = 0.0;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase * 0.6 + fi * 2.2) * 0.22,
            sin(phase * 1.1 + fi * 1.5) * 0.14,
            cos(phase * 1.3 + fi * 1.1) * 0.18);
        float radius = (morph == 2 ? 0.10 : 0.07) + fi * 0.01;
        float vesicle = max(sphere_mask(p0, center, radius, 0.05),
                            sphere_mask(p1, center, radius, 0.05));
        vec3 vesicle_col = mix(vec3(0.58, 0.74, 0.82), vec3(0.66, 0.54, 0.32), smoothstep(0.35, 0.85, life_age));
        vesicle_col = mix(vesicle_col, vec3(0.24, 0.22, 0.20), corpse);
        color += vesicle_col * vesicle * 0.09;
        vesicles += vesicle;
    }
    density += vesicles * mix(0.05, 0.03, corpse);

    vec4 decorative = sample_nonfunctional_organelles(p0, p1, p2, morph, phase, noise, life_age, corpse);
    color += decorative.rgb;
    density += decorative.a;

    if (infection_progress > 0.0 && corpse < 0.5) {
        vec4 viral_overlay = sample_viral_replication_overlay(
            p0, p1, p2, infection_progress, infection_load, infection_morphology, phase, infection_axis);
        float infection_mix = smoothstep(0.02, 0.30, infection_progress);
        color = mix(color, color * (0.88 - infection_mix * 0.18) + viral_overlay.rgb, infection_mix * 0.70);
        density += viral_overlay.a;
    }

    if (morph == 1) {
        for (int i = 0; i < 3; ++i) {
            float fi = float(i);
            vec3 center = nucleus_center + vec3(0.18, -0.08 + fi * 0.07, 0.05 * sin(phase + fi));
            float golgi = max(ellipsoid_mask(p0, center, vec3(0.18, 0.03, 0.10), 0.03),
                              ellipsoid_mask(p1, center, vec3(0.18, 0.03, 0.10), 0.03));
            vec3 golgi_col = mix(vec3(0.78, 0.62, 0.28), vec3(0.52, 0.36, 0.22), smoothstep(0.30, 0.88, life_age));
            golgi_col = mix(golgi_col, vec3(0.22, 0.20, 0.18), corpse);
            color += golgi_col * golgi * 0.12;
            density += golgi * mix(0.03, 0.015, corpse);
        }
    } else {
        float er = smoothstep(0.72, 1.0,
            0.5 + 0.5 * sin(p1.x * 16.0 + p1.z * 11.0 + phase * 2.0));
        er *= smoothstep(0.56, 0.12, length(p1 - nucleus_center));
        vec3 er_col = mix(vec3(0.62, 0.50, 0.74), vec3(0.46, 0.34, 0.22), smoothstep(0.35, 0.88, life_age));
        er_col = mix(er_col, vec3(0.22, 0.22, 0.20), corpse);
        color += er_col * er * 0.10;
        density += er * mix(0.04, 0.02, corpse);
    }

    float ribosomes = smoothstep(0.90, 0.995, hash3d(p0 * 28.0 + vec3(phase)));
    ribosomes += smoothstep(0.92, 0.997, hash3d(p1 * 34.0 - vec3(phase * 0.7)));
    vec3 ribosome_col = mix(vec3(0.94, 0.88, 0.74), vec3(0.54, 0.46, 0.30), smoothstep(0.45, 0.90, life_age));
    ribosome_col = mix(ribosome_col, vec3(0.20, 0.20, 0.18), corpse);
    color += ribosome_col * ribosomes * 0.12;
    density += ribosomes * mix(0.03, 0.015, corpse);

    if (mitosis_mix > 0.0) {
        vec4 mitosis_overlay = sample_mitosis_sequence(p0, p1, p2, mitosis, phase);
        color = mix(color, cytoplasm_tint * 0.18 + mitosis_overlay.rgb, mitosis_mix);
        density = mix(density, 0.12 + mitosis_overlay.a, mitosis_mix);
    }

    return vec4(color, clamp(density, 0.0, 0.95));
}

vec4 sample_bacteria_interior(vec3 surface_pt, vec3 local_view, int morph,
                              float aspect, float noise, float phase, float division,
                              float organelle_health, float nutrient_reserve,
                              float corpse) {
    vec3 p0 = surface_pt + local_view * (0.12 + noise * 0.02);
    vec3 p1 = surface_pt + local_view * (0.26 + noise * 0.04);
    vec3 p2 = surface_pt + local_view * (0.40 + noise * 0.05);
    float life_age = corpse > 0.5 ? 1.0 : clamp(1.0 - (clamp(organelle_health, 0.0, 1.0) * 0.65 +
                                                       clamp(nutrient_reserve, 0.0, 1.0) * 0.35), 0.0, 1.0);

    vec3 cytosol = vec3(0.30, 0.22, 0.10);
    if (morph == 0)
        cytosol = vec3(0.36, 0.28, 0.14);
    else if (morph == 2)
        cytosol = vec3(0.20, 0.34, 0.18);

    vec3 aged_cytosol = mix(cytosol, vec3(0.42, 0.30, 0.14), smoothstep(0.35, 0.82, life_age));
    vec3 dead_cytosol = vec3(0.18, 0.16, 0.12);
    vec3 color = mix(aged_cytosol, dead_cytosol, corpse) *
                 (0.18 + hash3d(p1 * 9.0 + vec3(phase, -phase, phase * 0.3)) * 0.10);
    float density = mix(0.14, 0.10, corpse);

    float nucleoid = 0.0;
    if (morph == 1) {
        nucleoid = max(
            capsule_mask(p1, vec3(0.0, 0.0, -0.30), vec3(0.0, 0.0, 0.30), 0.12, 0.07),
            capsule_mask(p2, vec3(0.08 * sin(phase), 0.0, -0.24), vec3(-0.08 * sin(phase), 0.0, 0.24), 0.10, 0.07));
    } else if (morph == 2) {
        vec3 q = p1;
        q.xy -= vec2(cos(q.z * 8.0 + phase), sin(q.z * 8.0 + phase)) * 0.09;
        nucleoid = mask_from_sdf(length(vec3(q.xy, max(abs(q.z) - 0.34, 0.0))) - 0.11, 0.06);
    } else {
        nucleoid = max(
            ellipsoid_mask(p1, vec3(0.0), vec3(0.26, 0.18, 0.17), 0.07),
            ellipsoid_mask(p2, vec3(0.03 * sin(phase), 0.0, 0.0), vec3(0.22, 0.16, 0.15), 0.07));
    }
    vec3 nucleoid_col = mix(vec3(0.94, 0.72, 0.28), vec3(0.64, 0.46, 0.20), smoothstep(0.35, 0.85, life_age));
    nucleoid_col = mix(nucleoid_col, vec3(0.24, 0.22, 0.20), corpse);
    color += nucleoid_col * nucleoid * 0.22;
    density += nucleoid * mix(0.18, 0.10, corpse);

    float plasmid = torus_mask(p1, vec3(0.10, 0.0, -0.04), vec2(0.10, 0.018), 0.03);
    plasmid += torus_mask(p2, vec3(-0.08, 0.04, 0.08), vec2(0.08, 0.015), 0.03);
    vec3 plasmid_col = mix(vec3(0.66, 0.92, 0.74), vec3(0.46, 0.60, 0.38), smoothstep(0.40, 0.90, life_age));
    plasmid_col = mix(plasmid_col, vec3(0.22, 0.22, 0.20), corpse);
    color += plasmid_col * plasmid * 0.16;
    density += plasmid * mix(0.05, 0.025, corpse);

    float granules = 0.0;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase + fi * 1.9) * 0.18,
            sin(phase * 1.2 + fi * 1.3) * 0.12,
            cos(phase * 0.8 + fi * 2.1) * (morph == 1 ? 0.22 : 0.12));
        float granule = max(sphere_mask(p0, center, 0.05 + fi * 0.01, 0.04),
                            sphere_mask(p1, center, 0.05 + fi * 0.01, 0.04));
        vec3 granule_col = mix(vec3(0.84, 0.74, 0.30), vec3(0.52, 0.40, 0.18), smoothstep(0.35, 0.88, life_age));
        granule_col = mix(granule_col, vec3(0.20, 0.18, 0.16), corpse);
        color += granule_col * granule * 0.08;
        granules += granule;
    }
    density += granules * mix(0.05, 0.03, corpse);

    if (morph == 1) {
        float septum = max(
            ellipsoid_mask(p1, vec3(0.0, 0.0, 0.05), vec3(0.20, 0.20, 0.04), 0.03),
            ellipsoid_mask(p2, vec3(0.0, 0.0, -0.06), vec3(0.18, 0.18, 0.04), 0.03));
        vec3 septum_col = mix(vec3(0.92, 0.86, 0.58), vec3(0.56, 0.42, 0.22), smoothstep(0.40, 0.90, life_age));
        septum_col = mix(septum_col, vec3(0.18, 0.16, 0.14), corpse);
        color += septum_col * septum * 0.08;
        density += septum * mix(0.04, 0.02, corpse);
    }

    float ribosomes = smoothstep(0.87, 0.995, hash3d(p0 * 36.0 + vec3(phase * 0.5)));
    ribosomes += smoothstep(0.91, 0.998, hash3d(p1 * 44.0 - vec3(phase * 0.8)));
    vec3 ribosome_col = mix(vec3(0.98, 0.88, 0.60), vec3(0.62, 0.50, 0.28), smoothstep(0.40, 0.92, life_age));
    ribosome_col = mix(ribosome_col, vec3(0.20, 0.20, 0.18), corpse);
    color += ribosome_col * ribosomes * 0.10;
    density += ribosomes * mix(0.08, 0.04, corpse);

    if (division > 0.0 && corpse < 0.5) {
        vec4 fission_overlay = sample_binary_fission_sequence(p0, p1, p2, division, morph, phase);
        float fission_mix = smoothstep(0.04, 0.22, division);
        color = mix(color, color * 0.82 + fission_overlay.rgb, fission_mix * 0.72);
        density += fission_overlay.a;
    }

    return vec4(color, clamp(density, 0.0, 0.85));
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

vec3 background(vec3 rd, float time, vec3 env_tint, float oxygen, float nutrients,
                float acidity, float toxicity) {
    vec3 base = vec3(0.010, 0.025, 0.035);
    vec3 tinted = base + env_tint * vec3(0.10, 0.14, 0.18);
    vec3 col = mix(base, tinted, 0.85);

    // Subtle caustic patterns (like light through water)
    vec2 uv = rd.xz / (abs(rd.y) + 0.5);
    float c1 = sin(uv.x * 8.0 + time * 0.3) * sin(uv.y * 6.0 + time * 0.2);
    float c2 = sin(uv.x * 5.0 - time * 0.4 + 2.0) * sin(uv.y * 9.0 - time * 0.15);
    float caustic = max(0.0, c1 + c2) * 0.012;
    col += mix(vec3(caustic * 0.18, caustic * 0.45, caustic * 0.35),
               env_tint * caustic * (0.35 + oxygen * 0.25),
               0.65);

    // Floating debris particles
    for (int layer = 0; layer < 2; layer++) {
        float scale = 300.0 + float(layer) * 200.0;
        vec2 cell = floor(rd.xz * scale / (abs(rd.y) + 0.3));
        float h = hash(cell + float(layer) * 71.0);
        if (h > 0.96) {
            float brightness = (h - 0.96) / 0.04;
            brightness *= brightness * (0.08 + nutrients * 0.07 + toxicity * 0.05);
            vec3 debris_col = mix(vec3(0.2, 0.5, 0.3), vec3(0.3, 0.6, 0.7), hash(cell + 37.0));
            debris_col = mix(debris_col, env_tint + vec3(0.08, 0.10, 0.12), 0.45);
            col += debris_col * brightness;
        }
    }

    float acidic_shift = clamp((7.1 - acidity) * 0.5 + 0.5, 0.0, 1.0);
    col += mix(vec3(0.01, 0.02, 0.01), vec3(0.04, 0.02, 0.03), acidic_shift) * 0.04;
    col += vec3(0.08, 0.04, 0.02) * toxicity * 0.10;

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

vec4 trace_environment_media(vec3 ro, vec3 rd, float max_t, int feature_count) {
    vec3 accum = vec3(0.0);
    float density = 0.0;
    float limit = max(max_t, 350.0);

    for (int i = 0; i < feature_count && i < 128; ++i) {
        vec3 center = features[i].pos_radius.xyz;
        float radius = features[i].pos_radius.w;
        vec3 to_center = center - ro;
        float t = clamp(dot(to_center, rd), 0.0, limit);
        vec3 closest = ro + rd * t;
        float d = distance(closest, center);
        if (d >= radius) continue;

        float influence = 1.0 - d / radius;
        influence = pow(max(influence, 0.0), 1.2 + features[i].meta.x * 2.0) * features[i].axis_strength.w;

        vec3 tint = features[i].tint_type.rgb;
        float type = features[i].tint_type.a;
        float gain = 0.10;
        if (type > 0.5 && type < 1.5) gain = 0.18;
        else if (type > 1.5 && type < 2.5) gain = 0.16;
        else if (type > 2.5) gain = 0.09;

        accum += tint * influence * gain;
        density += influence * gain * 1.4;
    }

    return vec4(accum, clamp(density, 0.0, 1.0));
}

// ── Main raytracing ────────────────────────────────────────────────────────

void main() {
    float W = screen_info.x;
    float H = screen_info.y;
    int entity_count = int(screen_info.z);
    float time = screen_info.w;
    int feature_count = int(lighting_params.x);
    float ambient = lighting_params.z;
    vec3 env_tint = environment_color.rgb;
    float haze_density = environment_color.a;
    float oxygen = environment_factors.x;
    float nutrients = environment_factors.y;
    float acidity = environment_factors.z;
    float toxicity = environment_factors.w;

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
        vec4 media = trace_environment_media(ro, rd, 1200.0, feature_count);
        outColor = vec4(background(rd, time, env_tint, oxygen, nutrients, acidity, toxicity) + media.rgb, 1.0);
        return;
    }

    // ── Hit — compute shading ──────────────────────────────────────────────
    Sphere hit = spheres[closest_idx];
    vec3 hit_pos;
    vec3 normal;
    vec3 surface_pt;
    refine_entity_hit(ro, rd, hit, closest_t, closest_t, hit_pos, normal, surface_pt);

    vec3 base_color = hit.color_type.rgb;
    int entity_type = int(hit.color_type.a + 0.5);
    int morphology = int(hit.axis_morph.a + 0.5);
    float radius = hit.pos_radius.w;
    float aspect = hit.shape_params.x;
    float shape_noise = hit.shape_params.y;
    float phase = hit.shape_params.z;
    float mitosis = hit.shape_params.w;
    float organelle_health = hit.life_params.x;
    float nutrient_reserve = hit.life_params.y;
    float corpse = hit.life_params.z;
    float telomere_state = hit.life_params.w;
    float infection_progress = hit.signal_params.x;
    float infection_load = hit.signal_params.y;
    int infection_morphology = int(hit.signal_params.z + 0.5);
    float antibiotic_film = hit.signal_params.w;
    float antibiotic_type = hit.gene_params.x;
    float antibiotic_diversity = hit.gene_params.y;
    float antibiotic_yield = hit.gene_params.z;
    mat3 hit_basis = basis_from_axis(hit.axis_morph.xyz);
    vec3 infection_axis_local = transpose(hit_basis) * hit.aux_params.xyz;
    infection_axis_local = length(infection_axis_local) > 1e-4
        ? normalize(infection_axis_local)
        : vec3(1.0, 0.0, 0.0);
    float bacteria_core_sdf = 0.0;
    bool bacteria_cloud_hit = false;
    if (entity_type == 1) {
        bacteria_core_sdf = sd_bacteria_shape(surface_pt, morphology, aspect, shape_noise, phase, mitosis);
        bacteria_cloud_hit = (antibiotic_film > 0.02 && corpse < 0.5 && bacteria_core_sdf > 0.025);
    }

    if (entity_type == 0) {
        if (morphology == 1) base_color = mix(base_color, vec3(0.78, 0.88, 0.74), 0.45);
        else if (morphology == 2) base_color = mix(base_color, vec3(0.48, 0.86, 0.70), 0.42);
        else base_color = mix(base_color, vec3(0.72, 0.78, 1.00), 0.28);
    } else if (entity_type == 1) {
        if (morphology == 0) base_color = mix(base_color, vec3(0.96, 0.74, 0.36), 0.32);
        else if (morphology == 1) base_color = mix(base_color, vec3(0.93, 0.66, 0.18), 0.24);
        else base_color = mix(base_color, vec3(0.58, 0.92, 0.62), 0.35);
    } else if (entity_type == 2) {
        if (morphology == 1) base_color = mix(base_color, vec3(0.96, 0.42, 0.18), 0.38);
        else if (morphology == 2) base_color = mix(base_color, vec3(0.48, 0.96, 0.82), 0.58);
        else base_color = mix(base_color, vec3(0.88, 0.22, 0.44), 0.24);
    } else if (entity_type == 7) {
        base_color = mix(base_color, vec3(0.82, 0.92, 0.76), 0.36);
    } else if (entity_type == 8) {
        base_color = mix(base_color, vec3(0.52, 0.88, 0.70), 0.42);
    }
    if (corpse > 0.5)
        base_color = mix(base_color, vec3(0.22, 0.20, 0.18), 0.72);

    vec3 V = normalize(eye_pos.xyz - hit_pos);
    vec3 local_view = normalize(transpose(hit_basis) * (-V));

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
    if (entity_type == 2)
        diffuse = base_color * key_color * key_ndl * 0.88
                + base_color * fill_color * fill_ndl * 0.18;

    // ── Ambient ────────────────────────────────────────────────────────────
    // Hemispheric ambient (warmer above, cooler below)
    float hemi = 0.5 + 0.5 * normal.y;
    vec3 lower_ambient = mix(vec3(0.04, 0.06, 0.08), env_tint * 0.35 + vec3(0.03, 0.04, 0.05), 0.65);
    vec3 upper_ambient = env_tint * 0.55 + vec3(0.08, 0.10, 0.12);
    vec3 ambient_light = mix(lower_ambient, upper_ambient, hemi);
    vec3 amb = base_color * (ambient + 0.08) * ambient_light / 0.1;

    // ── Specular — soft, wet-looking ───────────────────────────────────────
    vec3 H_key = normalize(key_dir + V);
    float spec_key = pow(max(dot(normal, H_key), 0.0), 16.0);
    // Wet/membrane sheen — use Schlick fresnel
    float fresnel = pow(1.0 - max(dot(normal, V), 0.0), 3.0);
    float spec = spec_key * 0.25 + fresnel * 0.15;
    vec3 spec_color = mix(vec3(0.5, 0.7, 0.6), vec3(0.8, 0.85, 0.9), fresnel);
    if (entity_type == 2) {
        spec = pow(max(dot(normal, H_key), 0.0), 34.0) * 0.42 + fresnel * 0.10;
        spec_color = mix(vec3(0.82, 0.86, 0.92), vec3(1.0), fresnel * 0.55);
    }

    // ── Subsurface scattering ──────────────────────────────────────────────
    vec3 sss = subsurface(normal, key_dir, V, base_color, radius);
    if (entity_type == 2)
        sss *= 0.08;

    // ── Rim/edge glow ──────────────────────────────────────────────────────
    float rim = pow(1.0 - max(dot(normal, V), 0.0), 2.5);
    vec3 rim_color = base_color * 0.5 + vec3(0.1, 0.2, 0.15);
    if (entity_type == 2)
        rim_color = mix(base_color, vec3(0.95, 0.96, 1.0), 0.30);

    // ── Membrane detail (procedural surface noise) ─────────────────────────
    float noise1 = hash3d(surface_pt * 8.0 + vec3(time * 0.1 + phase));
    float noise2 = hash3d(surface_pt * 16.0 - vec3(time * 0.05 - phase));
    float membrane = 0.85 + 0.15 * (noise1 * 0.6 + noise2 * 0.4);
    if (entity_type == 2)
        membrane = 0.96 + 0.04 * (noise1 * 0.55 + noise2 * 0.45);

    float spike = 0.0;
    if (entity_type == 2) {
        float theta = atan(surface_pt.z, surface_pt.x);
        float phi = asin(clamp(surface_pt.y, -1.0, 1.0));
        float s = sin(theta * (6.0 + aspect) + time + phase) * sin(phi * 5.0 + time * 0.7);
        spike = max(0.0, s) * (0.10 + shape_noise * 0.18);
    }

    vec3 shell_color = (diffuse + amb + sss) * membrane
                     + spec_color * spec
                     + rim_color * rim * 0.35
                     + base_color * spike;
    if (entity_type == 2) {
        shell_color = base_color * 0.24 + diffuse * 0.78 + amb * 0.52
                    + spec_color * (0.35 + spike * 0.9)
                    + rim_color * rim * 0.16;
    }
    vec3 interior_color = vec3(0.0);
    float interior_alpha = 0.0;
    vec3 final_color = shell_color;

    if (entity_type == 0 || entity_type == 1 || entity_type == 7 || entity_type == 8) {
        vec4 interior = entity_type == 1
            ? (bacteria_cloud_hit
                ? vec4(0.0)
                : sample_bacteria_interior(surface_pt, local_view, morphology, aspect, shape_noise, phase, mitosis,
                                           organelle_health, nutrient_reserve, corpse))
            : sample_cell_interior(surface_pt, local_view,
                                   (entity_type == 7 || entity_type == 8) ? 2 : morphology,
                                   (entity_type == 7 || entity_type == 8) ? 1.0 : aspect,
                                   shape_noise, phase, mitosis,
                                   infection_progress, infection_load, infection_morphology, infection_axis_local,
                                   organelle_health, nutrient_reserve, telomere_state, corpse);
        float translucency = entity_type == 1 ? 0.34 : 0.42;
        if (corpse > 0.5)
            translucency *= 0.35;
        translucency *= 0.55 + 0.45 * max(dot(normal, V), 0.0);
        translucency *= 0.85 + 0.15 * membrane;
        if (entity_type == 1 && bacteria_cloud_hit)
            translucency *= 0.18;
        interior_color = interior.rgb;
        interior_alpha = interior.a * translucency;
        final_color = mix(shell_color, shell_color * 0.90 + interior_color, interior_alpha);
    }

    if (entity_type == 0) {
        vec3 membrane_ridge = vec3(
            sin(surface_pt.x * 18.0 + phase),
            sin(surface_pt.y * 16.0 - phase * 1.3),
            sin(surface_pt.z * 14.0 + phase * 0.8));
        float cortex = 0.5 + 0.5 * dot(membrane_ridge, vec3(0.3333));
        final_color += vec3(0.14, 0.10, 0.18) * cortex * 0.08;
    } else if (entity_type == 1) {
        float band = 0.55 + 0.45 * sin(surface_pt.z * (18.0 + aspect * 3.0) + phase * 2.0);
        final_color *= mix(0.82, 1.10, band);
        if (antibiotic_film > 0.02 && corpse < 0.5) {
            vec3 film_color = spectrum_color(antibiotic_type, antibiotic_diversity);
            float film_shell = 1.0 - smoothstep(-0.12, 0.18 + antibiotic_film * 0.22, bacteria_core_sdf);
            float cloud_noise = 0.5 + 0.5 * sin(dot(surface_pt, vec3(19.0, 15.0, 13.0)) + phase * 2.1);
            float iridescence = 0.5 + 0.5 * sin(surface_pt.z * 22.0 + phase * 2.0 + antibiotic_diversity * 4.0);
            if (bacteria_cloud_hit) {
                final_color = mix(film_color * (0.52 + antibiotic_yield * 0.10),
                                  film_color * (0.86 + cloud_noise * 0.18) + vec3(0.06, 0.08, 0.10),
                                  iridescence * 0.45);
                final_color += film_color * cloud_noise * antibiotic_film * 0.22;
            } else {
                final_color = mix(final_color, final_color * 0.82 + film_color * (0.66 + antibiotic_yield * 0.12),
                                  film_shell * antibiotic_film * 0.60);
                final_color += film_color * film_shell * antibiotic_film * (0.08 + 0.06 * iridescence);
            }
        }
    } else if (entity_type == 8) {
        float vacuole = smoothstep(0.64, 0.98, hash3d(surface_pt * 13.0 + vec3(phase * 0.5)));
        final_color += vec3(0.08, 0.16, 0.10) * vacuole * 0.10;
    } else if (entity_type == 2 && morphology == 2) {
        float tail_stripe = 0.55 + 0.45 * sin(surface_pt.z * 34.0 - phase * 2.0);
        final_color *= mix(0.86, 1.08, tail_stripe);
    }

    if (entity_type == 0 && infection_progress > 0.0 && corpse < 0.5) {
        float lesion = 0.5 + 0.5 * sin(surface_pt.x * 16.0 + surface_pt.y * 19.0 + phase * 2.4);
        final_color = mix(final_color, final_color * vec3(0.92, 0.78, 0.78) + vec3(0.36, 0.06, 0.08) * lesion,
                          smoothstep(0.08, 0.95, infection_progress) * 0.26);
        final_color += vec3(0.88, 0.16, 0.14) * rim * infection_progress * 0.12;
    }

    if (corpse > 0.5) {
        float wrinkle = 0.5 + 0.5 * sin(surface_pt.x * 22.0 + surface_pt.y * 17.0 - phase * 1.7);
        final_color = mix(final_color, final_color * vec3(0.56, 0.48, 0.42), 0.45);
        final_color -= vec3(0.08, 0.06, 0.04) * wrinkle * 0.35;
    }

    if (entity_type == 3) {
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

    vec4 media = trace_environment_media(ro, rd, closest_t, feature_count);
    vec3 bg = background(rd, time, env_tint, oxygen, nutrients, acidity, toxicity);

    if (entity_type == 0) {
        float membrane_alpha = clamp(0.18 + fresnel * 0.26 + rim * 0.10 + membrane * 0.05, 0.20, 0.44);
        vec3 transparent_fill = mix(bg + media.rgb * 0.24,
                                    interior_color + shell_color * 0.20,
                                    clamp(interior_alpha + 0.20, 0.20, 0.88));
        final_color = mix(transparent_fill, shell_color + interior_color * 0.78, membrane_alpha);
        final_color += vec3(0.10, 0.18, 0.22) * fresnel * 0.10;
    }

    final_color += media.rgb;

    float haze = 1.0 - exp(-closest_t * haze_density);
    haze *= 0.35 + nutrients * 0.10 + toxicity * 0.35;
    final_color = mix(final_color, bg + media.rgb * 0.35, clamp(haze + media.a * 0.22, 0.0, 0.82));

    // Gamma correction
    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
