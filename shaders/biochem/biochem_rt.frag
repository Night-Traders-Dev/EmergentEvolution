#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── Camera / scene uniforms ────────────────────────────────────────────────

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;            // inverse view-projection matrix
    vec4 eye_pos;           // xyz = camera position
    vec4 screen_info;       // x = width, y = height, z = entity_count, w = time
    vec4 lighting_params;   // x = feature_count, y = antibiotic visibility, z = ambient, w = unused
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
    vec4 signal_params; // x = viral infection progress, y = viral infection load, z = viral infection morph, w = antibiotic film
    vec4 gene_params;   // x = antibiotic type, y = antibiotic diversity, z = antibiotic yield, w = bacterial infection progress
    vec4 aux_params;    // xyz = viral infection axis, w = bacterial infection load
};

layout(std430, set = 0, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

struct EnvFeature {
    vec4 pos_radius;
    vec4 axis_strength;
    vec4 tint_type;
    vec4 meta; // x = falloff, y = noise, z = structure shape, w = opacity
};

layout(std430, set = 0, binding = 2) readonly buffer FeatureBuffer {
    EnvFeature features[];
};

const float FEATURE_MEMBRANE = 0.0;
const float FEATURE_NUTRIENT = 1.0;
const float FEATURE_TOXIN = 2.0;
const float FEATURE_CURRENT = 3.0;
const float FEATURE_STRUCTURE = 4.0;

const int STRUCT_LUNG_BRANCH = 0;
const int STRUCT_ALVEOLAR_CLUSTER = 1;
const int STRUCT_POND_REED = 2;
const int STRUCT_POND_ROCK = 3;
const int STRUCT_PETRI_RIM = 4;
const int STRUCT_PETRI_AGAR = 5;
const int STRUCT_BRAIN_FOLD = 6;
const int STRUCT_BRAIN_VESSEL = 7;
const int STRUCT_GUT_VILLUS = 8;
const int STRUCT_GUT_CRYPT = 9;
const int STRUCT_BLOOD_WALL = 10;
const int STRUCT_BLOOD_VALVE = 11;
const int STRUCT_SOIL_GRAIN = 12;
const int STRUCT_SOIL_ROOT = 13;
const int STRUCT_WOUND_FIBRIN = 14;
const int STRUCT_WOUND_TISSUE = 15;

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

float luminance3(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 contrast_color(vec3 color, vec3 backdrop) {
    float contrast = abs(luminance3(color) - luminance3(backdrop));
    vec3 fallback = normalize(abs(cross(backdrop + vec3(0.11, 0.09, 0.07), vec3(0.28, 0.88, 0.44))) + vec3(0.001));
    fallback = mix(fallback, vec3(0.96, 0.98, 1.0), 0.24);
    return mix(color, fallback, 1.0 - smoothstep(0.18, 0.36, contrast));
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

float sd_influenza(vec3 p, float noise, float phase) {
    float body = sd_ellipsoid(p, vec3(0.60, 0.54, 0.60));
    for (int i = 0; i < 12; ++i) {
        float fi = float(i);
        float z = 1.0 - 2.0 * (fi + 0.5) / 12.0;
        float r = sqrt(max(1.0 - z * z, 0.0));
        float a = fi * 2.39996323 + phase * 0.7;
        vec3 dir = normalize(vec3(cos(a) * r, sin(a) * r, z));
        float spike = sd_capsule(p, dir * 0.42, dir * (0.62 + noise * 0.05), 0.032 + noise * 0.015);
        body = min(body, spike);
    }
    return body;
}

float sd_virus_shape(vec3 p, int morph, float aspect, float noise, float phase) {
    if (morph == 1)
        return sd_corona(p, noise, phase);
    if (morph == 2)
        return sd_phage(vec3(p.xy, p.z * clamp(aspect / 2.8, 0.8, 1.15)), noise, phase);
    if (morph == 3)
        return sd_influenza(p, noise, phase);

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
            float antibiotic_visibility = lighting_params.y;
            float cloud = sd_antibiotic_cloud(p, morph, aspect, noise, phase, division,
                                              antibiotic_film * antibiotic_visibility, antibiotic_diversity);
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
    else if (virus_morph == 3)
        virus_col = vec3(0.84, 0.42, 0.18);

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
    color += virus_col * ingress * entry * 0.84;
    color += mix(vec3(0.98, 0.78, 0.28), virus_col, 0.35) * pore * entry * 0.38;
    density += ingress * entry * 0.24 + pore * entry * 0.08;

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
        float pulse = 0.65 + 0.35 * sin(phase * 3.2 + fi * 1.4 + progress * 8.0);
        color += virus_col * virion * swarm * is_active * pulse * (0.24 + crowding * 0.12);
        density += virion * swarm * is_active * pulse * (0.072 + crowding * 0.040);
    }

    float rupture = smoothstep(0.76, 1.02, progress) + crowding * 0.45;
    float stress = smoothstep(0.78, 0.98, hash3d(p1 * 18.0 + vec3(phase * 1.6)));
    float crack = smoothstep(0.78, 0.96, abs(sin(dot(p1, entry_tangent * 11.0 + entry_bitangent * 9.0) + phase * 2.7)));
    color += vec3(0.98, 0.28, 0.22) * rupture * stress * 0.28;
    color += vec3(1.0, 0.80, 0.20) * rupture * crack * 0.16;
    density += rupture * stress * 0.12 + rupture * crack * 0.05;

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
    color += vec3(1.00, 0.82, 0.30) * nuc * (replication * 0.22 + elongation * 0.14);
    density += nuc * (replication * 0.10 + elongation * 0.07);

    vec3 septum_radii = (morph == 0) ? vec3(0.08, 0.19, 0.19) : vec3(0.19, 0.19, 0.08);
    float septum = max(ellipsoid_mask(p1, vec3(0.0), septum_radii, 0.03),
                       ellipsoid_mask(p2, vec3(0.0), septum_radii * 0.92, 0.03));
    float ring = torus_mask(p1, vec3(0.0), vec2(morph == 0 ? 0.20 : 0.24, 0.018), 0.028);
    color += vec3(0.92, 0.96, 0.58) * septum * septation * 0.14;
    color += vec3(0.62, 1.00, 0.92) * ring * septation * 0.11;
    density += septum * septation * 0.06 + ring * septation * 0.05;

    float split_glow = smoothstep(0.0, 1.0, separation) *
                       smoothstep(0.78, 0.96, abs(dot(p1, split_axis)));
    color += vec3(0.62, 0.92, 1.00) * split_glow * 0.14;
    density += split_glow * 0.04;

    return vec4(color, clamp(density, 0.0, 0.32));
}

// ── Mitosis sub-helpers ───────────────────────────────────────────────────

vec4 mitosis_centrosomes_and_asters(vec3 p1, vec3 pole_a, vec3 pole_b,
                                     float progress, float phase, float cytokinesis) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    // Centrosome bodies — small bright dots at spindle poles
    float centro_a = sphere_mask(p1, pole_a, 0.032, 0.025);
    float centro_b = sphere_mask(p1, pole_b, 0.032, 0.025);
    float centro_vis = smoothstep(0.02, 0.14, progress) * (1.0 - cytokinesis * 0.6);
    color += vec3(0.92, 0.96, 0.40) * (centro_a + centro_b) * centro_vis * 0.32;
    density += (centro_a + centro_b) * centro_vis * 0.08;

    // Aster microtubule fibers radiating from centrosomes
    float aster_vis = smoothstep(0.06, 0.22, progress) * (1.0 - smoothstep(0.80, 0.94, progress));
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float a = fi * 1.047198 + phase * 0.3;
        vec3 ray_dir = normalize(vec3(cos(a) * 0.4 - 1.0, sin(a), cos(a + 1.5) * 0.5));
        float fiber_a = capsule_mask(p1, pole_a, pole_a + ray_dir * 0.22, 0.006, 0.015);
        float fiber_b = capsule_mask(p1, pole_b, pole_b - ray_dir * 0.22, 0.006, 0.015);
        color += vec3(0.48, 0.78, 0.92) * (fiber_a + fiber_b) * aster_vis * 0.06;
        density += (fiber_a + fiber_b) * aster_vis * 0.015;
    }
    return vec4(color, density);
}

vec4 mitosis_nuclear_envelope(vec3 p1, float progress, float phase,
                               float prometaphase, float metaphase) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    float envelope_vis = 1.0 - smoothstep(0.18, 0.36, progress);
    // Early intact envelope
    float envelope = torus_mask(p1, vec3(0.0), vec2(0.22, 0.014), 0.03) * envelope_vis;
    color += vec3(0.56, 0.42, 0.68) * envelope * 0.18;
    density += envelope * 0.04;
    // Breakdown fragments during prometaphase
    float breakdown = prometaphase * (1.0 - metaphase);
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec3 frag_pos = vec3(sin(fi * 2.3 + phase) * 0.28, cos(fi * 1.7 + phase) * 0.18,
                             sin(fi * 3.1 - phase) * 0.14);
        float frag = sphere_mask(p1, frag_pos, 0.025, 0.022) * breakdown;
        color += vec3(0.50, 0.38, 0.62) * frag * 0.14;
        density += frag * 0.025;
    }
    return vec4(color, density);
}

