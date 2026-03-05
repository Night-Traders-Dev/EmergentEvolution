#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform CameraUBO {
    mat4 inv_vp;
    vec4 eye_pos;
    vec4 screen_info;
    vec4 lighting_params; // x=star, y=uniform, z=ambient, w=fastStar
    vec4 quality_params;  // x=quality, y=hq, z=bg preset, w=sim time
    vec4 render_flags;    // x=background, y=corona, z=comet tails, w=bh lensing
    vec4 fabric_params;   // x=enabled, y=grid size, z=warp strength, w=gravity scale
    vec4 fabric_center;   // xyz=focus plane center
    vec4 fabric_right;    // xyz=focus plane right axis
    vec4 fabric_up;       // xyz=focus plane up axis
};

struct Sphere {
    vec4 pos_radius;
    vec4 base_emit;
    vec4 class_seed_temp;
    vec4 terrain_params;
    vec4 material_params;
    vec4 feature_params;
    vec4 composition_params;
    vec4 atmosphere_params;
    vec4 activity_params;
    vec4 magnetosphere_params;
    vec4 gravity_params;
    vec4 ring_params;
    vec4 phase_params;
    vec4 impact_axis;
    vec4 impact_params;
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

const int PHASE_SOLID = 0;
const int PHASE_LIQUID = 1;
const int PHASE_ICE = 2;
const int PHASE_GAS = 3;
const int PHASE_MOLTEN = 4;
const int PHASE_PLASMA = 5;
const int PHASE_COLLAPSING = 6;

const int SSTAGE_MAIN_SEQUENCE = 0;
const int SSTAGE_RED_GIANT = 2;
const int SSTAGE_WHITE_DWARF = 7;
const int SSTAGE_NEUTRON_STAR = 8;

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

vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289((x * 34.0 + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float simplex3D(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    i = mod289(i);
    vec4 p = permute(permute(permute(
                   i.z + vec4(0.0, i1.z, i2.z, 1.0))
                 + i.y + vec4(0.0, i1.y, i2.y, 1.0))
                 + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 1.0 / 7.0;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

int adjusted_octaves(int base_octaves);

float billow_fbm(vec3 p, int octaves) {
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    int octs = adjusted_octaves(octaves);
    for (int i = 0; i < octs; i++) {
        float n = abs(simplex3D(p * freq));
        val += amp * (n * 2.0 - 1.0);
        freq *= 2.02;
        amp *= 0.5;
    }
    return val * 0.5 + 0.5;
}

float rigid_multifractal(vec3 p, int octaves) {
    float sum = 0.0;
    float amp = 0.65;
    float freq = 1.0;
    float weight = 1.0;
    int octs = adjusted_octaves(octaves);
    for (int i = 0; i < octs; i++) {
        float n = 1.0 - abs(simplex3D(p * freq));
        n *= n;
        n *= weight;
        weight = clamp(n * 2.1, 0.0, 1.0);
        sum += n * amp;
        freq *= 2.12;
        amp *= 0.55;
    }
    return sum;
}

vec2 voronoi_f1_f2(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    float f1 = 1.0e9;
    float f2 = 1.0e9;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 cell = vec3(float(x), float(y), float(z));
                vec3 jitter = hash33(i + cell) * 0.5 + 0.5;
                vec3 r = cell + jitter - f;
                float d = dot(r, r);
                if (d < f1) {
                    f2 = f1;
                    f1 = d;
                } else if (d < f2) {
                    f2 = d;
                }
            }
        }
    }
    return vec2(sqrt(max(f1, 0.0)), sqrt(max(f2, 0.0)));
}

float white_noise(vec3 p) {
    return fract(sin(dot(p, vec3(37.17, 61.43, 97.29))) * 43758.5453);
}

vec3 domain_warp(vec3 p, float strength) {
    vec3 q = vec3(
        simplex3D(p + vec3(12.7, 4.3, 9.2)),
        simplex3D(p + vec3(5.1, 17.9, 2.6)),
        simplex3D(p + vec3(8.3, 1.7, 21.4)));
    return p + q * strength;
}

vec3 cube_sphere_coords(vec3 n) {
    float m = max(max(abs(n.x), abs(n.y)), abs(n.z));
    return n / max(m, 1.0e-4);
}

float hydraulic_erosion_mask(vec3 p) {
    vec3 flow = p;
    float erosion = 0.0;
    float carry = 0.46;
    for (int i = 0; i < 4; ++i) {
        vec3 grad = vec3(
            simplex3D(flow + vec3(0.035, 0.0, 0.0)) - simplex3D(flow - vec3(0.035, 0.0, 0.0)),
            simplex3D(flow + vec3(0.0, 0.035, 0.0)) - simplex3D(flow - vec3(0.0, 0.035, 0.0)),
            simplex3D(flow + vec3(0.0, 0.0, 0.035)) - simplex3D(flow - vec3(0.0, 0.0, 0.035)));
        float g2 = dot(grad, grad);
        vec3 dir = (g2 > 1.0e-6) ? (grad * inversesqrt(g2)) : vec3(0.0, -1.0, 0.0);
        flow -= dir * (0.26 + float(i) * 0.18);
        float channel = 1.0 - abs(simplex3D(flow * 1.65 + vec3(float(i) * 7.3)));
        erosion += channel * carry;
        carry *= 0.56;
    }
    return clamp(erosion, 0.0, 1.0);
}

vec3 whittaker_biome(float temp_n, float humid_n) {
    vec3 tundra = vec3(0.60, 0.64, 0.58);
    vec3 boreal = vec3(0.26, 0.40, 0.25);
    vec3 grass = vec3(0.38, 0.52, 0.24);
    vec3 temperate = vec3(0.20, 0.46, 0.18);
    vec3 rainforest = vec3(0.08, 0.36, 0.12);
    vec3 desert = vec3(0.78, 0.66, 0.44);
    vec3 scrub = vec3(0.56, 0.48, 0.30);
    vec3 snow = vec3(0.86, 0.90, 0.95);

    if (temp_n < 0.18) return mix(tundra, snow, smoothstep(0.45, 0.95, humid_n));
    if (temp_n < 0.35) return mix(tundra, boreal, smoothstep(0.25, 0.85, humid_n));
    if (temp_n < 0.62) {
        vec3 dry = mix(scrub, grass, smoothstep(0.10, 0.55, humid_n));
        vec3 wet = mix(temperate, rainforest, smoothstep(0.60, 1.0, humid_n));
        return mix(dry, wet, smoothstep(0.45, 0.95, humid_n));
    }
    return mix(desert, rainforest, smoothstep(0.35, 0.95, humid_n));
}

int adjusted_octaves(int base_octaves) {
    int quality = int(quality_params.x + 0.5);
    if (quality <= 0) return max(1, base_octaves - 2);
    if (quality == 1) return max(1, base_octaves - 1);
    return base_octaves;
}

vec3 gerstner_lobes(vec2 uv, float t, float seed, float scale) {
    vec2 d1 = normalize(vec2(0.84, 0.54));
    vec2 d2 = normalize(vec2(-0.36, 0.93));
    vec2 d3 = normalize(vec2(0.24, -0.97));
    float p1 = dot(uv, d1) * 10.0 + t * (1.0 + hash11(seed * 0.31) * 1.2);
    float p2 = dot(uv, d2) * 17.0 + t * (1.6 + hash11(seed * 0.19) * 1.1) + 1.7;
    float p3 = dot(uv, d3) * 24.0 + t * (2.2 + hash11(seed * 0.41) * 1.4) + 2.8;
    float h1 = sin(p1) * 0.55;
    float h2 = sin(p2) * 0.30;
    float h3 = sin(p3) * 0.15;
    float gx = cos(p1) * d1.x * 10.0 * 0.55 +
               cos(p2) * d2.x * 17.0 * 0.30 +
               cos(p3) * d3.x * 24.0 * 0.15;
    float gy = cos(p1) * d1.y * 10.0 * 0.55 +
               cos(p2) * d2.y * 17.0 * 0.30 +
               cos(p3) * d3.y * 24.0 * 0.15;
    return vec3((h1 + h2 + h3) * scale, gx * scale, gy * scale);
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

vec3 star_surface_tint(float temperature, float mass, float radius, float stage_f, float luminosity) {
    vec3 tint = blackbody_tint(temperature);
    float giant = (abs(stage_f - float(SSTAGE_RED_GIANT)) < 0.5) ? 1.0 : clamp((radius - 40.0) / 120.0, 0.0, 1.0);
    float white_dwarf = (abs(stage_f - float(SSTAGE_WHITE_DWARF)) < 0.5) ? 1.0 : 0.0;
    float neutron_star = (abs(stage_f - float(SSTAGE_NEUTRON_STAR)) < 0.5) ? 1.0 : 0.0;
    float massive_hot = clamp((mass - 8.0) / 32.0, 0.0, 1.0) * clamp((temperature - 9000.0) / 26000.0, 0.0, 1.0);
    float cool = clamp((6000.0 - temperature) / 3600.0, 0.0, 1.0);
    float luminous = clamp(log2(max(luminosity + 1.0, 1.0)) / 8.0, 0.0, 1.0);

    tint = mix(tint, vec3(1.00, 0.62, 0.34), giant * max(cool, 0.35) * 0.75);
    tint = mix(tint, vec3(0.76, 0.86, 1.00), massive_hot * 0.70);
    tint = mix(tint, vec3(0.92, 0.96, 1.00), white_dwarf * 0.90);
    tint = mix(tint, vec3(0.72, 0.84, 1.00), neutron_star * 0.96);
    tint = mix(tint, vec3(1.0), luminous * 0.10);
    return clamp(tint, 0.0, 1.0);
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

vec3 rotate_about_axis(vec3 v, vec3 axis, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    vec3 a = normalize(axis);
    return v * c + cross(a, v) * s + a * dot(a, v) * (1.0 - c);
}

float spin_phase_from_rate(float angular_vel) {
    const float tau = 6.28318530718;
    return mod(quality_params.w * angular_vel, tau);
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

bool intersect_plane(vec3 ro, vec3 rd, vec3 plane_point, vec3 plane_normal, out float t) {
    float denom = dot(rd, plane_normal);
    if (abs(denom) < 1.0e-5) {
        t = -1.0;
        return false;
    }
    t = dot(plane_point - ro, plane_normal) / denom;
    return t > 0.001;
}

float irregular_radius_scale(vec3 dir, float seed, float roughness) {
    float n = noise3D(dir * 6.0 + vec3(seed * 0.021, seed * 0.017, seed * 0.013));
    return clamp(1.0 + n * roughness, 0.65, 1.45);
}

float rubble_pile_sdf(vec3 local, float base_radius, float seed, float roughness) {
    vec3 lp = local / max(base_radius, 1.0e-4);
    float hull = -1.0e5;
    for (int i = 0; i < 14; ++i) {
        float fi = float(i);
        vec3 n = normalize(hash33(vec3(seed * 0.019 + fi * 17.0, seed * 0.013 + fi * 11.0, seed * 0.031 + fi * 7.0)));
        float plane_r = 0.62 + hash11(seed * 0.037 + fi * 2.73) * 0.44;
        hull = max(hull, dot(lp, n) - plane_r);
    }

    float simplex = simplex3D(lp * 5.6 + vec3(seed * 0.021)) * 0.14 * roughness;
    float rigid = rigid_multifractal(lp * 4.2 + vec3(seed * 0.017), 4) * 0.22 * roughness;
    vec2 vor = voronoi_f1_f2(lp * 6.4 + vec3(seed * 0.023));
    float shards = smoothstep(0.02, 0.20, vor.y - vor.x);
    float pits = smoothstep(0.36, 0.72, 1.0 - vor.x);
    float disp = simplex - rigid * 0.55 + shards * 0.08 * roughness - pits * 0.12 * roughness;
    return (hull + disp) * base_radius;
}

float irregular_sdf(vec3 p, vec3 center, float base_radius, float seed, float roughness,
                    float spin_phase, vec3 spin_axis) {
    vec3 local = p - center;
    if (abs(spin_phase) > 1.0e-8)
        local = rotate_about_axis(local, spin_axis, -spin_phase);
    bool rubble_mode = roughness < 0.30;
    if (rubble_mode) {
        return rubble_pile_sdf(local, base_radius, seed, roughness * 1.8);
    }
    vec3 d = normalize(local);
    float target_r = base_radius * irregular_radius_scale(d, seed, roughness);
    return length(local) - target_r;
}

float intersect_irregular_body(vec3 ro, vec3 rd, vec3 center, float base_radius,
                               float seed, float roughness, float spin_phase, vec3 spin_axis) {
    float bound_r = base_radius * (1.0 + roughness * 2.1);
    float t0 = intersect_sphere(ro, rd, center, bound_r);
    if (t0 < 0.0) return -1.0;

    float t = max(t0 - base_radius * roughness, 0.001);
    for (int i = 0; i < 28; i++) {
        vec3 p = ro + rd * t;
        float d = irregular_sdf(p, center, base_radius, seed, roughness, spin_phase, spin_axis);
        if (d < 0.001) return t;
        t += max(d * 0.65, 0.002);
        if (t > t0 + base_radius * (2.0 + roughness * 3.0)) break;
    }
    return -1.0;
}

vec3 irregular_normal(vec3 p, vec3 center, float base_radius, float seed, float roughness,
                      float spin_phase, vec3 spin_axis) {
    float e = max(base_radius * 0.008, 0.02);
    vec2 h = vec2(e, 0.0);
    float dx = irregular_sdf(p + vec3(h.x, h.y, h.y), center, base_radius, seed, roughness, spin_phase, spin_axis)
             - irregular_sdf(p - vec3(h.x, h.y, h.y), center, base_radius, seed, roughness, spin_phase, spin_axis);
    float dy = irregular_sdf(p + vec3(h.y, h.x, h.y), center, base_radius, seed, roughness, spin_phase, spin_axis)
             - irregular_sdf(p - vec3(h.y, h.x, h.y), center, base_radius, seed, roughness, spin_phase, spin_axis);
    float dz = irregular_sdf(p + vec3(h.y, h.y, h.x), center, base_radius, seed, roughness, spin_phase, spin_axis)
             - irregular_sdf(p - vec3(h.y, h.y, h.x), center, base_radius, seed, roughness, spin_phase, spin_axis);
    return normalize(vec3(dx, dy, dz));
}

float crater_mask(vec3 normal, float seed, float density) {
    vec3 p = normal * (6.0 + density * 14.0) + hash31(seed) * 9.0;
    float base = fbm(p, 4);
    float pits = smoothstep(0.62, 0.88, base);
    float rims = smoothstep(0.48, 0.60, base) - smoothstep(0.60, 0.72, base);
    return clamp(pits * density + rims * density * 0.5, 0.0, 1.0);
}

vec3 safe_normalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    return (len2 > 1.0e-6) ? (v * inversesqrt(len2)) : fallback;
}

void impact_masks(vec3 normal, Sphere hit, out float basin, out float rim, out float ejecta) {
    vec3 axis = safe_normalize(hit.impact_axis.xyz, vec3(0.0, 1.0, 0.0));
    float scar = hit.impact_params.x;
    float radius = max(hit.impact_params.z, 0.02);
    float ejecta_strength = hit.impact_params.w;
    float angular = 1.0 - clamp(dot(normal, axis), -1.0, 1.0);
    float basin_inner = radius * (0.22 + scar * 0.12);
    float basin_outer = radius * (0.95 + scar * 0.55);
    basin = (1.0 - smoothstep(basin_inner, basin_outer, angular)) * scar;
    float rim_inner = radius * (0.70 + scar * 0.18);
    float rim_outer = radius * (1.18 + scar * 0.45);
    rim = (smoothstep(rim_inner, rim_outer, angular) - smoothstep(rim_outer, rim_outer + radius * 0.42, angular)) * scar;
    float ejecta_noise = fbm(normal * (10.0 + scar * 22.0) + axis * 6.0 + vec3(hit.class_seed_temp.x * 0.03), 4);
    float ejecta_band = smoothstep(radius * 0.85, radius * 1.85 + ejecta_strength * 0.55, angular);
    ejecta_band *= 1.0 - smoothstep(radius * 1.85 + ejecta_strength * 0.55,
                                    radius * 3.10 + ejecta_strength * 0.95, angular);
    ejecta = ejecta_band * ejecta_strength * smoothstep(0.42, 0.88, ejecta_noise);
}

float starfield_layer(vec3 rd, float scale, float threshold, float sharpness) {
    vec3 p = normalize(rd) * scale;
    float n = fbm(p + vec3(17.0, 31.0, 47.0), 3);
    float s = smoothstep(threshold, 1.0, n);
    return pow(max(s, 0.0), sharpness);
}

vec3 sample_starfield(vec3 rd) {
    float stars_faint = starfield_layer(rd, 64.0, 0.84, 2.0);
    float stars_mid = starfield_layer(rd * 1.7 + 0.3, 138.0, 0.90, 3.5);
    float stars_bright = starfield_layer(rd * 2.8 - 0.4, 240.0, 0.965, 9.0);
    float twinkle = 0.96 + 0.04 * sin(screen_info.w * 0.16 + rd.x * 133.0 + rd.z * 91.0);

    float tint_seed = hash11(rd.x * 71.0 + rd.y * 39.0 + rd.z * 113.0);
    vec3 warm = vec3(1.00, 0.95, 0.82);
    vec3 cool = vec3(0.72, 0.82, 1.00);
    vec3 neutral = vec3(0.92, 0.93, 0.96);
    vec3 tint = mix(neutral, mix(warm, cool, smoothstep(0.45, 0.92, tint_seed)), 0.75);

    float dense = stars_faint * 0.95 + stars_mid * 0.85;
    float bright = stars_bright * (1.2 + 0.6 * hash11(tint_seed * 91.0));
    return tint * (dense + bright) * twinkle;
}

vec3 background(vec3 rd) {
    if (render_flags.x < 0.5) return vec3(0.0);

    int preset = int(quality_params.z + 0.5);
    float galactic_band = pow(clamp(1.0 - abs(rd.y * 0.80 + noise3D(rd * 2.1) * 0.12), 0.0, 1.0), 5.6);
    float dust = smoothstep(0.40, 0.88, fbm(rd * 12.0 + vec3(2.3, -1.8, 0.5), 5));
    float nebula = fbm(rd * 6.5 + vec3(1.3, -2.1, 0.7), 4);

    vec3 bg = vec3(0.00012, 0.00016, 0.00028);
    vec3 milky_col = mix(vec3(0.009, 0.010, 0.012), vec3(0.026, 0.023, 0.019), nebula);
    float band_core = galactic_band * (0.35 + 0.70 * (1.0 - dust * 0.85));
    bg += milky_col * band_core;
    bg += vec3(0.008, 0.007, 0.006) * pow(galactic_band, 2.1) * (1.0 - dust);

    if (preset == 1) { // Deep black
        bg = vec3(0.00003, 0.00004, 0.00007);
        bg += vec3(0.004, 0.004, 0.005) * pow(galactic_band, 2.8) * (1.0 - dust);
        bg += sample_starfield(rd) * 0.75;
    } else if (preset == 2) { // Nebula
        float nebula2 = fbm(rd * 5.2 + vec3(-3.1, 1.4, 2.2), 5);
        vec3 haze = mix(vec3(0.08, 0.03, 0.10), vec3(0.02, 0.07, 0.12), nebula2);
        bg = vec3(0.00020, 0.00025, 0.00045);
        bg += haze * (0.18 + 0.35 * galactic_band) * smoothstep(0.28, 0.92, nebula);
        bg += vec3(0.010, 0.008, 0.011) * pow(galactic_band, 1.9);
        bg += sample_starfield(rd) * 0.92;
    } else if (preset == 3) { // Warm dust
        vec3 warm = mix(vec3(0.015, 0.010, 0.006), vec3(0.032, 0.020, 0.010), nebula);
        bg = vec3(0.00010, 0.00008, 0.00006);
        bg += warm * (0.28 + 0.55 * galactic_band) * (1.0 - dust * 0.55);
        bg += vec3(0.006, 0.005, 0.004) * pow(galactic_band, 2.0);
        bg += sample_starfield(rd) * 0.86;
    } else if (preset == 4) { // Blue haze
        float haze_n = fbm(rd * 8.0 + vec3(5.3, -2.0, 1.0), 4);
        vec3 haze = mix(vec3(0.010, 0.022, 0.050), vec3(0.020, 0.040, 0.085), haze_n);
        bg = vec3(0.00010, 0.00016, 0.00035);
        bg += haze * (0.22 + 0.60 * galactic_band);
        bg += vec3(0.007, 0.010, 0.016) * pow(galactic_band, 2.2);
        bg += sample_starfield(rd) * 0.95;
    } else { // Realistic
        bg += sample_starfield(rd);
    }
    return bg;
}

float grid_mask(vec2 p, float cell_size, float thickness) {
    vec2 scaled = p / max(cell_size, 1.0e-4);
    vec2 fw = max(fwidth(scaled), vec2(1.0e-4));
    vec2 dist = abs(fract(scaled - 0.5) - 0.5);
    vec2 line = 1.0 - smoothstep(vec2(0.0), fw * (1.2 + thickness), dist);
    return clamp(max(line.x, line.y), 0.0, 1.0);
}

void sample_fabric_field(vec3 world_pos, int body_count, out vec2 warped_local,
                         out float well, out float curvature, out vec2 flow_dir) {
    vec3 plane_center = fabric_center.xyz;
    vec3 plane_right = normalize(fabric_right.xyz);
    vec3 plane_up = normalize(fabric_up.xyz);
    vec2 local = vec2(dot(world_pos - plane_center, plane_right),
                      dot(world_pos - plane_center, plane_up));

    vec2 grad = vec2(0.0);
    float grid_size = max(fabric_params.y, 1.0);
    float gravity_scale = max(fabric_params.w, 0.001);
    well = 0.0;

    for (int i = 0; i < body_count && i < 512; i++) {
        float mass = max(spheres[i].gravity_params.x, 0.0);
        float display_mass = log2(1.0 + mass * gravity_scale * 12.0);
        if (display_mass < 0.03) continue;
        float curvature_mass = pow(display_mass, 1.18);
        int render_class = int(spheres[i].class_seed_temp.y + 0.5);
        if (render_class == RENDER_PLANET || render_class == RENDER_MOON)
            curvature_mass *= 1.35;
        else if (render_class == RENDER_STAR)
            curvature_mass *= 1.85;
        else if (render_class == RENDER_BLACK_HOLE)
            curvature_mass *= 2.75;

        vec2 body_local = vec2(dot(spheres[i].pos_radius.xyz - plane_center, plane_right),
                               dot(spheres[i].pos_radius.xyz - plane_center, plane_up));
        vec2 delta = local - body_local;
        float body_soft = max(spheres[i].pos_radius.w * 1.1, grid_size * 0.18);
        float dist2 = dot(delta, delta) + body_soft * body_soft;
        float inv_dist = inversesqrt(dist2);
        float inv_dist3 = inv_dist * inv_dist * inv_dist;

        well += curvature_mass * inv_dist;
        grad -= delta * curvature_mass * inv_dist3;
    }

    float well_boost = 1.0 + clamp(well * 0.06, 0.0, 2.6);
    warped_local = local + grad * (grid_size * fabric_params.z * 0.72 * well_boost);
    curvature = length(grad) * (grid_size * fabric_params.z * 0.16);
    flow_dir = grad;
}

bool sample_space_fabric(vec3 ro, vec3 rd, int body_count,
                         out vec3 fabric_col, out float fabric_alpha, out float fabric_t) {
    if (fabric_params.x < 0.5) {
        fabric_t = -1.0;
        fabric_col = vec3(0.0);
        fabric_alpha = 0.0;
        return false;
    }

    vec3 plane_right = normalize(fabric_right.xyz);
    vec3 plane_up = normalize(fabric_up.xyz);
    vec3 plane_normal = normalize(cross(plane_right, plane_up));
    if (!intersect_plane(ro, rd, fabric_center.xyz, plane_normal, fabric_t)) {
        fabric_col = vec3(0.0);
        fabric_alpha = 0.0;
        return false;
    }

    vec3 world_pos = ro + rd * fabric_t;
    vec2 warped_local;
    float well;
    float curvature;
    vec2 flow_dir;
    sample_fabric_field(world_pos, body_count, warped_local, well, curvature, flow_dir);

    float grid_size = max(fabric_params.y, 1.0);
    float minor = grid_mask(warped_local, grid_size, 0.00);
    float major = grid_mask(warped_local, grid_size * 5.0, 0.05);
    float well_glow = 1.0 - exp(-well * 0.20);
    float facing = abs(dot(rd, plane_normal));
    float flow_len = length(flow_dir);
    float flow_boost = clamp(flow_len * grid_size * 0.06, 0.0, 1.0);
    float bend_boost = clamp(curvature * 0.85, 0.0, 1.0);
    float minor_intensity = minor * (0.16 + 0.10 * well_glow + 0.08 * bend_boost);
    float major_intensity = major * (0.28 + 0.14 * well_glow + 0.10 * bend_boost);
    float line_alpha = minor_intensity + major_intensity;
    line_alpha *= 0.28 + 0.52 * facing;
    line_alpha *= 0.82 + 0.28 * max(well_glow, flow_boost);
    line_alpha = clamp(line_alpha, 0.0, 0.48);

    vec3 line_col = vec3(0.96 + 0.04 * well_glow);
    line_col = clamp(line_col, 0.0, 1.0);
    vec3 col = line_col * (minor_intensity * 1.05 + major_intensity * 1.18);

    fabric_col = col;
    fabric_alpha = line_alpha;
    return true;
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

vec3 magnetic_axis(float angle, float seed) {
    float phase = fract(seed * 0.0137 + 0.17) * 6.28318530718;
    vec3 axis = vec3(cos(phase) * sin(angle), cos(angle), sin(phase) * sin(angle));
    if (dot(axis, axis) < 1.0e-5) axis = vec3(0.0, 1.0, 0.0);
    return normalize(axis);
}

vec3 ring_axis(float tilt, float seed) {
    float phase = fract(seed * 0.0173 + 0.61) * 6.28318530718;
    vec3 axis = vec3(cos(phase) * sin(tilt), cos(tilt), sin(phase) * sin(tilt));
    if (dot(axis, axis) < 1.0e-5) axis = vec3(0.0, 1.0, 0.0);
    return normalize(axis);
}

vec3 phase_emission_tint(float phase_kind, float intensity, float temperature, vec3 base_col) {
    if (phase_kind < float(PHASE_MOLTEN) - 0.5)
        return vec3(0.0);

    if (phase_kind < float(PHASE_PLASMA) - 0.5) {
        vec3 hot = mix(vec3(0.95, 0.18, 0.02), vec3(1.00, 0.58, 0.12),
                       clamp((temperature - 900.0) / 1200.0, 0.0, 1.0));
        return hot * intensity * 1.25;
    }
    if (phase_kind < float(PHASE_COLLAPSING) - 0.5) {
        vec3 plasma = mix(vec3(1.00, 0.58, 0.18), vec3(1.00, 0.92, 0.74),
                          clamp((temperature - 2000.0) / 12000.0, 0.0, 1.0));
        return plasma * intensity * 1.45;
    }

    vec3 collapsing = mix(base_col * 0.75 + vec3(0.28, 0.14, 0.06),
                          vec3(1.0, 0.82, 0.48),
                          clamp(intensity, 0.0, 1.0));
    return collapsing * intensity * 0.75;
}

void accumulate_rings(vec3 ro, vec3 rd, int body_count, float scene_t,
                      out vec3 ring_col, out float ring_alpha) {
    ring_col = vec3(0.0);
    ring_alpha = 0.0;

    for (int i = 0; i < body_count && i < 512; i++) {
        Sphere body = spheres[i];
        float inner = body.ring_params.x;
        float outer = body.ring_params.y;
        float density = body.ring_params.z;
        if (density < 0.01 || outer <= inner + 0.01) continue;

        vec3 axis = ring_axis(max(body.ring_params.w, 0.02), body.class_seed_temp.x);
        float denom = dot(rd, axis);
        if (abs(denom) < 1.0e-4) continue;

        float t = dot(body.pos_radius.xyz - ro, axis) / denom;
        if (t <= 0.0) continue;
        if (scene_t > 0.0 && t > scene_t + 0.02) continue;

        float body_t = intersect_sphere(ro, rd, body.pos_radius.xyz, body.pos_radius.w);
        if (body_t > 0.0 && body_t < t - 0.02) continue;

        vec3 hit = ro + rd * t;
        vec3 rel = hit - body.pos_radius.xyz;
        vec3 planar = rel - axis * dot(rel, axis);
        float radial = length(planar);
        if (radial < inner || radial > outer) continue;

        float edge = max((outer - inner) * 0.06, body.pos_radius.w * 0.05);
        float annulus = smoothstep(inner, inner + edge, radial) *
                        (1.0 - smoothstep(outer - edge, outer, radial));
        float ring_u = (radial - inner) / max(outer - inner, 1.0e-4);
        float az = atan(planar.z, planar.x);
        float band_noise = fbm(vec3(ring_u * 22.0,
                                    az * 3.4 + body.class_seed_temp.x * 0.07,
                                    body.class_seed_temp.x * 0.11), 4);
        float gaps = smoothstep(0.10, 0.24, abs(sin(ring_u * 36.0 + band_noise * 3.2)));
        float bands = mix(0.48, 1.0, band_noise) * gaps;
        float view_facing = 0.35 + 0.65 * clamp(1.0 - abs(dot(rd, axis)), 0.0, 1.0);
        float shadow = 1.0;
        float n_dot_l = dot(axis, normalize(vec3(0.45, 0.75, 0.35)));
        shadow *= 0.78 + 0.22 * abs(n_dot_l);

        vec3 dusty = mix(vec3(0.48, 0.40, 0.30), vec3(0.76, 0.66, 0.52), band_noise);
        vec3 icy = mix(vec3(0.70, 0.76, 0.84), vec3(0.95, 0.98, 1.0), bands);
        float ice_fraction = body.phase_params.w;
        float hot = clamp((body.class_seed_temp.w - 260.0) / 1400.0, 0.0, 1.0);
        vec3 col = mix(dusty, icy, ice_fraction);
        col = mix(col, vec3(0.92, 0.42, 0.12), hot * (1.0 - ice_fraction) * 0.55);

        float alpha = density * annulus * bands * view_facing * shadow * 0.72;
        ring_col += col * alpha;
        ring_alpha = clamp(ring_alpha + alpha, 0.0, 0.82);
    }
}

vec3 magnetosphere_tint(int render_class, float surface_type, float temperature) {
    if (render_class == RENDER_STAR)
        return mix(vec3(1.0, 0.78, 0.50), vec3(0.78, 0.88, 1.0), clamp((temperature - 4500.0) / 18000.0, 0.0, 1.0));
    if (surface_type > 2.5 && surface_type < 3.5)
        return mix(vec3(0.34, 0.72, 1.0), vec3(0.62, 0.86, 1.0), clamp((180.0 - temperature) / 140.0, 0.0, 1.0));
    return vec3(0.32, 0.88, 0.72);
}

void accumulate_magnetospheres(vec3 ro, vec3 rd, int body_count, float scene_t,
                               out vec3 magnet_col, out float magnet_alpha) {
    magnet_col = vec3(0.0);
    magnet_alpha = 0.0;

    for (int i = 0; i < body_count && i < 512; i++) {
        Sphere body = spheres[i];
        int render_class = int(body.class_seed_temp.y + 0.5);
        if (render_class != RENDER_PLANET && render_class != RENDER_MOON && render_class != RENDER_STAR)
            continue;

        float field = body.magnetosphere_params.x;
        float outer_radius = body.magnetosphere_params.y;
        if (field < 0.03 || outer_radius <= body.pos_radius.w * 1.04)
            continue;

        float outer_t = intersect_sphere(ro, rd, body.pos_radius.xyz, outer_radius);
        if (outer_t < 0.0) continue;
        if (scene_t > 0.0 && outer_t > scene_t + 0.02) continue;

        float inner_radius = max(body.pos_radius.w * (render_class == RENDER_STAR ? 1.04 : 1.10),
                                 outer_radius * mix(0.54, 0.72, clamp(body.magnetosphere_params.w, 0.0, 1.0)));
        float inner_t = intersect_sphere(ro, rd, body.pos_radius.xyz, inner_radius);
        float shell_thickness = max(outer_radius - inner_radius, body.pos_radius.w * 0.04);
        float path_thickness = (inner_t > outer_t) ? min(inner_t - outer_t, shell_thickness) : shell_thickness;

        vec3 sample_pos = ro + rd * outer_t;
        vec3 n = normalize(sample_pos - body.pos_radius.xyz);
        vec3 axis = magnetic_axis(body.magnetosphere_params.z, body.class_seed_temp.x);
        float pole = pow(clamp(abs(dot(n, axis)), 0.0, 1.0), 4.5);
        float belt = pow(clamp(1.0 - abs(dot(n, axis)), 0.0, 1.0), 5.0);
        float edge = pow(clamp(1.0 - abs(dot(n, -rd)), 0.0, 1.0), 1.8);
        float thickness_n = clamp(path_thickness / max(shell_thickness, 1.0e-4), 0.0, 1.0);
        bool planet_like = (render_class == RENDER_PLANET || render_class == RENDER_MOON);
        float storm = planet_like ? body.gravity_params.y : body.activity_params.x * 0.45;
        float retention = planet_like ? body.gravity_params.z : 1.0;
        float belt_strength = planet_like ? body.gravity_params.w : body.magnetosphere_params.w * (0.45 + storm * 0.30);
        float phase = screen_info.w * (0.40 + storm * 1.45 + field * 0.035) + body.class_seed_temp.x * 0.21;
        float field_glow = field * (0.060 + 0.085 * thickness_n);

        vec3 tint = magnetosphere_tint(render_class, body.class_seed_temp.z, body.class_seed_temp.w);
        float aurora = body.activity_params.y;
        float belt_wave = smoothstep(0.52, 0.84,
            fbm(vec3(atan(n.z, n.x) * 7.5 - phase * 1.4,
                     dot(n, axis) * 15.0,
                     body.class_seed_temp.x * 0.09), 4));
        float aurora_arc = smoothstep(0.50, 0.82,
            fbm(vec3(atan(n.z, n.x) * 10.0 + phase * 1.8,
                     abs(dot(n, axis)) * 18.0 - phase * 0.7,
                     body.class_seed_temp.x * 0.23), 4));
        float intensity = edge * field_glow * (0.85 + storm * 0.12);
        if (planet_like) {
            intensity += pole * aurora * retention * (0.12 + storm * 0.22) * aurora_arc;
            intensity += belt * belt_strength * (0.03 + storm * 0.025) * belt_wave;
        } else {
            intensity += pole * body.activity_params.x * 0.045 * aurora_arc;
            intensity += belt * belt_strength * 0.025 * belt_wave;
        }

        intensity = clamp(intensity, 0.0, 0.78);
        if (intensity <= 0.001) continue;

        vec3 aurora_tint = mix(vec3(0.18, 1.0, 0.56), vec3(0.52, 0.92, 1.0),
                               step(2.5, body.class_seed_temp.z));
        vec3 belt_tint = mix(tint, vec3(0.84, 0.94, 1.0), 0.45);
        vec3 shell = tint * edge * field_glow * (0.65 + 0.55 * pole + 0.20 * belt);
        if (planet_like) {
            shell += aurora_tint * pole * aurora_arc * aurora * retention * (0.20 + storm * 0.38);
            shell += belt_tint * belt * belt_wave * belt_strength * (0.08 + storm * 0.06);
        } else {
            shell += mix(tint, vec3(1.0, 0.92, 0.80), 0.25) * belt * belt_wave * belt_strength * 0.08;
        }
        magnet_col += shell;
        magnet_alpha = clamp(magnet_alpha + intensity * 0.70, 0.0, 0.82);
    }
}

vec3 shade_star(vec3 normal, vec3 rd, Sphere hit) {
    float seed = hit.class_seed_temp.x;
    float temperature = hit.class_seed_temp.w;
    float gran_amp = hit.terrain_params.x;
    float gran_freq = hit.terrain_params.y;
    float flare_activity = hit.activity_params.x;
    float corona_strength = hit.activity_params.y;
    float spin_visual = hit.activity_params.z;
    float spot_strength = hit.activity_params.w;
    float spot_coverage = hit.composition_params.x;
    float flare_frequency = max(hit.composition_params.y, 0.3);
    float pulsation = hit.composition_params.z;
    float differential_rotation = hit.composition_params.w;
    float mass = hit.gravity_params.x;
    float stage = hit.gravity_params.y;
    float fuel = hit.gravity_params.z;
    float luminosity = hit.gravity_params.w;

    vec3 tint = star_surface_tint(temperature, mass, hit.pos_radius.w, stage, luminosity);
    float lon = atan(normal.z, normal.x);
    float lat = asin(clamp(normal.y, -1.0, 1.0));
    float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
    vec3 nwarp = vec3(
        lon * (gran_freq * (1.8 + differential_rotation * 0.8)) + spin_phase,
        lat * (gran_freq * 3.0),
        seed * 0.11 + screen_info.w * pulsation * 0.18);
    float granulation = fbm(nwarp, 5);
    float cells = smoothstep(0.40 - gran_amp * 0.10, 0.64 + gran_amp * 0.10, granulation);
    vec3 gran_col = mix(tint * (0.76 + fuel * 0.10), tint * (1.14 + gran_amp * 0.22), cells);

    float active_lat = 0.16 + differential_rotation * 0.24;
    float magnetic_band = 1.0 - smoothstep(0.08, 0.44, abs(abs(normal.y) - active_lat));
    float cool_star = clamp((6500.0 - temperature) / 4000.0, 0.0, 1.0);
    vec3 spot_field = vec3(
        lon * (8.0 + differential_rotation * 8.0) - spin_phase * (0.9 + differential_rotation * 0.5),
        lat * 14.0,
        seed * 0.37);
    float spot_noise = fbm(spot_field, 4);
    float spot_cluster = fbm(vec3(lon * 4.0 + seed * 0.03, lat * 6.0, seed * 0.59 - spin_phase * 0.25), 3);
    float spot_mask = spot_noise * 0.72 + spot_cluster * 0.48 + magnetic_band * 0.22;
    float spot_cut = clamp(1.0 - spot_coverage * (1.35 + cool_star * 0.35), 0.52, 0.92);
    float spots = smoothstep(spot_cut, min(spot_cut + 0.10, 0.995), spot_mask);
    float facula = smoothstep(0.46, 0.64, fbm(spot_field * vec3(0.72, 0.82, 1.0) + vec3(0.8, 0.0, 1.7), 4));
    gran_col = mix(gran_col, gran_col * (0.42 + 0.2 * cool_star), spots * spot_strength * cool_star);
    gran_col += tint * facula * spot_strength * (0.10 + 0.12 * cool_star);

    float mu = max(dot(normal, -rd), 0.0);
    float limb_dark = mix(0.45, 0.75, clamp(temperature / 18000.0, 0.0, 1.0));
    float pulsate = 1.0 + pulsation * 0.06 * sin(screen_info.w * (0.6 + flare_frequency * 0.35) + seed * 0.13);
    vec3 col = gran_col * mix(limb_dark, 1.0, pow(mu, 0.65)) * pulsate;

    float global_flare = 0.94 + flare_activity * 0.09 *
        sin(screen_info.w * (1.0 + flare_frequency * 0.7) + seed * 0.13);
    col *= global_flare;

    if (render_flags.y > 0.5) {
        float edge = pow(clamp(1.0 - mu, 0.0, 1.0), 2.0);
        float active_region = smoothstep(0.62, 0.84,
            fbm(vec3(lon * 6.0 + spin_phase * 0.65, lat * 10.0 - spin_phase * 0.18, seed * 0.41), 4)
            + magnetic_band * 0.35);
        float flare_cycle = max(0.0, sin(screen_info.w * (0.7 + flare_frequency * 1.1) + seed * 0.47));
        float flare_burst = pow(flare_cycle, 8.0) * active_region * flare_activity;
        float streamer = edge * active_region * (0.35 + 0.65 * flare_burst);
        col += tint * corona_strength * edge * (0.62 + 0.18 * pulsation);
        col += mix(tint, vec3(1.00, 0.96, 0.88), 0.35) * streamer * (0.18 + flare_activity * 0.55);
        col += vec3(0.95, 0.98, 1.0) * flare_burst * edge * (0.25 + corona_strength * 0.65);
    }

    return col * (1.1 + hit.base_emit.a * 0.35);
}

vec3 shade_gas_giant(vec3 normal, Sphere hit, vec3 light_dir, vec3 view_dir) {
    float seed = hit.class_seed_temp.x;
    float temperature = hit.class_seed_temp.w;
    float mass = hit.gravity_params.x;
    float band_freq = 5.0 + hit.terrain_params.y * 0.85;
    vec3 seed_offset = hash31(seed) * 80.0;
    float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
    vec3 spin_axis = ring_axis(max(hit.ring_params.w, 0.02), seed);
    vec3 sample_normal = (abs(spin_phase) > 1.0e-8)
        ? rotate_about_axis(normal, spin_axis, spin_phase)
        : normal;
    float lon = atan(sample_normal.z, sample_normal.x);
    float lat = asin(clamp(sample_normal.y, -1.0, 1.0));
    float storms = hit.activity_params.x;
    bool ice_giant = temperature < 170.0 || mass < 2.5e-4;
    float lat_abs = abs(sample_normal.y);
    float zonal_speed = mix(1.90, 0.35, pow(lat_abs, 1.55));
    float wind_speed = screen_info.w * (0.08 + storms * 0.22 + hash11(seed * 0.031) * 0.10) * zonal_speed;
    vec3 flow_uv = vec3(lon * 2.6, lat * 5.0, seed * 0.07 + screen_info.w * 0.02);
    vec3 flow_map = domain_warp(flow_uv, 0.55 + storms * 0.25);
    float shear = sin(lat * 6.0) * (0.8 + storms * 0.45);
    float band_lon = lon * (10.0 + hit.terrain_params.y * 0.28) + wind_speed * (1.0 + shear) + flow_map.x * 0.42;
    float fine_lon = lon * (18.0 + hit.terrain_params.y * 0.42) + wind_speed * (1.6 - shear * 0.35) + flow_map.y * 0.55;
    float warp = simplex3D(vec3(band_lon * 0.52, lat * 4.4, seed * 0.07) + seed_offset * 0.02) * 1.1;
    float band = sin(lat * band_freq + warp + fbm(vec3(band_lon * 0.20, lat * 2.0, seed * 0.11), 4) * 1.8);
    float secondary = sin(lat * (band_freq * 0.62) - warp * 0.55 +
                          fbm(vec3(fine_lon * 0.15, lat * 3.4, seed * 0.17), 3) * 1.4);

    vec3 warm;
    vec3 cool;
    if (ice_giant) {
        warm = vec3(0.38, 0.70, 0.92);
        cool = vec3(0.10, 0.24, 0.52);
    } else if (temperature > 900.0) {
        warm = vec3(0.95, 0.48, 0.20);
        cool = vec3(0.56, 0.15, 0.08);
    } else {
        warm = vec3(0.82, 0.68, 0.45);
        cool = vec3(0.54, 0.40, 0.25);
    }

    vec3 col = mix(warm, cool, band * 0.5 + 0.5);
    col = mix(col, col * 1.14, secondary * 0.5 + 0.5);
    float cloud_lanes = smoothstep(0.46, 0.78,
        fbm(vec3(fine_lon * 0.55, lat * 7.5 + sin(band_lon * 0.3) * 0.7, seed * 0.13), 5));
    col = mix(col, mix(col, vec3(0.95, 0.98, 1.0), ice_giant ? 0.55 : 0.35), cloud_lanes * (0.18 + storms * 0.16));

    float storm = fbm(vec3(lon * 7.0 - wind_speed * 0.7, lat * 10.0, seed * 0.09) + seed_offset * 0.03, 5);
    col += (storm - 0.5) * (0.16 + storms * 0.14);

    float giant_storm = exp(-pow((lon + wind_speed * 0.28 - sin(seed * 0.01) * 1.4) / 0.28, 2.0)
                          - pow((lat - sin(seed * 0.003) * 0.22) / 0.11, 2.0));
    vec3 storm_col = ice_giant ? vec3(0.92, 0.98, 1.0) : vec3(1.0, 0.74, 0.52);
    col = mix(col, storm_col, giant_storm * (0.22 + storms * 0.35));

    float haze = smoothstep(0.58, 0.94, abs(sample_normal.y));
    col = mix(col, mix(col, vec3(0.95, 0.98, 1.0), 0.60), haze * (ice_giant ? 0.34 : 0.16));
    float fresnel = pow(clamp(1.0 - max(dot(normal, -view_dir), 0.0), 0.0, 1.0), 2.8);
    vec3 rim_tint = ice_giant ? vec3(0.70, 0.82, 0.96) : vec3(0.92, 0.88, 0.74);
    col = mix(col, mix(col, rim_tint, 0.28), fresnel * (0.34 + storms * 0.18));

    float ndl = max(dot(normal, light_dir), 0.0);
    return col * (0.28 + 0.72 * ndl);
}

vec3 rocky_palette(float style, float tone) {
    vec3 dark;
    vec3 mid;
    vec3 bright;

    if (style < 0.5) {
        dark = vec3(0.08, 0.09, 0.11);
        mid = vec3(0.28, 0.31, 0.35);
        bright = vec3(0.60, 0.65, 0.71);
    } else if (style < 1.5) {
        dark = vec3(0.15, 0.06, 0.05);
        mid = vec3(0.45, 0.18, 0.10);
        bright = vec3(0.78, 0.45, 0.20);
    } else if (style < 2.5) {
        dark = vec3(0.21, 0.15, 0.09);
        mid = vec3(0.57, 0.40, 0.23);
        bright = vec3(0.86, 0.73, 0.50);
    } else if (style < 3.5) {
        dark = vec3(0.13, 0.11, 0.08);
        mid = vec3(0.36, 0.28, 0.15);
        bright = vec3(0.72, 0.66, 0.42);
    } else if (style < 4.5) {
        dark = vec3(0.09, 0.12, 0.08);
        mid = vec3(0.26, 0.34, 0.20);
        bright = vec3(0.60, 0.70, 0.46);
    } else {
        dark = vec3(0.16, 0.18, 0.20);
        mid = vec3(0.44, 0.48, 0.54);
        bright = vec3(0.82, 0.85, 0.89);
    }

    vec3 col = mix(dark, mid, smoothstep(0.14, 0.52, tone));
    return mix(col, bright, smoothstep(0.60, 0.88, tone));
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
    float continent_cov = hit.feature_params.x;
    float island_cov = hit.feature_params.y;
    float river_density = hit.feature_params.z;
    float ice_sheet_cov = hit.feature_params.w;
    float cloud_cov = hit.atmosphere_params.x;
    float volcanic = hit.activity_params.z;
    float phase_kind = hit.phase_params.x;
    float phase_intensity = hit.phase_params.y;
    float collapse_phase = hit.phase_params.z;
    float impact_heat = hit.impact_params.y;
    bool frozen_world = (surface_type > 1.5 && surface_type < 2.5);
    bool gas_world = (surface_type > 2.5 && surface_type < 3.5);
    bool mixed_world = (surface_type > 3.5);
    bool water_world = (surface_type > 0.5 && surface_type < 1.5);
    bool earth_like_world = mixed_world && ocean_cov > 0.20 && ocean_cov < 0.80 &&
                            temperature >= 240.0 && temperature <= 330.0;
    float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
    vec3 spin_axis = ring_axis(max(hit.ring_params.w, 0.02), seed);
    vec3 sample_normal = (abs(spin_phase) > 1.0e-8)
        ? rotate_about_axis(normal, spin_axis, spin_phase)
        : normal;

    vec3 seed_offset = hash31(seed) * 120.0;
    float lon = atan(sample_normal.z, sample_normal.x);
    float lat = asin(clamp(sample_normal.y, -1.0, 1.0));
    vec3 cube_np = cube_sphere_coords(sample_normal) * max(terrain_freq, 1.0) + seed_offset;
    vec3 np = cube_np;
    vec3 warp = vec3(
        simplex3D(np + vec3(0.0, 5.2, 1.3)),
        simplex3D(np + vec3(5.2, 1.3, 0.0)),
        simplex3D(np + vec3(1.3, 0.0, 5.2))
    );
    vec3 warped_np = domain_warp(np + warp * terrain_amp * 0.35, 0.22 + ridge_amp * 0.30);
    float base_fbm = fbm(warped_np * 0.95, 6);
    float simplex_elev = simplex3D(warped_np * 1.15) * 0.5 + 0.5;
    float rigid_detail = rigid_multifractal(warped_np * 1.6 + vec3(seed * 0.09), 5);
    float billow_mtn = billow_fbm(warped_np * 1.25 + vec3(7.0, 2.0, 4.0), 5);
    float elev = clamp(mix(base_fbm, simplex_elev, 0.35) +
                       rigid_detail * 0.18 + billow_mtn * 0.12 * ridge_amp, 0.0, 1.0);
    if (!gas_world && surface_type < 0.5) {
        float crater_field = crater_mask(sample_normal, seed * 1.17, clamp(hit.terrain_params.w + 0.18, 0.0, 1.0));
        elev = clamp(elev - crater_field * 0.10 + rigid_detail * 0.05, 0.0, 1.0);
    }
    if (frozen_world) {
        vec2 cell = voronoi_f1_f2(warped_np * 3.5 + vec3(seed * 0.11));
        float cracks = smoothstep(0.03, 0.18, cell.y - cell.x);
        float basin = smoothstep(0.20, 0.54, cell.x);
        elev = clamp(elev + cracks * 0.06 - basin * 0.08, 0.0, 1.0);
    }
    vec3 macro_np = sample_normal * max(terrain_freq * 0.36, 0.9) + seed_offset * 0.12;
    float continent_noise = fbm(macro_np + warp * 0.32, 5);
    float island_noise = fbm(sample_normal * (terrain_freq * 1.65 + 2.0) + seed_offset * 0.26 + warp * 0.18, 4);
    float continent_mask = smoothstep(0.58 - continent_cov * 0.40, 0.88 - continent_cov * 0.18, continent_noise);
    float island_mask = smoothstep(0.80 - island_cov * 0.42, 0.96, island_noise) * (1.0 - continent_mask * 0.72);
    float land_mask = clamp(continent_mask + island_mask * 0.72, 0.0, 1.0);
    float mountain_mask = smoothstep(0.54, 0.82, ridged_fbm(warped_np * 0.55 + vec3(4.0, 1.2, 2.0), 4)) * land_mask;
    float valley_mask = smoothstep(0.42, 0.66, fbm(warped_np * 0.78 + vec3(-3.0, 2.0, 1.0), 4)) *
                        land_mask * (1.0 - mountain_mask * 0.65);
    if (earth_like_world) {
        float continent_simplex = simplex3D(macro_np * 1.35 + warp * 0.22) * 0.5 + 0.5;
        continent_mask = smoothstep(0.45 - continent_cov * 0.28, 0.72, continent_simplex);
        land_mask = clamp(max(land_mask, continent_mask), 0.0, 1.0);
        float billow_ranges = billow_fbm(warped_np * 1.42 + vec3(seed * 0.13), 5);
        mountain_mask = max(mountain_mask, smoothstep(0.60, 0.88, billow_ranges) * land_mask);
        float erosion = hydraulic_erosion_mask(warped_np * 1.9 + vec3(seed * 0.07));
        valley_mask = max(valley_mask, erosion * land_mask * (1.0 - mountain_mask * 0.45));
        elev = clamp(elev - erosion * 0.08 * land_mask + mountain_mask * 0.06, 0.0, 1.0);
    }
    elev = clamp(elev + land_mask * (0.06 + terrain_amp * 0.18) +
                 mountain_mask * (0.08 + ridge_amp * 0.22) -
                 valley_mask * (0.04 + terrain_amp * 0.10), 0.0, 1.0);

    vec3 tangent;
    vec3 bitangent;
    build_basis(sample_normal, tangent, bitangent);
    float eps = 0.004;
    float e_du = fbm((sample_normal + tangent * eps) * terrain_freq + seed_offset, 4) - elev;
    float e_dv = fbm((sample_normal + bitangent * eps) * terrain_freq + seed_offset, 4) - elev;
    vec3 surf_normal_local = normalize(sample_normal + (tangent * e_du + bitangent * e_dv) * hit.material_params.w * 10.0);
    float slope = clamp(1.0 - max(dot(surf_normal_local, sample_normal), 0.0), 0.0, 1.0);
    surf_normal = (abs(spin_phase) > 1.0e-8)
        ? rotate_about_axis(surf_normal_local, spin_axis, -spin_phase)
        : surf_normal_local;

    if (surface_type > 2.5 && surface_type < 3.5)
        return shade_gas_giant(normal, hit, light_dir, rd);

    float sea_level = clamp(1.00 - clamp(ocean_cov, 0.0, 1.0) * 0.90, 0.10, 0.94);
    if (water_world) {
        sea_level = max(sea_level, 0.68 + ocean_cov * 0.24);
    }
    float temperate = clamp(1.0 - abs(temperature - 288.0) / 120.0, 0.0, 1.0);
    float pressure_factor = smoothstep(0.03, 2.5, hit.atmosphere_params.y);
    float humidity = clamp(ocean_cov * 0.88 + cloud_cov * 0.55 + pressure_factor * 0.18, 0.0, 1.0);
    float dryness = clamp(1.0 - humidity * 0.90, 0.0, 1.0);
    float coldness = clamp((245.0 - temperature) / 190.0, 0.0, 1.0);
    float heat = clamp((temperature - 340.0) / 900.0, 0.0, 1.0);
    float palette_roll = hash11(seed * 0.021 + terrain_freq * 0.13 + continent_cov * 2.7 +
                                hit.gravity_params.x * 1800.0);
    float style = floor(palette_roll * 6.0);
    if (temperature > 900.0) {
        style = 1.0;
    } else if (coldness > 0.55 && ocean_cov < 0.18) {
        style = 5.0;
    } else if (dryness > 0.65 && temperature > 285.0) {
        style = 2.0;
    } else if (humidity > 0.55 && temperate > 0.40) {
        style = 3.0;
    } else if (metal_frac > 0.34 && heat > 0.18) {
        style = 1.0;
    }

    vec3 palette_shift = (hash31(seed * 0.113 + surface_type * 9.7) - 0.5) * vec3(0.14, 0.12, 0.10);
    float rock_tone = clamp(elev + land_mask * 0.08 - valley_mask * 0.05, 0.0, 1.0);
    vec3 ice_col = vec3(0.72, 0.82, 0.95);
    vec3 col = rocky_palette(style, rock_tone);
    col = clamp(col + palette_shift * (0.10 + metal_frac * 0.06), 0.0, 1.0);

    vec3 base_land = rocky_palette(style, clamp(0.44 + elev * 0.22 + rock_frac * 0.10, 0.0, 1.0));
    vec3 desert_col = mix(vec3(0.60, 0.42, 0.22), vec3(0.86, 0.72, 0.46), smoothstep(0.18, 0.72, elev));
    vec3 oxidized_col = mix(vec3(0.44, 0.18, 0.12), vec3(0.78, 0.36, 0.16),
                            smoothstep(0.18, 0.70, metal_frac + heat * 0.5));
    vec3 vegetated_col = mix(vec3(0.14, 0.22, 0.10), vec3(0.20, 0.46, 0.16), temperate);
    vegetated_col = mix(vegetated_col, vec3(0.26, 0.58, 0.22), clamp(humidity * temperate, 0.0, 1.0) * 0.55);
    vec3 land_col = base_land;
    land_col = mix(land_col, desert_col, dryness * (0.55 + heat * 0.25));
    land_col = mix(land_col, oxidized_col, clamp(heat * 0.40 + metal_frac * 0.22, 0.0, 0.72));
    land_col = mix(land_col, vegetated_col, humidity * temperate * (0.60 + pressure_factor * 0.20));
    if (surface_type < 0.5) {
        float steep = smoothstep(0.10, 0.58, slope);
        float flats = 1.0 - steep;
        vec3 dust_tone = mix(vec3(0.52, 0.42, 0.28), vec3(0.72, 0.62, 0.45), clamp(elev + dryness * 0.3, 0.0, 1.0));
        land_col = mix(land_col, dust_tone, flats * dryness * 0.40);
        land_col = mix(land_col, land_col * 0.58, steep * 0.58);
    }
    if (earth_like_world) {
        float biome_temp = clamp((temperature - 170.0) / 220.0 + elev * 0.12 - abs(normal.y) * 0.18, 0.0, 1.0);
        float biome_humidity = clamp(humidity * 0.72 + river_density * 0.35 +
                                     (1.0 - dryness) * 0.24 + valley_mask * 0.18, 0.0, 1.0);
        vec3 biome_col = whittaker_biome(biome_temp, biome_humidity);
        land_col = mix(land_col, biome_col, land_mask * (0.45 + 0.18 * temperate));
    }
    land_col = clamp(land_col + palette_shift * 0.08, 0.0, 1.0);
    col = mix(col, land_col, land_mask * (0.60 + 0.30 * continent_cov + 0.10 * island_cov));

    if (surface_type > 1.5 && surface_type < 2.5)
        col = mix(col, ice_col, 0.74 + ice_frac * 0.18);
    else if (surface_type > 3.5)
        col = mix(col, land_col, 0.18 + humidity * 0.10);

    if (temperature < 240.0)
        col = mix(col, ice_col, smoothstep(0.55, 0.92, abs(sample_normal.y)) * clamp(ice_frac + 0.25, 0.0, 1.0));

    float ice_sheet_mask = max(
        smoothstep(0.40 - ice_sheet_cov * 0.14, 0.98, abs(sample_normal.y)),
        smoothstep(0.76, 0.95, elev) * (0.35 + ice_sheet_cov * 0.65));
    col = mix(col, ice_col, ice_sheet_mask * clamp(ice_sheet_cov + (temperature < 245.0 ? 0.25 : 0.0), 0.0, 1.0));

    bool is_ocean = ocean_cov > 0.01 && elev < sea_level;
    float coast = smoothstep(sea_level + 0.01, sea_level + 0.09, elev) * land_mask;
    col = mix(col, vec3(0.90, 0.82, 0.62), coast * (0.30 + humidity * 0.28));
    roughness_out = hit.material_params.x;
    if (is_ocean) {
        float ocean_type = ocean_type_from_temp(temperature);
        vec2 wave_uv = vec2(lon, lat * 1.4 + sin(lon * 2.0) * 0.08);
        float wave_time = screen_info.w * (0.40 + hit.activity_params.x * 0.65);
        vec3 waves = gerstner_lobes(wave_uv, wave_time, seed, water_world ? 0.034 : 0.014);
        float wave_height = waves.x;
        float depth = smoothstep(sea_level + wave_height, sea_level - 0.25, elev);
        float shallows = 1.0 - smoothstep(sea_level - 0.05, sea_level - 0.18, elev);
        vec3 ocean_col;
        if (ocean_type < 1.5) {
            ocean_col = mix(vec3(0.02, 0.08, 0.24), vec3(0.08, 0.34, 0.62), 1.0 - depth);
            ocean_col = mix(ocean_col, vec3(0.24, 0.70, 0.84), shallows * 0.42);
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
        if (water_world) {
            float abyss = smoothstep(0.25, 1.0, depth);
            ocean_col = mix(ocean_col, vec3(0.01, 0.05, 0.13), abyss * 0.55);
        }
        vec3 wave_normal = normalize(normal + tangent * waves.y * 0.08 + bitangent * waves.z * 0.08);
        surf_normal = mix(surf_normal, wave_normal, water_world ? 0.92 : 0.68);
        roughness_out = min(roughness_out, water_world ? 0.07 : 0.10);
        col = mix(col, ocean_col, depth);
    }

    if (!is_ocean && river_density > 0.02) {
        vec3 river_np = vec3(sample_normal.x * 18.0, sample_normal.y * 8.0, sample_normal.z * 18.0) +
                        seed_offset * 0.06 + warp * 0.9;
        float river_field = ridged_fbm(river_np + vec3(screen_info.w * 0.003, 0.0, -screen_info.w * 0.002), 4);
        float river_pref = smoothstep(sea_level + 0.02, sea_level + 0.28, elev) * land_mask * (1.0 - mountain_mask * 0.55);
        float river_mask = smoothstep(0.74 - river_density * 0.30, 0.92, river_field) * river_pref;
        col = mix(col, vec3(0.08, 0.20, 0.36), river_mask * 0.78);
        roughness_out = mix(roughness_out, 0.20, river_mask * 0.45);
    }

    if (volcanic > 0.01 && temperature > 650.0 && surface_type < 0.5) {
        float cracks = smoothstep(0.46, 0.54, fbm(warped_np * 0.7 + 4.0, 4));
        float lava_glow = 0.65 + 0.35 * sin(screen_info.w * 1.3 + seed * 0.1 + cracks * 5.0);
        col = mix(col, vec3(1.0, 0.42, 0.08) * lava_glow, cracks * volcanic * 0.7);
    }

    if (cloud_cov > 0.01 && surface_type < 3.5) {
        float cloud_phase = screen_info.w * (0.06 + hit.activity_params.x * 0.22 + hash11(seed * 0.071) * 0.08);
        vec3 cloud_np = vec3(lon * 7.5 + cloud_phase,
                             lat * 11.0 + sin(cloud_phase + lon * 2.0) * 0.8,
                             seed * 0.19);
        float cloud_shape = smoothstep(0.42 - cloud_cov * 0.24, 0.74, fbm(cloud_np, 5));
        float cloud_detail = smoothstep(0.52, 0.86, fbm(cloud_np * vec3(1.9, 1.4, 1.0) + vec3(3.2, 1.1, 0.7), 4));
        float cloud_mask = cloud_shape * mix(0.65, 1.0, cloud_detail);
        float shadow = smoothstep(0.52, 0.86, fbm(cloud_np + vec3(2.1, 0.7, 1.4), 5));
        float cloud_lit = 0.35 + 0.65 * max(dot(normal, light_dir), 0.0);
        vec3 cloud_col = mix(vec3(0.84, 0.88, 0.94), vec3(0.98, 0.99, 1.0), cloud_lit);
        col = mix(col, cloud_col, cloud_mask * cloud_cov * (0.64 + 0.24 * cloud_lit));
        col *= 1.0 - shadow * cloud_cov * 0.18;
    }

    if (phase_kind > float(PHASE_ICE) - 0.5 && phase_kind < float(PHASE_ICE) + 0.5) {
        col = mix(col, ice_col, phase_intensity * 0.42);
    } else if (phase_kind > float(PHASE_GAS) - 0.5 && phase_kind < float(PHASE_GAS) + 0.5 &&
               surface_type < 2.5) {
        col = mix(col, mix(col, vec3(0.78, 0.72, 0.64), 0.55), phase_intensity * 0.22);
    } else if (phase_kind > float(PHASE_MOLTEN) - 0.5 && phase_kind < float(PHASE_MOLTEN) + 0.5) {
        vec3 molten_col = mix(vec3(0.38, 0.08, 0.02), vec3(1.00, 0.52, 0.08),
                              clamp((temperature - 900.0) / 1000.0, 0.0, 1.0));
        col = mix(col, molten_col, phase_intensity * (0.30 + 0.32 * (1.0 - land_mask)));
    } else if (phase_kind > float(PHASE_PLASMA) - 0.5) {
        vec3 plasma_col = mix(vec3(1.00, 0.58, 0.12), vec3(1.00, 0.92, 0.72),
                              clamp((temperature - 2000.0) / 12000.0, 0.0, 1.0));
        col = mix(col, plasma_col, phase_intensity * 0.60);
    }
    if (collapse_phase > 0.01) {
        col = mix(col, col * 0.70 + vec3(0.30, 0.14, 0.06), collapse_phase * 0.28);
    }

    if (frozen_world) {
        float backlit = pow(clamp(dot(-light_dir, surf_normal), 0.0, 1.0), 1.4);
        float edge = pow(clamp(1.0 - max(dot(surf_normal, -rd), 0.0), 0.0, 1.0), 1.9);
        float sss = backlit * edge * (0.12 + 0.42 * clamp(ice_frac + ice_sheet_cov * 0.45, 0.0, 1.0));
        col += vec3(0.18, 0.28, 0.38) * sss;
        float sparkle = pow(white_noise(warped_np * 170.0 + vec3(screen_info.w * 0.16)), 26.0);
        col += vec3(0.70, 0.80, 0.95) * sparkle * (0.05 + 0.08 * ice_frac);
    }

    if (hit.impact_params.x > 0.001) {
        float basin;
        float rim;
        float ejecta;
        impact_masks(sample_normal, hit, basin, rim, ejecta);
        vec3 ejecta_col = mix(vec3(0.58, 0.54, 0.50), vec3(0.90, 0.84, 0.74), clamp(ice_frac + 0.2, 0.0, 1.0));
        vec3 melt_col = mix(vec3(0.44, 0.10, 0.03), vec3(1.00, 0.62, 0.14), clamp((temperature - 850.0) / 1800.0 + impact_heat * 0.8, 0.0, 1.0));
        col = mix(col, col * 0.42, basin * (0.55 + hit.impact_params.x * 0.25));
        col = mix(col, ejecta_col, ejecta * (0.18 + hit.impact_params.w * 0.38));
        col = mix(col, vec3(0.94, 0.86, 0.72), rim * 0.28);
        col += melt_col * basin * impact_heat * (0.25 + 0.45 * (1.0 - ocean_cov));
        roughness_out = mix(roughness_out, 0.92, ejecta * 0.35 + basin * 0.18);
    }

    float ndl = max(dot(surf_normal, light_dir), 0.0);
    return col * (0.25 + 0.75 * ndl);
}

vec3 shade_moon_surface(vec3 normal, Sphere hit, vec3 rd, vec3 light_dir,
                        out float roughness_out, out vec3 surf_normal) {
    vec3 col = shade_planet_surface(normal, hit, rd, light_dir, roughness_out, surf_normal);
    float seed = hit.class_seed_temp.x;
    float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
    vec3 spin_axis = ring_axis(max(hit.ring_params.w, 0.02), seed);
    vec3 sample_normal = (abs(spin_phase) > 1.0e-8)
        ? rotate_about_axis(normal, spin_axis, spin_phase)
        : normal;
    float crater = crater_mask(sample_normal, seed, hit.terrain_params.w);
    vec3 regolith = mix(vec3(0.18, 0.18, 0.20), vec3(0.70, 0.72, 0.78), hit.composition_params.y);
    col = mix(col, regolith, crater * 0.55);
    col *= 0.92 - crater * 0.12;
    if (hit.composition_params.y > 0.35) {
        float fractures = ridged_fbm(sample_normal * 18.0 + hash31(seed) * 8.0, 4);
        col += vec3(0.10, 0.14, 0.18) * smoothstep(0.35, 0.65, fractures) * 0.35;
        vec2 ice_cells = voronoi_f1_f2(sample_normal * 16.0 + vec3(seed * 0.05));
        float crack_lines = smoothstep(0.02, 0.14, ice_cells.y - ice_cells.x);
        col += vec3(0.26, 0.34, 0.44) * crack_lines * 0.20;
        float ice_sss = pow(clamp(dot(-light_dir, surf_normal), 0.0, 1.0), 1.3) *
                        pow(clamp(1.0 - max(dot(surf_normal, -rd), 0.0), 0.0, 1.0), 2.0);
        col += vec3(0.14, 0.22, 0.30) * ice_sss * 0.16;
        roughness_out = max(0.22, roughness_out - 0.08);
    }
    if (hit.impact_params.x > 0.001) {
        float basin;
        float rim;
        float ejecta;
        impact_masks(sample_normal, hit, basin, rim, ejecta);
        col = mix(col, col * 0.32, basin * 0.42);
        col = mix(col, vec3(0.84, 0.82, 0.78), ejecta * 0.22 + rim * 0.16);
    }
    return col;
}

vec3 shade_asteroid_surface(vec3 normal, Sphere hit, vec3 light_dir, bool is_comet_body, out float roughness_out) {
    float seed = hit.class_seed_temp.x;
    float subtype = hit.class_seed_temp.z;
    float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
    vec3 spin_axis = ring_axis(max(hit.ring_params.w, 0.02), seed);
    vec3 sample_normal = (abs(spin_phase) > 1.0e-8)
        ? rotate_about_axis(normal, spin_axis, spin_phase)
        : normal;
    float crater = crater_mask(sample_normal, seed, hit.terrain_params.w);
    vec3 rubble_np = sample_normal * (6.0 + hit.terrain_params.y * 1.4) + hash31(seed) * 5.0;
    float simplex = simplex3D(rubble_np * 1.6 + vec3(seed * 0.09)) * 0.5 + 0.5;
    float rigid = rigid_multifractal(rubble_np * 1.3 + vec3(seed * 0.07), 4);
    vec2 vor = voronoi_f1_f2(rubble_np * 2.5 + vec3(seed * 0.11));
    float shard_faces = smoothstep(0.03, 0.16, vor.y - vor.x);
    float shattered = smoothstep(0.20, 0.64, 1.0 - vor.x);
    float chip = clamp(simplex * 0.46 + rigid * 0.42 + shard_faces * 0.28 - shattered * 0.18, 0.0, 1.0);
    float jagged = smoothstep(0.48, 0.88, rigid);
    vec3 col;

    if (is_comet_body) {
        col = mix(vec3(0.10, 0.09, 0.08), vec3(0.72, 0.78, 0.86), smoothstep(0.45, 0.9, chip) * hit.composition_params.y);
        col = mix(col, vec3(0.20, 0.18, 0.16), crater * 0.35);
        col = mix(col, vec3(0.84, 0.90, 0.95), shard_faces * hit.composition_params.y * 0.24);
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
    col = mix(col, col * 0.54, jagged * 0.48);
    col = mix(col, col + vec3(0.10, 0.09, 0.08), shard_faces * 0.22);
    col = mix(col, col * 0.55, crater * 0.5);
    float weathering = 0.5 + 0.5 * dot(sample_normal, normalize(vec3(0.7, 0.2, -0.4)));
    col *= mix(0.88, 1.06, weathering);
    if (hit.impact_params.x > 0.001) {
        float basin;
        float rim;
        float ejecta;
        impact_masks(sample_normal, hit, basin, rim, ejecta);
        vec3 hot = mix(vec3(0.40, 0.10, 0.03), vec3(1.00, 0.62, 0.18), clamp(hit.impact_params.y * 1.2, 0.0, 1.0));
        col = mix(col, col * 0.34, basin * 0.55);
        col += hot * basin * hit.impact_params.y * 0.35;
        col = mix(col, vec3(0.78, 0.74, 0.68), ejecta * 0.20 + rim * 0.15);
    }
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

vec3 black_hole_effect(vec3 ro, vec3 rd, Sphere bh, int body_count, bool hit_horizon, out float alpha_out) {
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
    vec3 fabric_col = vec3(0.0);
    float fabric_alpha = 0.0;
    float fabric_t = -1.0;
    if (sample_space_fabric(ro, lensed_dir, body_count, fabric_col, fabric_alpha, fabric_t))
        col = mix(col, col + fabric_col, fabric_alpha);
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
        alpha_out = 1.0;
        return vec3(0.0);
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
            float spin_phase = spin_phase_from_rate(spheres[i].impact_axis.w);
            vec3 spin_axis = ring_axis(max(spheres[i].ring_params.w, 0.02), spheres[i].class_seed_temp.x);
            t = intersect_irregular_body(ro, rd, spheres[i].pos_radius.xyz, spheres[i].pos_radius.w,
                                         spheres[i].class_seed_temp.x, roughness, spin_phase, spin_axis);
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
        vec3 fabric_col = vec3(0.0);
        float fabric_alpha = 0.0;
        float fabric_t = -1.0;
        if (sample_space_fabric(ro, rd, body_count, fabric_col, fabric_alpha, fabric_t))
            miss_col = mix(miss_col, miss_col + fabric_col, fabric_alpha);
        vec3 ring_col = vec3(0.0);
        float ring_alpha = 0.0;
        accumulate_rings(ro, rd, body_count, -1.0, ring_col, ring_alpha);
        if (ring_alpha > 0.0)
            miss_col = mix(miss_col, miss_col + ring_col, ring_alpha);
        vec3 magnet_col = vec3(0.0);
        float magnet_alpha = 0.0;
        accumulate_magnetospheres(ro, rd, body_count, -1.0, magnet_col, magnet_alpha);
        if (magnet_alpha > 0.0)
            miss_col = mix(miss_col, miss_col + magnet_col, magnet_alpha);
        for (int i = 0; i < body_count && i < 512; i++) {
            if (int(spheres[i].class_seed_temp.y + 0.5) != RENDER_BLACK_HOLE) continue;
            float effect_alpha = 0.0;
            vec3 effect = black_hole_effect(ro, rd, spheres[i], body_count, false, effect_alpha);
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
        float spin_phase = spin_phase_from_rate(hit.impact_axis.w);
        vec3 spin_axis = ring_axis(max(hit.ring_params.w, 0.02), hit.class_seed_temp.x);
        normal = irregular_normal(hit_pos, hit.pos_radius.xyz, hit.pos_radius.w,
                                  hit.class_seed_temp.x, irregularity, spin_phase, spin_axis);
    }

    if (render_class == RENDER_BLACK_HOLE || hit.base_emit.a < -0.5) {
        float alpha = 1.0;
        vec3 bh = black_hole_effect(ro, rd, hit, body_count, true, alpha);
        vec3 ring_col = vec3(0.0);
        float ring_alpha = 0.0;
        accumulate_rings(ro, rd, body_count, closest_t, ring_col, ring_alpha);
        if (ring_alpha > 0.0)
            bh = mix(bh, bh + ring_col, ring_alpha);
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
        vec3 ring_col = vec3(0.0);
        float ring_alpha = 0.0;
        accumulate_rings(ro, rd, body_count, closest_t, ring_col, ring_alpha);
        if (ring_alpha > 0.0)
            star_col = mix(star_col, star_col + ring_col, ring_alpha);
        vec3 magnet_col = vec3(0.0);
        float magnet_alpha = 0.0;
        accumulate_magnetospheres(ro, rd, body_count, closest_t, magnet_col, magnet_alpha);
        if (magnet_alpha > 0.0)
            star_col = mix(star_col, star_col + magnet_col, magnet_alpha);
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
        vec3 axis = magnetic_axis(hit.magnetosphere_params.z, hit.class_seed_temp.x);
        float storm = hit.gravity_params.y;
        float retention = hit.gravity_params.z;
        float aurora_base = hit.activity_params.y * retention;
        if (aurora_base > 0.001) {
            float pole = pow(clamp(abs(dot(normal, axis)), 0.0, 1.0), 5.0);
            float view_edge = pow(clamp(1.0 - max(dot(normal, -rd), 0.0), 0.0, 1.0), 2.2);
            float aurora_phase = screen_info.w * (0.70 + storm * 2.10) + hit.class_seed_temp.x * 0.27;
            float aurora_arc = smoothstep(0.52, 0.84,
                fbm(vec3(atan(normal.z, normal.x) * 12.0 + aurora_phase * 1.4,
                         abs(dot(normal, axis)) * 19.0 - aurora_phase * 0.9,
                         hit.class_seed_temp.x * 0.19), 4));
            vec3 aurora_col = mix(vec3(0.18, 1.0, 0.56), vec3(0.54, 0.90, 1.0),
                                  step(2.5, hit.class_seed_temp.z));
            final_color += aurora_col * aurora_base * pole * view_edge * aurora_arc * (0.20 + storm * 0.45);
        }
    }

    if (render_class == RENDER_PLANET || render_class == RENDER_MOON ||
        render_class == RENDER_ASTEROID || render_class == RENDER_COMET) {
        float phase_kind = hit.phase_params.x;
        float phase_intensity = hit.phase_params.y;
        vec3 emission = phase_emission_tint(phase_kind, phase_intensity, hit.class_seed_temp.w, base_color);
        if ((render_class == RENDER_PLANET || render_class == RENDER_MOON) && hit.impact_params.y > 0.001) {
            float basin;
            float rim;
            float ejecta;
            impact_masks(normal, hit, basin, rim, ejecta);
            vec3 impact_emission = mix(vec3(0.95, 0.30, 0.08), vec3(1.00, 0.72, 0.24),
                                       clamp(hit.impact_params.y * 1.2, 0.0, 1.0));
            emission += impact_emission * basin * hit.impact_params.y * (0.35 + 0.45 * hit.impact_params.x);
        }
        if (dot(emission, emission) > 0.0) {
            float edge_glow = 0.35 + 0.65 * pow(clamp(1.0 - max(dot(normal, -rd), 0.0), 0.0, 1.0), 1.35);
            final_color += emission * edge_glow;
        }
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

    vec3 fabric_col = vec3(0.0);
    float fabric_alpha = 0.0;
    float fabric_t = -1.0;
    if (sample_space_fabric(ro, rd, body_count, fabric_col, fabric_alpha, fabric_t)) {
        float overlay = fabric_alpha * 0.75;
        if (fabric_t > closest_t)
            overlay *= 0.85;
        final_color = mix(final_color, final_color + fabric_col, overlay);
    }

    vec3 ring_col = vec3(0.0);
    float ring_alpha = 0.0;
    accumulate_rings(ro, rd, body_count, closest_t, ring_col, ring_alpha);
    if (ring_alpha > 0.0)
        final_color = mix(final_color, final_color + ring_col, ring_alpha);

    vec3 magnet_col = vec3(0.0);
    float magnet_alpha = 0.0;
    accumulate_magnetospheres(ro, rd, body_count, closest_t, magnet_col, magnet_alpha);
    if (magnet_alpha > 0.0)
        final_color = mix(final_color, final_color + magnet_col, magnet_alpha);

    outColor = vec4(pow(max(final_color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
