#version 450

// ── Push constants (must match vertex shader) ───────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 cam_pos_time;    // xyz=camera pos (camera-relative), w=time
    vec4 body_params;     // x=render_class, y=temperature, z=ocean_level, w=radius
    vec4 color_params;    // rgb=base color, a=emissive
    vec4 surface_params;  // x=terrain_amp, y=ice_coverage, z=rock_frac, w=ocean_frac
};

// ── Inputs from vertex shader ───────────────────────────────────────────────
layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_pos;
layout(location = 3) in float frag_height;

layout(location = 0) out vec4 out_color;

// ── Render class constants (must match BodyRenderClass enum) ────────────────
const int RC_STAR      = 0;
const int RC_PLANET    = 1;
const int RC_MOON      = 2;
const int RC_ASTEROID  = 3;
const int RC_COMET     = 4;
const int RC_BLACK_HOLE = 5;

// ── Helpers ─────────────────────────────────────────────────────────────────

// Simple hash for per-vertex variation
float hash31(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

void main() {
    vec3 N = normalize(frag_normal);
    vec3 V = normalize(cam_pos_time.xyz - frag_pos);

    float radius      = body_params.w;
    float terrain_amp = surface_params.x;
    float ice_cov     = surface_params.y;
    float rock_frac   = surface_params.z;
    float ocean_frac  = surface_params.w;
    float temperature = body_params.y;
    float ocean_level = body_params.z;
    int   rc          = int(body_params.x + 0.5);

    // Normalized height [-1, 1] relative to terrain amplitude
    float h = clamp(frag_height / max(radius * terrain_amp, 0.001), -1.0, 1.0);

    // ── Lighting ────────────────────────────────────────────────────────────
    float NdotL   = max(dot(N, V), 0.0);
    float ambient = 0.10;
    float diffuse = NdotL * 0.80;

    // Rim light for atmosphere-like glow
    float rim = pow(1.0 - max(dot(N, V), 0.0), 4.0) * 0.12;

    // Subtle specular
    vec3 H = normalize(V + V); // half-vector (light ≈ camera direction)
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.15;

    // ── Surface coloring ────────────────────────────────────────────────────
    vec3 color = color_params.rgb;

    if (rc == RC_PLANET || rc == RC_MOON) {
        // Biome palette
        vec3 deep_ocean    = vec3(0.015, 0.06, 0.20);
        vec3 shallow_water = vec3(0.04, 0.18, 0.40);
        vec3 beach         = vec3(0.72, 0.66, 0.48);
        vec3 lowland       = mix(vec3(0.18, 0.38, 0.12), vec3(0.42, 0.36, 0.22), rock_frac);
        vec3 highland      = mix(vec3(0.52, 0.47, 0.38), vec3(0.38, 0.33, 0.28), rock_frac);
        vec3 mountain      = vec3(0.48, 0.46, 0.43);
        vec3 snow          = vec3(0.90, 0.92, 0.95);

        // Hot / barren worlds — no vegetation
        if (temperature > 400.0) {
            lowland  = vec3(0.52, 0.38, 0.22);
            highland = vec3(0.62, 0.48, 0.32);
            beach    = vec3(0.58, 0.50, 0.35);
        }
        // Very cold / airless — greyish rock
        if (temperature < 120.0 || ocean_frac < 0.01) {
            lowland  = mix(vec3(0.35, 0.33, 0.30), lowland, ocean_frac);
            highland = mix(vec3(0.45, 0.43, 0.40), highland, ocean_frac);
        }

        // Height-based biome selection
        if (h < -0.35 && ocean_frac > 0.05) {
            color = deep_ocean;
        } else if (h < -0.08 && ocean_frac > 0.05) {
            float t = (h + 0.35) / 0.27;
            color = mix(deep_ocean, shallow_water, clamp(t, 0.0, 1.0));
        } else if (h < 0.0 && ocean_frac > 0.05) {
            float t = (h + 0.08) / 0.08;
            color = mix(shallow_water, beach, clamp(t, 0.0, 1.0));
        } else if (h < 0.25) {
            float t = max(h, 0.0) / 0.25;
            color = mix(lowland, highland, t);
        } else if (h < 0.6) {
            float t = (h - 0.25) / 0.35;
            color = mix(highland, mountain, t);
        } else {
            float t = clamp((h - 0.6) / 0.4, 0.0, 1.0);
            color = mix(mountain, snow, t);
        }

        // Per-vertex noise variation to break up uniform bands
        float noise = hash31(frag_pos * 50.0) * 0.06 - 0.03;
        color += noise;

        // Ice caps based on latitude (using sphere normal Y component)
        float lat = abs(N.y);
        if (lat > (1.0 - ice_cov) && ice_cov > 0.01) {
            float ice_blend = smoothstep(1.0 - ice_cov, 1.0 - ice_cov * 0.6, lat);
            color = mix(color, snow, ice_blend);
            spec *= 1.5; // ice is shinier
        }

        // Ocean specular highlight
        if (h < -0.05 && ocean_frac > 0.05) {
            spec *= 3.0;
        }
    }
    else if (rc == RC_STAR) {
        // Star surface: emissive with turbulence-driven brightness variation
        float turb = h * 15.0;
        float bright = 0.85 + turb * 0.15;
        color = color_params.rgb * max(bright, 0.3);
        out_color = vec4(color, 1.0);
        return;
    }
    else if (rc == RC_ASTEROID) {
        // Cratered grey-brown rock
        vec3 rock_base = vec3(0.40, 0.37, 0.32);
        vec3 rock_dark = vec3(0.22, 0.20, 0.18);
        float t = clamp(h * 0.5 + 0.5, 0.0, 1.0);
        color = mix(rock_dark, rock_base, t);
        color += hash31(frag_pos * 80.0) * 0.04 - 0.02;
    }

    // ── Final composition ───────────────────────────────────────────────────
    vec3 lit = color * (ambient + diffuse) + vec3(spec) + vec3(rim) * color;

    // Emissive bodies (stars already returned above)
    if (color_params.a > 0.5) {
        lit = color * (0.75 + color_params.a * 0.5);
    }

    out_color = vec4(lit, 1.0);
}