vec4 mitosis_chromosomes(vec3 p1, vec3 p2, vec3 pole_a, vec3 pole_b, float progress, float phase,
                          float prophase, float prometaphase, float metaphase, float anaphase) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    float meta_vis = max(prometaphase, metaphase);
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        vec3 chr_axis = normalize(vec3(
            cos(fi * 1.7 + phase * 0.4), sin(fi * 1.4 - phase * 0.3), cos(fi * 0.9 + phase * 0.5)));
        vec3 chr_perp = normalize(cross(chr_axis, vec3(0.0, 1.0, 0.1)));

        // Prophase: condensing X-shaped chromosomes near center
        vec3 pro_c = vec3(sin(phase * 0.8 + fi * 1.3) * 0.13,
                          cos(phase * 0.5 + fi * 1.6) * 0.12,
                          sin(phase * 0.6 + fi * 1.9) * 0.10);
        float arm = 0.065 + fi * 0.004;
        float pro_chr = max(
            capsule_mask(p1, pro_c - chr_axis * arm + chr_perp * 0.012,
                             pro_c + chr_axis * arm - chr_perp * 0.012, 0.018, 0.022),
            capsule_mask(p1, pro_c - chr_axis * arm - chr_perp * 0.012,
                             pro_c + chr_axis * arm + chr_perp * 0.012, 0.018, 0.022));
        float centromere = sphere_mask(p2, pro_c, 0.022, 0.018);
        color += vec3(0.94, 0.44, 0.78) * pro_chr * prophase * 0.22;
        color += vec3(1.0, 0.82, 0.36) * centromere * prophase * 0.16;
        density += pro_chr * prophase * 0.07 + centromere * prophase * 0.03;

        // Metaphase: aligned on metaphase plate
        float plate_y = -0.22 + fi * 0.064;
        vec3 meta_c = vec3(0.0, plate_y, 0.035 * sin(phase * 0.6 + fi * 1.2));
        float meta_chr = max(
            capsule_mask(p1, meta_c - vec3(0.0, 0.045, 0.0), meta_c + vec3(0.0, 0.045, 0.0), 0.020, 0.024),
            capsule_mask(p2, meta_c - vec3(0.0, 0.045, 0.0), meta_c + vec3(0.0, 0.045, 0.0), 0.020, 0.024));
        // Kinetochore attachment points
        float kineto = sphere_mask(p2, meta_c + vec3(-0.028, 0.0, 0.0), 0.012, 0.012)
                     + sphere_mask(p2, meta_c + vec3( 0.028, 0.0, 0.0), 0.012, 0.012);
        // Spindle fibers from poles to kinetochores
        float fibers = capsule_mask(p1, pole_a, meta_c + vec3(-0.024, 0.0, 0.0), 0.008, 0.016)
                     + capsule_mask(p1, pole_b, meta_c + vec3( 0.024, 0.0, 0.0), 0.008, 0.016);
        color += vec3(0.96, 0.58, 0.86) * meta_chr * meta_vis * 0.22;
        color += vec3(1.0, 0.92, 0.28) * kineto * meta_vis * 0.14;
        color += vec3(0.52, 0.86, 0.98) * fibers * meta_vis * 0.09;
        density += (meta_chr * 0.08 + kineto * 0.025 + fibers * 0.03) * meta_vis;

        // Anaphase: sister chromatids pulled to poles with V-shaped trailing arms
        float split_off = 0.10 + smoothstep(0.50, 0.76, progress) * 0.22;
        float ana_y = -0.18 + fi * 0.052;
        vec3 ana_l = vec3(-split_off, ana_y,  0.024 * cos(phase + fi));
        vec3 ana_r = vec3( split_off, ana_y, -0.024 * cos(phase + fi));
        vec3 trail = chr_axis * 0.05;
        float ana_chr = max(
            max(capsule_mask(p1, ana_l - trail, ana_l + trail, 0.016, 0.022),
                capsule_mask(p2, ana_l - trail, ana_l + trail, 0.016, 0.022)),
            max(capsule_mask(p1, ana_r - trail, ana_r + trail, 0.016, 0.022),
                capsule_mask(p2, ana_r - trail, ana_r + trail, 0.016, 0.022)));
        float ana_fib = capsule_mask(p1, pole_a, ana_l, 0.007, 0.015)
                      + capsule_mask(p1, pole_b, ana_r, 0.007, 0.015);
        color += vec3(0.96, 0.64, 0.88) * ana_chr * anaphase * 0.20;
        color += vec3(0.54, 0.90, 0.98) * ana_fib * anaphase * 0.08;
        density += (ana_chr * 0.07 + ana_fib * 0.03) * anaphase;
    }
    return vec4(color, density);
}

vec4 mitosis_telophase_cytokinesis(vec3 p1, vec3 p2, float progress, float phase,
                                    float telophase, float cytokinesis) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    // Two reforming nuclei
    float tel_spread = 0.22 + smoothstep(0.72, 0.92, progress) * 0.08;
    vec3 tel_a = vec3(-tel_spread, 0.0, 0.0);
    vec3 tel_b = vec3( tel_spread, 0.0, 0.0);
    vec3 tel_r = vec3(0.16, 0.14, 0.15);
    float nuc_a = max(ellipsoid_mask(p1, tel_a, tel_r, 0.05),
                      ellipsoid_mask(p2, tel_a, tel_r * 0.94, 0.05));
    float nuc_b = max(ellipsoid_mask(p1, tel_b, tel_r, 0.05),
                      ellipsoid_mask(p2, tel_b, tel_r * 0.94, 0.05));
    // Reforming nuclear envelopes
    float env_reform = smoothstep(0.74, 0.90, progress);
    float env_a = torus_mask(p1, tel_a, vec2(0.16, 0.012), 0.025) * env_reform;
    float env_b = torus_mask(p1, tel_b, vec2(0.16, 0.012), 0.025) * env_reform;
    color += vec3(0.56, 0.32, 0.74) * (nuc_a + nuc_b) * telophase * 0.26;
    color += vec3(0.50, 0.40, 0.66) * (env_a + env_b) * 0.15;
    density += ((nuc_a + nuc_b) * 0.10 + (env_a + env_b) * 0.03) * telophase;

    // Contractile ring constricting + midbody
    float constrict = smoothstep(0.86, 0.98, progress);
    float ring_r = mix(0.28, 0.08, constrict);
    float ring = torus_mask(p1, vec3(0.0), vec2(ring_r, 0.018 + constrict * 0.010), 0.025);
    ring += torus_mask(p2, vec3(0.0), vec2(ring_r * 0.94, 0.016), 0.025) * 0.6;
    float midbody = capsule_mask(p1, vec3(-0.04, 0.0, 0.0), vec3(0.04, 0.0, 0.0), 0.022, 0.020)
                  * smoothstep(0.92, 0.98, progress);
    color += vec3(0.62, 0.94, 1.00) * ring * cytokinesis * 0.14;
    color += vec3(0.88, 0.96, 0.44) * midbody * 0.22;
    density += (ring * 0.05 + midbody * 0.04) * cytokinesis;
    return vec4(color, density);
}

// ── Mitosis sequence (6 sub-stages) ──────────────────────────────────────

vec4 sample_mitosis_sequence(vec3 p0, vec3 p1, vec3 p2, float progress, float phase) {
    float prophase     = stage_band(progress, 0.04, 0.26);
    float prometaphase = stage_band(progress, 0.22, 0.38);
    float metaphase    = stage_band(progress, 0.34, 0.54);
    float anaphase     = stage_band(progress, 0.50, 0.76);
    float telophase    = stage_band(progress, 0.72, 0.90);
    float cytokinesis  = smoothstep(0.86, 0.99, progress);

    vec3 color = vec3(0.0);
    float density = 0.0;

    // Centrosome positions migrate from near-nucleus to poles
    float pole_spread = smoothstep(0.04, 0.32, progress);
    vec3 pole_a = vec3(-0.14 - pole_spread * 0.30, 0.0, 0.0);
    vec3 pole_b = vec3( 0.14 + pole_spread * 0.30, 0.0, 0.0);

    // Centrosomes + aster fibers
    vec4 ca = mitosis_centrosomes_and_asters(p1, pole_a, pole_b, progress, phase, cytokinesis);
    color += ca.rgb; density += ca.a;

    // Nuclear envelope breakdown/reform
    vec4 ne = mitosis_nuclear_envelope(p1, progress, phase, prometaphase, metaphase);
    color += ne.rgb; density += ne.a;

    // Chromosomes through all stages
    vec4 chr = mitosis_chromosomes(p1, p2, pole_a, pole_b, progress, phase,
                                    prophase, prometaphase, metaphase, anaphase);
    color += chr.rgb; density += chr.a;

    // Telophase nuclei + cytokinesis contractile ring + midbody
    vec4 tc = mitosis_telophase_cytokinesis(p1, p2, progress, phase, telophase, cytokinesis);
    color += tc.rgb; density += tc.a;

    return vec4(color, clamp(density, 0.0, 0.52));
}

// ── Cell interior organelle helpers ───────────────────────────────────────

vec4 organelle_nucleus(vec3 p1, vec3 p2, vec3 nucleus_center, vec3 nucleus_radii,
                        float phase, float life_age, float corpse) {
    vec3 color = vec3(0.0);
    float density = 0.0;

    // Nuclear envelope — double membrane with pores
    float nuc_shell = max(ellipsoid_mask(p1, nucleus_center, nucleus_radii, 0.06),
                          ellipsoid_mask(p2, nucleus_center, nucleus_radii * 0.96, 0.06));
    float nuc_inner = ellipsoid_mask(p1, nucleus_center, nucleus_radii * 0.88, 0.05);
    float envelope = max(nuc_shell - nuc_inner * 0.7, 0.0);
    // Nuclear pores — small holes in the envelope
    float pore_pattern = smoothstep(0.88, 0.96,
        sin(atan(p1.z - nucleus_center.z, p1.x - nucleus_center.x) * 8.0 + phase) *
        sin(asin(clamp((p1.y - nucleus_center.y) / max(nucleus_radii.y, 0.01), -1.0, 1.0)) * 6.0));
    envelope *= (1.0 - pore_pattern * 0.4);

    vec3 nuc_col = mix(vec3(0.40, 0.22, 0.52), vec3(0.58, 0.40, 0.20), smoothstep(0.35, 0.85, life_age));
    color += nuc_col * nuc_shell * mix(0.42, 0.22, corpse);
    color += vec3(0.48, 0.36, 0.62) * envelope * 0.14;
    density += nuc_shell * mix(0.28, 0.16, corpse) + envelope * 0.06;

    // Nucleolus — dense RNA-rich body
    float nucleolus = sphere_mask(p2,
        nucleus_center + vec3(0.05 * cos(phase * 2.1), -0.03, 0.05 * sin(phase * 1.7)),
        0.08, 0.05) * nuc_shell;
    vec3 nucleolus_col = mix(vec3(0.82, 0.56, 0.86), vec3(0.42, 0.36, 0.30), corpse);
    color += nucleolus_col * nucleolus * mix(0.40, 0.12, life_age);
    density += nucleolus * mix(0.12, 0.04, corpse);

    // Chromatin — diffuse DNA network inside nucleus
    float chromatin = hash3d(p2 * 22.0 + nucleus_center + vec3(phase * 0.4));
    chromatin = smoothstep(0.62, 0.88, chromatin) * nuc_inner;
    color += mix(vec3(0.68, 0.38, 0.72), vec3(0.42, 0.28, 0.22), life_age) * chromatin * 0.12;
    density += chromatin * 0.04;

    return vec4(color, density);
}

vec4 organelle_mitochondria(vec3 p0, vec3 p1, int morph, float phase,
                             float life_age, float corpse) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    // 6 mitochondria with double-membrane, cristae folds, and matrix
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            sin(phase * 0.7 + fi * 1.7) * 0.32,
            cos(phase * 1.1 + fi * 1.8) * (morph == 1 ? 0.10 : 0.20),
            sin(phase * 0.6 + fi * 1.2) * 0.24);
        vec3 axis = normalize(vec3(
            cos(fi * 2.0 + phase * 0.5),
            sin(fi * 1.4 - phase * 0.4),
            cos(fi * 0.9 + phase * 0.8)));
        float len = 0.10 + fi * 0.008;
        // Outer membrane
        float outer = max(capsule_mask(p0, center - axis * len, center + axis * len, 0.048, 0.04),
                          capsule_mask(p1, center - axis * len, center + axis * len, 0.048, 0.04));
        // Inner membrane with cristae — sinusoidal folding pattern
        float along = dot(p1 - center, axis);
        float cristae = 0.5 + 0.5 * sin(along * 48.0 + fi * 1.6 + phase * 2.0);
        float cristae2 = 0.5 + 0.5 * sin(along * 36.0 - fi * 2.3 + phase * 1.4);
        float cristae_mix = max(cristae, cristae2 * 0.6);
        // Matrix — denser central region
        float inner = capsule_mask(p1, center - axis * (len * 0.7), center + axis * (len * 0.7), 0.028, 0.035);
        vec3 outer_col = mix(vec3(0.74, 0.40, 0.16), vec3(0.96, 0.60, 0.22), cristae_mix);
        vec3 inner_col = mix(vec3(0.86, 0.52, 0.18), vec3(0.98, 0.70, 0.28), cristae_mix * 0.6);
        outer_col = mix(outer_col, vec3(0.48, 0.34, 0.22), smoothstep(0.35, 0.85, life_age));
        outer_col = mix(outer_col, vec3(0.26, 0.24, 0.22), corpse);
        inner_col = mix(inner_col, vec3(0.36, 0.30, 0.20), corpse);
        color += outer_col * outer * mix(0.16, 0.07, corpse);
        color += inner_col * inner * cristae_mix * mix(0.08, 0.03, corpse);
        density += outer * mix(0.08, 0.04, corpse) + inner * cristae_mix * 0.03;
    }
    return vec4(color, density);
}

vec4 organelle_er_and_golgi(vec3 p0, vec3 p1, vec3 p2, vec3 nucleus_center, vec3 nucleus_radii,
                             int morph, float phase, float life_age, float corpse) {
    vec3 color = vec3(0.0);
    float density = 0.0;

    // Rough endoplasmic reticulum — folded membrane sheets near nucleus, studded with ribosomes
    float nuc_dist = length(p1 - nucleus_center);
    float er_zone = smoothstep(0.50, 0.16, nuc_dist);  // strongest near nucleus
    // Wavy membrane pattern (rough ER cisternae)
    float er_wave = sin(p1.x * 18.0 + p1.z * 13.0 + phase * 1.4) *
                    sin(p1.y * 15.0 + p1.x * 9.0 - phase * 1.1);
    float rough_er = smoothstep(0.62, 0.92, 0.5 + 0.5 * er_wave) * er_zone;
    // Ribosome dots on rough ER
    float er_ribosomes = smoothstep(0.92, 0.99, hash3d(p1 * 42.0 + vec3(phase * 0.3))) * rough_er;
    vec3 er_col = mix(vec3(0.58, 0.48, 0.72), vec3(0.42, 0.32, 0.22), smoothstep(0.35, 0.88, life_age));
    er_col = mix(er_col, vec3(0.22, 0.22, 0.20), corpse);
    color += er_col * rough_er * 0.10;
    color += vec3(0.90, 0.84, 0.68) * er_ribosomes * 0.08;
    density += rough_er * mix(0.04, 0.02, corpse) + er_ribosomes * 0.02;

    // Smooth ER — tubular network further from nucleus (lipid synthesis)
    float smooth_zone = smoothstep(0.14, 0.42, nuc_dist) * (1.0 - smoothstep(0.50, 0.72, nuc_dist));
    float ser_tubes = sin(p1.x * 24.0 + p1.y * 11.0 + phase * 1.8) *
                      sin(p1.z * 20.0 - p1.x * 7.0 + phase * 0.9);
    float smooth_er = smoothstep(0.74, 0.96, 0.5 + 0.5 * ser_tubes) * smooth_zone;
    vec3 ser_col = mix(vec3(0.52, 0.62, 0.44), vec3(0.38, 0.32, 0.22), smoothstep(0.40, 0.90, life_age));
    ser_col = mix(ser_col, vec3(0.22, 0.20, 0.18), corpse);
    color += ser_col * smooth_er * 0.08;
    density += smooth_er * mix(0.03, 0.015, corpse);

    // Golgi apparatus — stacked cisternae (4-6 flattened discs) near nucleus
    vec3 golgi_base = nucleus_center + vec3(0.20, -0.04, 0.08);
    if (morph == 1) golgi_base = nucleus_center + vec3(0.18, -0.10, 0.05);
    if (morph == 2) golgi_base = nucleus_center + vec3(0.16, 0.02, -0.10);
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec3 stack_pos = golgi_base + vec3(0.0, fi * 0.028 - 0.056, 0.0);
        // Curved cisterna (slightly arc-shaped disc)
        float curve = sin(fi * 0.8 + phase * 0.4) * 0.02;
        stack_pos.x += curve;
        vec3 disc_radii = vec3(0.12, 0.010 + fi * 0.001, 0.08);
        float disc = max(ellipsoid_mask(p0, stack_pos, disc_radii, 0.025),
                         ellipsoid_mask(p1, stack_pos, disc_radii, 0.025));
        vec3 golgi_col = mix(vec3(0.82, 0.66, 0.28), vec3(0.56, 0.40, 0.22), smoothstep(0.30, 0.88, life_age));
        golgi_col = mix(golgi_col, vec3(0.22, 0.20, 0.18), corpse);
        // cis-face (near nucleus) lighter, trans-face darker
        float cis_trans = smoothstep(0.0, 4.0, fi);
        golgi_col = mix(golgi_col, golgi_col * 0.70, cis_trans);
        color += golgi_col * disc * 0.11;
        density += disc * mix(0.03, 0.015, corpse);
    }
    // Golgi vesicles budding off trans-face
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 bud = golgi_base + vec3(0.14 + fi * 0.04, fi * 0.02 - 0.06,
                                      sin(phase * 1.2 + fi * 2.0) * 0.05);
        float vesicle = sphere_mask(p1, bud, 0.018, 0.016);
        color += vec3(0.78, 0.60, 0.26) * vesicle * 0.10;
        density += vesicle * 0.02;
    }

    return vec4(color, density);
}

vec4 organelle_lysosomes_peroxisomes(vec3 p0, vec3 p1, vec3 p2, int morph,
                                      float phase, float noise, float life_age, float corpse) {
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

    // Lysosomes — 5 digestive vesicles with acidic interior
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase * 1.7 + fi * 1.3) * 0.26,
            sin(phase * 0.8 + fi * 2.7) * 0.18,
            cos(phase * 1.2 + fi * 1.9) * 0.24);
        float r = 0.038 + fi * 0.005;
        float lyso = max(sphere_mask(p1, center, r, 0.030),
                         sphere_mask(p2, center, r, 0.030));
        // Enzyme granularity inside lysosomes
        float enzyme = hash3d(p2 * 55.0 + center + vec3(fi)) * lyso;
        color += stage_b * lyso * mix(0.13, 0.08, life_age);
        color += vec3(0.96, 0.42, 0.28) * enzyme * 0.04;
        density += lyso * mix(0.03, 0.018, life_age);
    }

    // Peroxisomes — 4 smaller oxidative vesicles with crystalline core
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            cos(phase * 0.9 + fi * 1.5) * 0.20,
            sin(phase * 1.8 + fi * 1.2) * 0.15,
            cos(phase * 1.5 + fi * 2.5) * 0.18);
        float peroxi = max(sphere_mask(p0, center, 0.030, 0.024),
                           sphere_mask(p2, center, 0.030, 0.024));
        // Crystalline core (urate oxidase crystal)
        float crystal = sphere_mask(p2, center, 0.014, 0.012) * peroxi;
        color += stage_c * peroxi * 0.10;
        color += vec3(0.80, 0.88, 0.72) * crystal * 0.06;
        density += peroxi * mix(0.022, 0.012, life_age) + crystal * 0.01;
    }

    // Transport vesicles — 3 moving between organelles
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        vec3 center = vec3(
            sin(phase * 1.4 + fi * 2.4) * 0.24,
            cos(phase * 1.0 + fi * 1.6) * 0.15,
            sin(phase * 0.6 + fi * 2.8) * 0.22);
        vec3 axis = normalize(vec3(cos(fi + phase * 0.7), sin(fi * 1.7 - phase), cos(fi * 0.8 + phase * 1.4)));
        float body = max(capsule_mask(p0, center - axis * 0.05, center + axis * 0.05, 0.022, 0.025),
                         capsule_mask(p1, center - axis * 0.05, center + axis * 0.05, 0.022, 0.025));
        color += mix(vec3(0.82, 0.78, 0.36), vec3(0.50, 0.42, 0.22), life_age) * body * 0.08;
        density += body * mix(0.020, 0.010, life_age);
    }

    return vec4(color, density);
}

vec4 organelle_cytoskeleton(vec3 p0, vec3 p1, float phase, float life_age, float corpse) {
    vec3 color = vec3(0.0);
    float density = 0.0;
    // Microtubules — long fibers radiating from centrosome near nucleus
    vec3 centrosome = vec3(0.06, 0.04, 0.02);
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float a = fi * 1.2566 + phase * 0.2;  // 72-degree spacing
        vec3 end_pt = vec3(cos(a) * 0.42, sin(a + fi * 0.3) * 0.38,
                           cos(a * 1.3 + phase * 0.15) * 0.36);
        float mt = capsule_mask(p1, centrosome, end_pt, 0.005, 0.012);
        vec3 mt_col = mix(vec3(0.56, 0.72, 0.46), vec3(0.36, 0.40, 0.28), life_age);
        mt_col = mix(mt_col, vec3(0.20, 0.20, 0.18), corpse);
        color += mt_col * mt * 0.06;
        density += mt * 0.012;
    }
    // Actin cortex — mesh near membrane surface
    float cortex_dist = length(p0);
    float cortex = smoothstep(0.58, 0.72, cortex_dist) * (1.0 - smoothstep(0.74, 0.82, cortex_dist));
    float mesh = hash3d(p0 * 45.0 + vec3(phase * 0.2));
    cortex *= smoothstep(0.70, 0.90, mesh);
    vec3 actin_col = mix(vec3(0.44, 0.68, 0.52), vec3(0.30, 0.36, 0.24), life_age);
    actin_col = mix(actin_col, vec3(0.18, 0.18, 0.16), corpse);
    color += actin_col * cortex * 0.05;
    density += cortex * 0.015;

    return vec4(color, density);
}

// ── Cell interior compositing ────────────────────────────────────────────

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
    float mitosis_mix = smoothstep(0.02, 0.16, mitosis) *
                        (0.45 + 0.55 * smoothstep(0.16, 0.96, mitosis)) * (1.0 - corpse);

    vec3 cytoplasm = vec3(0.18, 0.28, 0.34);
    if (morph == 1) cytoplasm = vec3(0.26, 0.34, 0.24);
    else if (morph == 2) cytoplasm = vec3(0.16, 0.34, 0.28);

    vec3 young_cytoplasm = cytoplasm;
    vec3 mature_cytoplasm = mix(cytoplasm, cytoplasm + vec3(0.06, 0.05, 0.01), 0.5);
    vec3 senescent_cytoplasm = mix(cytoplasm, vec3(0.42, 0.34, 0.20), 0.75);
    vec3 dead_cytoplasm = vec3(0.20, 0.18, 0.16);
    vec3 aged_cytoplasm = mix(mix(young_cytoplasm, mature_cytoplasm, smoothstep(0.10, 0.35, life_age)),
                              senescent_cytoplasm, smoothstep(0.40, 0.82, life_age));
    vec3 cytoplasm_tint = mix(aged_cytoplasm, dead_cytoplasm, corpse);
    vec3 color = cytoplasm_tint * (0.20 + hash3d(p1 * 7.0 + vec3(phase)) * 0.10);
    float density = mix(0.16, 0.11, corpse);

    // Nucleus position and radii
    vec3 nucleus_center = vec3(0.12 * sin(phase * 1.1), 0.07 * cos(phase * 0.8), 0.08 * sin(phase * 1.6));
    if (morph == 1) nucleus_center += vec3(-0.05, -0.12, 0.02);
    else if (morph == 2) nucleus_center += vec3(0.10 * cos(phase * 0.7), 0.03, -0.05);
    vec3 nucleus_radii = vec3(0.26, 0.22 + (1.0 - aspect) * 0.03, 0.24);
    if (morph == 1) nucleus_radii = vec3(0.23, 0.18, 0.21);
    if (morph == 2) nucleus_radii = vec3(0.24, 0.20, 0.28);

    // Nucleus with envelope, nucleolus, chromatin
    vec4 nuc = organelle_nucleus(p1, p2, nucleus_center, nucleus_radii, phase, life_age, corpse);
    color += nuc.rgb; density += nuc.a;

    // Mitochondria with cristae and matrix
    vec4 mito = organelle_mitochondria(p0, p1, morph, phase, life_age, corpse);
    color += mito.rgb; density += mito.a;

    // ER (rough + smooth) and Golgi apparatus
    vec4 er_golgi = organelle_er_and_golgi(p0, p1, p2, nucleus_center, nucleus_radii,
                                            morph, phase, life_age, corpse);
    color += er_golgi.rgb; density += er_golgi.a;

    // Lysosomes, peroxisomes, transport vesicles
    vec4 lyso = organelle_lysosomes_peroxisomes(p0, p1, p2, morph, phase, noise, life_age, corpse);
    color += lyso.rgb; density += lyso.a;

    // Cytoskeleton — microtubules and actin cortex
    vec4 cyto = organelle_cytoskeleton(p0, p1, phase, life_age, corpse);
    color += cyto.rgb; density += cyto.a;

    // Free ribosomes scattered throughout cytoplasm
    float ribosomes = smoothstep(0.88, 0.995, hash3d(p0 * 30.0 + vec3(phase)));
    ribosomes += smoothstep(0.90, 0.997, hash3d(p1 * 36.0 - vec3(phase * 0.7)));
    ribosomes += smoothstep(0.93, 0.998, hash3d(p2 * 26.0 + vec3(phase * 0.4)));
    vec3 ribosome_col = mix(vec3(0.94, 0.88, 0.74), vec3(0.54, 0.46, 0.30), smoothstep(0.45, 0.90, life_age));
    ribosome_col = mix(ribosome_col, vec3(0.20, 0.20, 0.18), corpse);
    color += ribosome_col * ribosomes * 0.10;
    density += ribosomes * mix(0.03, 0.015, corpse);

    // Viral infection overlay
    if (infection_progress > 0.0 && corpse < 0.5) {
        vec4 viral_overlay = sample_viral_replication_overlay(
            p0, p1, p2, infection_progress, infection_load, infection_morphology, phase, infection_axis);
        float infection_mix = smoothstep(0.01, 0.22, infection_progress);
        color = mix(color, color * (0.88 - infection_mix * 0.18) + viral_overlay.rgb, infection_mix * 0.70);
        density += viral_overlay.a;
    }

    // Mitosis overlay
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
        float fission_mix = smoothstep(0.02, 0.16, division) *
                            (0.40 + 0.60 * smoothstep(0.14, 0.96, division));
        color = mix(color, color * 0.80 + fission_overlay.rgb, fission_mix * 0.84);
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

float structure_bound_scale(int shape) {
    if (shape == STRUCT_PETRI_RIM)
        return 1.50;
    if (shape == STRUCT_LUNG_BRANCH)
        return 1.40;
    if (shape == STRUCT_ALVEOLAR_CLUSTER)
        return 1.35;
    if (shape == STRUCT_POND_ROCK || shape == STRUCT_POND_REED)
        return 1.35;
    if (shape == STRUCT_BRAIN_FOLD)
        return 1.35;
    if (shape == STRUCT_PETRI_AGAR)
        return 1.30;
    if (shape == STRUCT_BRAIN_VESSEL)
        return 1.30;
    if (shape == STRUCT_BLOOD_WALL)
        return 1.40;
    if (shape == STRUCT_GUT_VILLUS || shape == STRUCT_GUT_CRYPT)
        return 1.30;
    if (shape == STRUCT_SOIL_GRAIN)
        return 1.30;
    if (shape == STRUCT_SOIL_ROOT)
        return 1.35;
    if (shape == STRUCT_WOUND_FIBRIN)
        return 1.25;
    if (shape == STRUCT_WOUND_TISSUE)
        return 1.35;
    if (shape == STRUCT_BLOOD_VALVE)
        return 1.25;
    return 1.20;
}

float sd_environment_structure_local(vec3 p, EnvFeature feature) {
    int shape = int(feature.meta.z + 0.5);
    float noise = feature.meta.y;
    float opacity = feature.meta.w;
    float detail_phase = noise * 6.2831853;

    if (shape == STRUCT_LUNG_BRANCH) {
        // Bronchiole — cartilaginous tube with dichotomous branching
        // Main trunk with tapering radius (proximal wider, distal narrow)
        float trunk_r = mix(0.18, 0.12, smoothstep(-0.88, 0.10, p.y));
        float trunk = sd_capsule(p, vec3(0.0, -0.88, 0.0), vec3(0.0, 0.10, 0.0), trunk_r);
        // Primary daughter branches at 35-degree angle (anatomically correct)
        float arm_a_r = mix(0.13, 0.08, smoothstep(0.0, 0.74, length(p - vec3(0.56, 0.74, 0.0))));
        float arm_a = sd_capsule(p, vec3(0.0, 0.02, 0.0), vec3(0.56, 0.74, 0.08), arm_a_r);
        float arm_b = sd_capsule(p, vec3(0.0, 0.02, 0.0), vec3(-0.56, 0.74, -0.08), arm_a_r);
        // Tertiary branches (smaller terminal bronchioles)
        float twig_a = sd_capsule(p, vec3(0.42, 0.54, 0.06), vec3(0.72, 0.92, 0.18), 0.055);
        float twig_b = sd_capsule(p, vec3(-0.38, 0.50, -0.06), vec3(-0.68, 0.88, -0.22), 0.050);
        float branch = min(trunk, min(min(arm_a, arm_b), min(twig_a, twig_b)));
        // Cartilage rings — C-shaped ridges along trunk (real bronchi have incomplete rings)
        float rings = sin(p.y * 18.0 + detail_phase) * 0.016 *
                      (1.0 - smoothstep(0.08, 0.22, abs(p.y - (-0.40))));
        // Mucosal texture on inner surface
        float mucosa = sin(p.x * 22.0 + p.z * 18.0 + detail_phase * 1.4) * 0.006;
        return branch - rings - mucosa;
    }
    if (shape == STRUCT_ALVEOLAR_CLUSTER) {
        // Alveolar sacs — grape-like cluster of thin-walled air spaces (~200 μm each)
        // Central alveolar duct
        float duct = sd_capsule(p, vec3(0.0, -0.38, 0.0), vec3(0.0, 0.10, 0.0), 0.08);
        // Individual alveoli — hollow spheres budding from duct
        float cluster = 1e5;
        for (int i = 0; i < 8; ++i) {
            float fi = float(i);
            float theta = fi * 2.39996 + detail_phase;
            float z = 1.0 - 2.0 * (fi + 0.5) / 8.0;
            float r_xy = sqrt(max(1.0 - z * z, 0.0));
            vec3 alv_center = vec3(cos(theta) * r_xy, z, sin(theta) * r_xy) * 0.38;
            float alv_r = 0.22 + sin(fi * 1.7 + detail_phase) * 0.04;
            // Hollow alveolus — thin wall
            float outer = sd_sphere(p - alv_center, alv_r);
            float inner = sd_sphere(p - alv_center, alv_r - 0.04);
            float alveolus = max(outer, -inner);
            cluster = min(cluster, alveolus);
        }
        float result = min(duct, cluster);
        // Capillary network texture on alveolar walls
        float capillaries = sin(dot(p, vec3(14.0, 11.0, 17.0)) + detail_phase) *
                            sin(dot(p, vec3(-9.0, 16.0, 8.0)) - detail_phase * 0.7) * 0.012;
        return result - capillaries;
    }
    if (shape == STRUCT_POND_REED) {
        // Massive plant stem — towering column at microscale
        // At cell scale, a reed stem is like a building-sized pillar
        // Main stem — thick column (cm-scale, enormous relative to cells)
        float stem = sd_capsule(p, vec3(0.0, -0.95, 0.0), vec3(0.0, 0.95, 0.0), 0.22);
        // Cell wall texture (plant cells ~30-100 μm, visible as rectangular pattern)
        float cell_wall = sin(p.y * 8.0 + detail_phase) * 0.008
                        + sin(p.y * 22.0 + p.x * 18.0 + detail_phase * 1.3) * 0.004;
        // Vascular bundles running along stem (xylem/phloem)
        float vascular = sin(atan(p.x, p.z) * 6.0 + detail_phase * 0.5) * 0.012;
        // Epidermal ridges along the stem surface
        float ridges = sin(atan(p.x, p.z) * 12.0) * 0.006;
        return stem - cell_wall - vascular - ridges;
    }
    if (shape == STRUCT_POND_ROCK) {
        // Massive submerged rock — terrain-scale ground surface
        // At microscale, rock is an enormous boulder/ground plane
        vec3 q = p;
        q.y *= 1.15; // slightly flattened
        float rock = sd_ellipsoid(q, vec3(0.98, 0.55, 0.92));
        // Broad erosion channels (weathering grooves in rock surface)
        float erosion = sin(p.x * 3.0 + detail_phase) * sin(p.z * 2.5 - detail_phase * 0.8) * 0.06;
        // Micro-pitting (mineral grain texture visible at cell scale)
        float pits = sin(p.x * 18.0 + p.y * 14.0 + detail_phase * 1.3) *
                     sin(p.z * 16.0 - p.x * 12.0 + detail_phase * 0.6) * 0.015;
        // Sediment layer on top
        float sediment = sd_ellipsoid(q + vec3(0.0, -0.18, 0.0), vec3(0.88, 0.14, 0.82));
        rock = min(rock, sediment);
        // Biofilm coating (thin organic layer)
        float biofilm = sin(p.x * 8.0 + p.z * 6.0 + detail_phase * 0.4) * 0.008;
        return rock - erosion - max(pits, 0.0) - biofilm;
    }
    if (shape == STRUCT_PETRI_RIM) {
        // Petri dish wall — at bacterial scale this is a massive glass cliff
        // 90mm dish at 0.625 μm/unit → rim is impossibly far, but we render it
        // as the world boundary wall (thick glass enclosure)
        vec3 q = p;
        q.y *= 1.20;
        // Massive glass wall ring (thick torus forming the enclosure)
        float rim = sd_torus(q, vec2(1.02, 0.16));
        // Flat glass base (floor)
        float base_disk = max(sd_sphere(q + vec3(0.0, 0.20, 0.0), 1.12),
                              -(sd_sphere(q + vec3(0.0, 0.20, 0.0), 1.06)));
        base_disk = max(base_disk, q.y + 0.12);
        base_disk = max(base_disk, -(q.y + 0.35));
        // Glass lid above
        float lid = sd_torus(q + vec3(0.0, -0.18, 0.0), vec2(1.06, 0.10));
        return min(min(rim, lid), base_disk);
    }
    if (shape == STRUCT_PETRI_AGAR) {
        // Agar gel — massive flat floor plane at bacterial scale
        // The agar surface extends far beyond what bacteria can see
        vec3 q = p;
        q.y += 0.35;
        // Flat slab — very wide, not very tall (floor-like)
        float slab = sd_ellipsoid(q, vec3(1.02, 0.18, 1.02));
        // Surface micro-texture (agar gel pores: 0.1-2.7 μm, sub-cell scale)
        float surface_tex = sin(q.x * 28.0 + detail_phase) * sin(q.z * 24.0 - detail_phase * 0.6) * 0.005;
        // Streak plate pattern (inoculation lines scored into surface)
        float streak = sin(q.x * 4.0 + q.z * 2.5 + detail_phase) * 0.012;
        // Colony growth dimples (bacterial colonies growing on surface)
        float colonies = 0.0;
        for (int i = 0; i < 5; ++i) {
            float fi = float(i);
            vec3 col_pos = vec3(sin(fi * 2.3 + detail_phase) * 0.55,
                                -0.12,
                                cos(fi * 1.9 + detail_phase * 0.7) * 0.50);
            colonies = max(colonies, -sd_sphere(q - col_pos, 0.04 + fi * 0.008));
        }
        return slab - surface_tex - streak + colonies * 0.25;
    }
    if (shape == STRUCT_BRAIN_FOLD) {
        // Massive cerebral cortex gyrus wall — terrain-scale tissue boundary
        // At cell scale, grey matter is an enormous undulating wall/cliff
        vec3 q = p;
        // Gentle gyral undulation at macro scale
        q.y += sin(p.z * 2.5 + detail_phase) * 0.14
             + sin(p.z * 4.5 - detail_phase * 0.4) * 0.05;
        q.x += sin(p.z * 2.0 - detail_phase * 0.6) * 0.08
             + cos(p.z * 3.5 + detail_phase * 0.3) * 0.03;
        // Elongated wall shape (tall and wide, like a cliff face)
        float fold = sd_ellipsoid(q, vec3(0.85, 0.70, 0.90));
        // Primary sulcus — deep groove carved into the tissue wall
        float sulcus = sd_capsule(q, vec3(0.0, -0.20, -0.85), vec3(0.0, 0.30, 0.85), 0.14);
        // Secondary sulcus (perpendicular, shallower)
        float sulcus2 = sd_capsule(q, vec3(-0.65, 0.10, 0.0), vec3(0.65, 0.15, 0.0), 0.09);
        float grooved = max(fold, -min(sulcus, sulcus2));
        // Cell-scale surface texture (neuronal soma bumps, ~20 μm)
        float cell_tex = sin(p.x * 12.0 + p.y * 10.0 + detail_phase) *
                         sin(p.z * 14.0 - detail_phase * 0.5) * 0.006;
        return grooved - cell_tex;
    }
    if (shape == STRUCT_BRAIN_VESSEL) {
        // Cerebral blood vessel — branching arteriole with bifurcation
        // Main vessel (arteriole, ~50 μm diameter)
        float trunk = sd_capsule(p, vec3(0.0, -0.76, -0.20), vec3(0.0, 0.78, 0.20), 0.09);
        // Primary bifurcation
        float branch_a = sd_capsule(p, vec3(0.0, 0.18, 0.04), vec3(0.44, 0.62, 0.36), 0.065);
        float branch_b = sd_capsule(p, vec3(0.0, 0.18, 0.04), vec3(-0.32, 0.54, -0.28), 0.058);
        // Terminal capillary branches
        float cap_a = sd_capsule(p, vec3(0.34, 0.50, 0.28), vec3(0.58, 0.78, 0.52), 0.035);
        float cap_b = sd_capsule(p, vec3(-0.24, 0.42, -0.20), vec3(-0.50, 0.68, -0.44), 0.032);
        float vessels = min(trunk, min(min(branch_a, branch_b), min(cap_a, cap_b)));
        // Vessel wall texture (smooth muscle wrapping)
        float wall = sin(p.y * 26.0 + p.z * 8.0 + detail_phase) * 0.005;
        return vessels - wall;
    }

    if (shape == STRUCT_GUT_VILLUS) {
        // Intestinal villus — finger-like projection with brush border microvilli
        float stem = sd_capsule(p, vec3(0.0, -0.80, 0.0), vec3(0.0, 0.72, 0.0), 0.16);
        float tip = sd_sphere(p - vec3(0.0, 0.72, 0.0), 0.20);
        float body = min(stem, tip);
        // Brush border microvilli texture (dense surface projections)
        float microvilli = sin(p.y * 30.0 + detail_phase) * sin(atan(p.x, p.z) * 14.0 + detail_phase * 0.6) * 0.008;
        // Epithelial cell boundaries (columnar cells ~20 μm)
        float cell_boundary = sin(p.y * 12.0 + detail_phase * 0.8) * 0.004
                            + sin(atan(p.x, p.z) * 8.0 + detail_phase * 1.2) * 0.003;
        // Goblet cell indentations (mucus-secreting cells interspersed)
        float goblet = sin(p.y * 6.0 + detail_phase * 1.5) * sin(atan(p.x, p.z) * 4.0) * 0.010;
        return body - microvilli - cell_boundary - max(goblet, 0.0) * 0.4;
    }
    if (shape == STRUCT_GUT_CRYPT) {
        // Crypt of Lieberkuhn — tubular gland invagination
        float outer = sd_capsule(p, vec3(0.0, -0.85, 0.0), vec3(0.0, 0.10, 0.0), 0.20);
        float inner = sd_capsule(p, vec3(0.0, -0.75, 0.0), vec3(0.0, 0.15, 0.0), 0.13);
        float crypt = max(outer, -inner);
        // Crypt epithelial cells (stem cells at base, differentiated at top)
        float cells = sin(p.y * 16.0 + detail_phase) * sin(atan(p.x, p.z) * 10.0 + detail_phase * 0.7) * 0.005;
        // Paneth cell granules at crypt base
        float paneth = smoothstep(-0.85, -0.55, p.y) * (1.0 - smoothstep(-0.55, -0.20, p.y));
        float granules = sin(atan(p.x, p.z) * 6.0 + detail_phase * 1.8) * paneth * 0.008;
        return crypt - cells - granules;
    }
    if (shape == STRUCT_BLOOD_WALL) {
        // Blood vessel endothelium — curved tube wall
        vec3 q = p;
        q.y *= 1.10;
        float wall = sd_ellipsoid(q, vec3(0.95, 0.85, 0.95));
        float lumen = sd_ellipsoid(q, vec3(0.82, 0.72, 0.82));
        float vessel = max(wall, -lumen);
        // Endothelial cell junctions (cobblestone pattern)
        float junctions = sin(atan(q.x, q.z) * 16.0 + detail_phase) *
                          sin(q.y * 14.0 + detail_phase * 0.8) * 0.004;
        // Glycocalyx brush border on inner surface
        float glycocalyx = sin(atan(q.x, q.z) * 28.0 + q.y * 22.0 + detail_phase * 1.3) * 0.003;
        // Smooth muscle layer wrapping (tunica media)
        float muscle = sin(atan(q.x, q.z) * 6.0 + detail_phase * 0.4) * 0.008;
        return vessel - junctions - glycocalyx - muscle;
    }
    if (shape == STRUCT_BLOOD_VALVE) {
        // Venous valve — thin leaflet cusps
        float cusp_a = sd_ellipsoid(p - vec3(0.15, 0.0, 0.0), vec3(0.06, 0.35, 0.28));
        float cusp_b = sd_ellipsoid(p + vec3(0.15, 0.0, 0.0), vec3(0.06, 0.35, 0.28));
        float valve = min(cusp_a, cusp_b);
        // Endothelial covering on valve surface
        float endothelium = sin(p.y * 20.0 + p.z * 16.0 + detail_phase) * 0.003;
        return valve - endothelium;
    }
    if (shape == STRUCT_SOIL_GRAIN) {
        // Soil mineral grain — weathered irregular boulder
        vec3 q = p;
        q.x += sin(p.y * 3.0 + detail_phase) * 0.08;
        q.z += cos(p.y * 2.5 - detail_phase * 0.6) * 0.06;
        float grain = sd_ellipsoid(q, vec3(0.82, 0.65, 0.76));
        // Mineral crystal facets
        float facets = sin(q.x * 6.0 + q.y * 5.0 + detail_phase) *
                       sin(q.z * 7.0 - detail_phase * 0.5) * 0.025;
        // Micro-pitting from weathering
        float pits = sin(q.x * 22.0 + q.z * 18.0 + detail_phase * 1.4) *
                     sin(q.y * 20.0 - q.x * 14.0 + detail_phase * 0.8) * 0.010;
        // Biofilm coating on exposed surfaces
        float biofilm = sin(q.x * 10.0 + q.z * 8.0 + detail_phase * 0.3) * 0.006;
        return grain - facets - max(pits, 0.0) - biofilm;
    }
    if (shape == STRUCT_SOIL_ROOT) {
        // Plant root — branching structure with root hairs
        float main_root = sd_capsule(p, vec3(0.0, -0.90, 0.0), vec3(0.0, 0.85, 0.0), 0.12);
        // Lateral roots branching off
        float lat_a = sd_capsule(p, vec3(0.0, -0.20, 0.0), vec3(0.50, 0.30, 0.25), 0.06);
        float lat_b = sd_capsule(p, vec3(0.0, 0.15, 0.0), vec3(-0.40, 0.55, -0.30), 0.05);
        float tip_a = sd_capsule(p, vec3(0.40, 0.22, 0.20), vec3(0.65, 0.42, 0.38), 0.035);
        float roots = min(main_root, min(min(lat_a, lat_b), tip_a));
        // Root epidermis cell texture
        float cells = sin(p.y * 14.0 + detail_phase) * 0.005
                    + sin(atan(p.x, p.z) * 10.0 + detail_phase * 0.9) * 0.004;
        // Root cap at growing tips (slightly bulbous)
        float root_cap = sd_sphere(p - vec3(0.0, -0.92, 0.0), 0.15);
        roots = min(roots, root_cap);
        return roots - cells;
    }
    if (shape == STRUCT_WOUND_FIBRIN) {
        // Fibrin mesh — criss-crossing strands forming clot scaffold
        float strand_a = sd_capsule(p, vec3(-0.70, -0.30, -0.20), vec3(0.65, 0.40, 0.30), 0.04);
        float strand_b = sd_capsule(p, vec3(0.20, -0.50, -0.60), vec3(-0.30, 0.55, 0.50), 0.035);
        float strand_c = sd_capsule(p, vec3(-0.50, 0.10, -0.45), vec3(0.55, -0.15, 0.55), 0.038);
        float strand_d = sd_capsule(p, vec3(-0.10, -0.60, 0.30), vec3(0.25, 0.65, -0.20), 0.032);
        float strand_e = sd_capsule(p, vec3(0.50, -0.40, 0.10), vec3(-0.45, 0.50, -0.15), 0.036);
        float mesh = min(strand_a, min(min(strand_b, strand_c), min(strand_d, strand_e)));
        // Fibrin cross-linking texture (D-D bonds)
        float cross_links = sin(dot(p, vec3(18.0, 14.0, 16.0)) + detail_phase) * 0.003;
        // Platelet aggregates trapped in mesh
        float platelet_a = sd_sphere(p - vec3(0.10, 0.05, -0.08), 0.06);
        float platelet_b = sd_sphere(p - vec3(-0.25, -0.15, 0.20), 0.05);
        mesh = min(mesh, min(platelet_a, platelet_b));
        return mesh - cross_links;
    }
    if (shape == STRUCT_WOUND_TISSUE) {
        // Wound edge — ragged tissue boundary with inflammatory damage
        vec3 q = p;
        q.y += sin(p.x * 3.5 + detail_phase) * 0.12 + sin(p.z * 2.8 - detail_phase * 0.7) * 0.08;
        float tissue = sd_ellipsoid(q, vec3(0.90, 0.60, 0.85));
        // Torn edge — irregular cavity from tissue damage
        float cavity = sd_ellipsoid(q + vec3(0.15, -0.10, 0.0), vec3(0.55, 0.40, 0.50));
        float wound = max(tissue, -cavity);
        // Exposed collagen fibers on wound surface
        float collagen = sin(q.x * 8.0 + q.z * 6.0 + detail_phase * 0.5) *
                         sin(q.y * 10.0 - detail_phase * 0.3) * 0.008;
        // Cellular debris texture
        float debris = sin(q.x * 20.0 + q.y * 18.0 + detail_phase * 1.2) *
                       sin(q.z * 16.0 + detail_phase * 0.6) * 0.005;
        return wound - collagen - debris;
    }

    return sd_sphere(p, 0.80);
}

vec3 structure_normal_local(vec3 p, EnvFeature feature) {
    vec2 e = vec2(0.0035, 0.0);
    return normalize(vec3(
        sd_environment_structure_local(p + vec3(e.x, e.y, e.y), feature) - sd_environment_structure_local(p - vec3(e.x, e.y, e.y), feature),
        sd_environment_structure_local(p + vec3(e.y, e.x, e.y), feature) - sd_environment_structure_local(p - vec3(e.y, e.x, e.y), feature),
        sd_environment_structure_local(p + vec3(e.y, e.y, e.x), feature) - sd_environment_structure_local(p - vec3(e.y, e.y, e.x), feature)
    ));
}

bool refine_structure_hit(vec3 ro, vec3 rd, EnvFeature feature, float bound_t,
                          out float refined_t, out vec3 refined_pos, out vec3 refined_normal,
                          out vec3 local_surface) {
    mat3 basis = basis_from_axis(feature.axis_strength.xyz);
    float radius = max(feature.pos_radius.w, 0.001);
    float bound_radius = radius * structure_bound_scale(int(feature.meta.z + 0.5));
    float start_t = max(bound_t - bound_radius * 1.15, 0.0);
    float end_t = bound_t + bound_radius * 1.25;
    float t = start_t;

    for (int step = 0; step < 64; ++step) {
        vec3 pos = ro + rd * t;
        vec3 local = transpose(basis) * ((pos - feature.pos_radius.xyz) / radius);
        float dist = sd_environment_structure_local(local, feature) * radius;
        if (dist < radius * 0.003) {
            vec3 normal_local = structure_normal_local(local, feature);
            refined_t = t;
            refined_pos = pos;
            refined_normal = normalize(basis * normal_local);
            local_surface = local;
            return true;
        }
        t += max(dist * 0.68, radius * 0.008);
        if (t > end_t)
            break;
    }
    return false;
}

bool trace_environment_structure(vec3 ro, vec3 rd, float max_t, int feature_count,
                                 out float closest_t, out int closest_idx,
                                 out vec3 hit_pos, out vec3 hit_normal, out vec3 local_hit) {
    closest_t = max_t;
    closest_idx = -1;
    hit_pos = vec3(0.0);
    hit_normal = vec3(0.0, 1.0, 0.0);
    local_hit = vec3(0.0);

    for (int i = 0; i < feature_count; ++i) {
        if (abs(features[i].tint_type.a - FEATURE_STRUCTURE) > 0.1)
            continue;
        float bound_radius = features[i].pos_radius.w * structure_bound_scale(int(features[i].meta.z + 0.5));
        float t = intersect_sphere(ro, rd, features[i].pos_radius.xyz, bound_radius);
        if (t < 0.0 || t >= closest_t)
            continue;

        float refined_t;
        vec3 refined_pos;
        vec3 refined_normal;
        vec3 refined_local;
        if (refine_structure_hit(ro, rd, features[i], t, refined_t, refined_pos, refined_normal, refined_local) &&
            refined_t < closest_t) {
            closest_t = refined_t;
            closest_idx = i;
            hit_pos = refined_pos;
            hit_normal = refined_normal;
            local_hit = refined_local;
        }
    }

    return closest_idx >= 0;
}

vec3 shade_environment_structure(EnvFeature feature, vec3 pos, vec3 normal, vec3 local_pos,
                                 vec3 view_dir, vec3 env_tint, vec3 bg, vec4 media, float ambient, float time) {
    int shape = int(feature.meta.z + 0.5);
    float opacity = clamp(feature.meta.w, 0.0, 1.0);
    vec3 base = feature.tint_type.rgb;
    vec3 key_dir = normalize(vec3(0.6, 0.8, 0.4));
    vec3 fill_dir = normalize(vec3(-0.4, -0.3, 0.6));
    float key = max(dot(normal, key_dir), 0.0);
    float fill = max(dot(normal, fill_dir), 0.0);
    float rim = pow(1.0 - max(dot(normal, view_dir), 0.0), 2.0);
    float detail = 0.5 + 0.5 * sin(dot(local_pos, vec3(11.0, 9.0, 7.0)) + feature.meta.y * 6.2831853 + time * 0.22);
    vec3 color = base;

    if (shape == STRUCT_LUNG_BRANCH) {
        // Bronchiole: pink mucosa with cartilage ring highlights
        float ring_band = 0.5 + 0.5 * sin(local_pos.y * 18.0 + time * 0.1);
        color = mix(base, vec3(0.88, 0.62, 0.56), detail * 0.40);
        color = mix(color, vec3(0.92, 0.82, 0.76), ring_band * 0.18); // cartilage
    } else if (shape == STRUCT_ALVEOLAR_CLUSTER) {
        // Alveoli: thin translucent walls with capillary network (red-pink)
        float capillary = 0.5 + 0.5 * sin(dot(local_pos, vec3(14.0, 11.0, 17.0)) + time * 0.2);
        color = mix(base, vec3(0.90, 0.68, 0.62), detail * 0.35);
        color = mix(color, vec3(0.82, 0.28, 0.24), capillary * 0.22); // blood vessels
    } else if (shape == STRUCT_POND_REED) {
        // Massive plant stem surface — chlorophyll-green with cell wall pattern
        float cell_walls = smoothstep(0.55, 0.75, abs(sin(local_pos.y * 8.0 + local_pos.x * 6.0)));
        float vascular = smoothstep(0.6, 0.8, abs(sin(atan(local_pos.x, local_pos.z) * 6.0)));
        color = mix(base, vec3(0.28, 0.52, 0.14), detail * 0.50);
        color = mix(color, vec3(0.18, 0.40, 0.08), cell_walls * 0.18); // cell walls
        color = mix(color, vec3(0.14, 0.32, 0.06), vascular * 0.22); // vascular bundles
    } else if (shape == STRUCT_POND_ROCK) {
        // Submerged rock: grey-brown with algae biofilm on top
        float algae = smoothstep(-0.10, 0.20, -local_pos.y); // green on top
        color = mix(base, vec3(0.38, 0.34, 0.26), detail * 0.28);
        color = mix(color, vec3(0.22, 0.42, 0.16), algae * 0.35); // biofilm
    } else if (shape == STRUCT_PETRI_RIM) {
        // Glass: clear with slight blue-white tint and specular
        color = mix(base, vec3(0.72, 0.74, 0.78), detail * 0.20);
        color += vec3(0.04, 0.06, 0.08) * rim; // glass reflection
    } else if (shape == STRUCT_PETRI_AGAR) {
        // Agar: amber-yellow translucent gel with streak marks
        float streaks = 0.5 + 0.5 * sin(local_pos.x * 5.0 + local_pos.z * 3.0);
        color = mix(base, vec3(0.72, 0.64, 0.32), detail * 0.30);
        color = mix(color, vec3(0.80, 0.72, 0.40), streaks * 0.15);
    } else if (shape == STRUCT_BRAIN_FOLD) {
        // Cortex: grey matter (outer) with pink-white inner matter
        // Depth-dependent: surface is grey, deeper is whiter (white matter)
        float depth = smoothstep(0.0, 0.5, length(local_pos.xz));
        color = mix(vec3(0.62, 0.56, 0.52), vec3(0.48, 0.42, 0.50), depth); // grey to white
        color = mix(color, base, 0.25);
        // Sulcus darkening (in the grooves)
        float sulcus_dark = smoothstep(0.0, 0.12, abs(local_pos.x)) * 0.15;
        color -= vec3(sulcus_dark);
    } else if (shape == STRUCT_BRAIN_VESSEL) {
        // Blood vessel: dark red with smooth muscle wall
        float vessel_stripe = 0.5 + 0.5 * sin(local_pos.y * 26.0 + local_pos.z * 8.0);
        color = mix(base, vec3(0.72, 0.16, 0.14), detail * 0.35);
        color = mix(color, vec3(0.84, 0.26, 0.20), vessel_stripe * 0.12); // endothelium
    } else if (shape == STRUCT_GUT_VILLUS) {
        // Villus: pink-salmon mucosa with brush border highlights
        float brush = 0.5 + 0.5 * sin(local_pos.y * 30.0 + atan(local_pos.x, local_pos.z) * 14.0);
        color = mix(base, vec3(0.85, 0.58, 0.52), detail * 0.40);
        color = mix(color, vec3(0.92, 0.72, 0.66), brush * 0.15); // microvilli
        // Goblet cells (mucus secreting — lighter patches)
        float goblet = smoothstep(0.7, 0.9, sin(local_pos.y * 6.0 + atan(local_pos.x, local_pos.z) * 4.0));
        color = mix(color, vec3(0.90, 0.88, 0.80), goblet * 0.20);
    } else if (shape == STRUCT_GUT_CRYPT) {
        // Crypt: darker than villi, stem cell niche at base
        float depth_gradient = smoothstep(-0.85, 0.10, local_pos.y);
        color = mix(vec3(0.52, 0.36, 0.32), vec3(0.78, 0.54, 0.48), depth_gradient);
        color = mix(color, base, 0.25);
        // Paneth cell granules at base (yellowish)
        float paneth = (1.0 - smoothstep(-0.85, -0.40, local_pos.y));
        color = mix(color, vec3(0.75, 0.68, 0.42), paneth * 0.25);
    } else if (shape == STRUCT_BLOOD_WALL) {
        // Vessel wall: layered — endothelium (inner), smooth muscle (middle), adventitia (outer)
        float radial = length(local_pos.xz);
        float layer = smoothstep(0.72, 0.95, radial); // inner to outer
        color = mix(vec3(0.80, 0.24, 0.20), vec3(0.55, 0.18, 0.16), layer); // endothelium to muscle
        color = mix(color, base, 0.20);
        // Endothelial junctions
        float junctions = smoothstep(0.6, 0.8, abs(sin(atan(local_pos.x, local_pos.z) * 16.0)));
        color = mix(color, vec3(0.72, 0.30, 0.26), junctions * 0.12);
    } else if (shape == STRUCT_BLOOD_VALVE) {
        // Valve leaflet: smooth pale tissue
        color = mix(base, vec3(0.78, 0.38, 0.34), detail * 0.30);
        color += vec3(0.04, 0.02, 0.02) * rim; // slight sheen
    } else if (shape == STRUCT_SOIL_GRAIN) {
        // Mineral grain: brown-grey with crystal facets and biofilm
        float facet = 0.5 + 0.5 * sin(dot(local_pos, vec3(6.0, 5.0, 7.0)));
        color = mix(base, vec3(0.48, 0.42, 0.32), detail * 0.35);
        color = mix(color, vec3(0.56, 0.50, 0.38), facet * 0.18); // crystal
        // Biofilm patches (greenish on exposed surfaces)
        float bio = smoothstep(-0.10, 0.15, -local_pos.y);
        color = mix(color, vec3(0.24, 0.38, 0.18), bio * 0.25);
    } else if (shape == STRUCT_SOIL_ROOT) {
        // Plant root: brown-white with cortex/epidermis layers
        float depth_root = smoothstep(0.0, 0.12, abs(length(local_pos.xz) - 0.10));
        color = mix(vec3(0.72, 0.62, 0.48), vec3(0.42, 0.34, 0.22), depth_root);
        color = mix(color, base, 0.20);
        // Root cap at tips (slightly greenish)
        float tip = smoothstep(-0.95, -0.85, local_pos.y);
        color = mix(color, vec3(0.52, 0.58, 0.38), tip * 0.30);
    } else if (shape == STRUCT_WOUND_FIBRIN) {
        // Fibrin mesh: pale golden-yellow strands
        color = mix(base, vec3(0.82, 0.74, 0.52), detail * 0.40);
        // Platelet aggregates (slightly darker reddish spots)
        float platelet_zone = smoothstep(0.05, 0.08, sd_sphere(local_pos - vec3(0.10, 0.05, -0.08), 0.08));
        color = mix(vec3(0.72, 0.32, 0.28), color, platelet_zone);
    } else if (shape == STRUCT_WOUND_TISSUE) {
        // Wound edge: inflamed reddish tissue with exposed collagen (white)
        float inflammation = 0.5 + 0.5 * sin(dot(local_pos, vec3(3.5, 4.0, 2.8)) + time * 0.15);
        color = mix(base, vec3(0.72, 0.28, 0.24), detail * 0.35);
        color = mix(color, vec3(0.85, 0.40, 0.32), inflammation * 0.20); // inflammation
        // Exposed collagen (white fibers at wound edge)
        float edge = 1.0 - smoothstep(0.35, 0.60, length(local_pos.xz));
        color = mix(color, vec3(0.88, 0.86, 0.82), edge * 0.25);
    }

    vec3 lit = color * (ambient * 0.90 + key * 0.70 + fill * 0.24);
    lit += subsurface(normal, key_dir, view_dir, color, feature.pos_radius.w * 0.8) * 0.45;
    lit += color * rim * (0.12 + opacity * 0.10);
    lit = mix(bg + media.rgb * 0.28, lit, clamp(0.42 + opacity * 0.48, 0.28, 0.92));
    return lit;
}

vec4 trace_environment_media(vec3 ro, vec3 rd, float max_t, int feature_count) {
    vec3 accum = vec3(0.0);
    float density = 0.0;
    float limit = max(max_t, 350.0);

    for (int i = 0; i < feature_count; ++i) {
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
        else if (type > 2.5 && type < 3.5) gain = 0.09;
        else if (type > 3.5) gain = 0.05 * features[i].meta.w;

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
    float antibiotic_visibility = lighting_params.y;
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

    for (int i = 0; i < entity_count; i++) {
        float t = intersect_sphere(ro, rd,
            spheres[i].pos_radius.xyz,
            spheres[i].pos_radius.w);
        if (t > 0.0 && t < closest_t) {
            closest_t = t;
            closest_idx = i;
        }
    }

    float structure_t;
    int structure_idx;
    vec3 structure_pos;
    vec3 structure_normal;
    vec3 structure_local;
    bool has_structure_hit = trace_environment_structure(ro, rd, closest_t, feature_count,
                                                         structure_t, structure_idx,
                                                         structure_pos, structure_normal, structure_local);

    // ── No hit — background ────────────────────────────────────────────────
    if (closest_idx < 0 && !has_structure_hit) {
        vec4 media = trace_environment_media(ro, rd, 1200.0, feature_count);
        outColor = vec4(background(rd, time, env_tint, oxygen, nutrients, acidity, toxicity) + media.rgb, 1.0);
        return;
    }

    vec3 bg = background(rd, time, env_tint, oxygen, nutrients, acidity, toxicity);
    if (has_structure_hit && (closest_idx < 0 || structure_t < closest_t)) {
        EnvFeature structure = features[structure_idx];
        vec4 media = trace_environment_media(ro, rd, structure_t, feature_count);
        vec3 structure_color = shade_environment_structure(structure, structure_pos, structure_normal,
                                                          structure_local, -rd, env_tint, bg, media, ambient, time);
        float haze = 1.0 - exp(-structure_t * haze_density);
        haze *= 0.28 + nutrients * 0.08 + toxicity * 0.28;
        structure_color += media.rgb;
        structure_color = mix(structure_color, bg + media.rgb * 0.26, clamp(haze + media.a * 0.18, 0.0, 0.62));
        outColor = vec4(pow(max(structure_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
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
    float bacterial_infection_progress = hit.gene_params.w;
    mat3 hit_basis = basis_from_axis(hit.axis_morph.xyz);
    vec3 infection_axis_local = transpose(hit_basis) * hit.aux_params.xyz;
    infection_axis_local = length(infection_axis_local) > 1e-4
        ? normalize(infection_axis_local)
        : vec3(1.0, 0.0, 0.0);
    float bacterial_infection_load = hit.aux_params.w;
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
        else if (morphology == 3) base_color = mix(base_color, vec3(0.88, 0.52, 0.20), 0.36);
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
            translucency *= 0.30;
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
            vec3 film_color = contrast_color(spectrum_color(antibiotic_type, antibiotic_diversity),
                                             env_tint * 0.70 + vec3(0.08, 0.10, 0.12));
            float film_shell = 1.0 - smoothstep(-0.12, 0.18 + antibiotic_film * 0.22, bacteria_core_sdf);
            float cloud_noise = 0.5 + 0.5 * sin(dot(surface_pt, vec3(19.0, 15.0, 13.0)) + phase * 2.1);
            float iridescence = 0.5 + 0.5 * sin(surface_pt.z * 22.0 + phase * 2.0 + antibiotic_diversity * 4.0);
            if (bacteria_cloud_hit) {
                vec3 cloud_color = mix(film_color * (0.44 + antibiotic_yield * 0.08),
                                       film_color * (0.74 + cloud_noise * 0.14) + vec3(0.06, 0.08, 0.10),
                                       iridescence * 0.45);
                final_color = mix(final_color, cloud_color, 0.64);
                final_color += film_color * cloud_noise * antibiotic_film * antibiotic_visibility * 0.24;
            } else {
                final_color = mix(final_color, final_color * 0.84 + film_color * (0.74 + antibiotic_yield * 0.12),
                                  film_shell * antibiotic_film * antibiotic_visibility * 0.82);
                final_color += film_color * film_shell * antibiotic_film * antibiotic_visibility *
                               (0.14 + 0.12 * iridescence);
            }
        }
    } else if (entity_type == 8) {
        float vacuole = smoothstep(0.64, 0.98, hash3d(surface_pt * 13.0 + vec3(phase * 0.5)));
        final_color += vec3(0.08, 0.16, 0.10) * vacuole * 0.10;
    } else if (entity_type == 2 && morphology == 2) {
        float tail_stripe = 0.55 + 0.45 * sin(surface_pt.z * 34.0 - phase * 2.0);
        final_color *= mix(0.86, 1.08, tail_stripe);
    }

    if (entity_type == 0 && corpse < 0.5) {
        if (infection_progress > 0.0) {
            float lesion = 0.5 + 0.5 * sin(surface_pt.x * 16.0 + surface_pt.y * 19.0 + phase * 2.4);
            final_color = mix(final_color, final_color * vec3(0.92, 0.78, 0.78) + vec3(0.36, 0.06, 0.08) * lesion,
                              smoothstep(0.08, 0.95, infection_progress) * 0.26);
            final_color += vec3(0.88, 0.16, 0.14) * rim * infection_progress * 0.12;
        }
        if (bacterial_infection_progress > 0.0) {
            float biofilm = 0.5 + 0.5 * sin(surface_pt.x * 22.0 - surface_pt.z * 18.0 + phase * 1.6);
            float lesion = smoothstep(0.04, 0.92, bacterial_infection_progress);
            vec3 lesion_col = mix(vec3(0.84, 0.72, 0.18), vec3(0.28, 0.72, 0.18), biofilm);
            final_color = mix(final_color, final_color * 0.78 + lesion_col, lesion * 0.24);
            final_color += lesion_col * (0.04 + bacterial_infection_load * 0.01) * rim;
        }
    }

    if (entity_type == 1 && infection_progress > 0.0 && corpse < 0.5) {
        float phage_stress = 0.5 + 0.5 * sin(surface_pt.z * 24.0 + surface_pt.x * 11.0 + phase * 2.1);
        vec3 phage_col = mix(vec3(0.92, 0.32, 0.22), vec3(0.94, 0.74, 0.30), phage_stress);
        final_color = mix(final_color, final_color * 0.80 + phage_col, smoothstep(0.06, 0.94, infection_progress) * 0.22);
    }

    if ((entity_type == 0 || entity_type == 1) && mitosis > 0.02 && corpse < 0.5) {
        vec3 division_glow = entity_type == 0
            ? mix(vec3(0.90, 0.46, 0.82), vec3(0.60, 0.94, 1.00), smoothstep(0.52, 0.92, mitosis))
            : mix(vec3(1.00, 0.84, 0.36), vec3(0.62, 0.98, 0.90), smoothstep(0.56, 0.92, mitosis));
        final_color += division_glow * (0.08 + rim * 0.20) * smoothstep(0.02, 0.98, mitosis);
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
    for (int i = 0; i < entity_count; i++) {
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

    if (entity_type == 0) {
        float membrane_alpha = clamp(0.18 + fresnel * 0.26 + rim * 0.10 + membrane * 0.05, 0.20, 0.44);
        vec3 transparent_fill = mix(bg + media.rgb * 0.24,
                                    interior_color + shell_color * 0.20,
                                    clamp(interior_alpha + 0.20, 0.20, 0.88));
        final_color = mix(transparent_fill, shell_color + interior_color * 0.78, membrane_alpha);
        final_color += vec3(0.10, 0.18, 0.22) * fresnel * 0.10;
    } else if (entity_type == 1 && bacteria_cloud_hit) {
        float film_alpha = clamp(0.18 + antibiotic_film * antibiotic_visibility * 0.26 +
                                 antibiotic_yield * 0.07, 0.20, 0.56);
        vec3 transparent_cloud = mix(bg + media.rgb * 0.26, final_color, film_alpha);
        final_color = mix(transparent_cloud, final_color, 0.62);
    }

    final_color += media.rgb;

    float haze = 1.0 - exp(-closest_t * haze_density);
    haze *= 0.35 + nutrients * 0.10 + toxicity * 0.35;
    final_color = mix(final_color, bg + media.rgb * 0.35, clamp(haze + media.a * 0.22, 0.0, 0.82));

    // Gamma correction
    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
