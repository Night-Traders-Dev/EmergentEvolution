#version 450

layout(location = 0) in vec2      v_local;       // [-1,1] quad space
layout(location = 1) in flat uint  v_fo_type;
layout(location = 2) in flat float v_fo_strength;
layout(location = 3) in flat float v_core_radius;
layout(location = 4) in flat float v_aura_radius;
layout(location = 5) in flat float v_glow_radius;
layout(location = 6) in flat float v_core_w;

layout(location = 0) out vec4 outColor;

void main() {
    // ── Mirror rendering ────────────────────────────────────────────────────
    if (v_fo_type == 5u) {
        float across = abs(v_local.y) * v_glow_radius;
        float along  = abs(v_local.x);

        // Fade at segment endpoints
        float end_fade = 1.0 - smoothstep(0.85, 1.0, along);

        vec3 mirror_color = vec3(0.6, 0.65, 0.85);

        if (across < v_core_w) {
            float inner = 1.0 - across / v_core_w;
            vec3 col = mix(mirror_color * 0.9, vec3(1.0), inner * inner * 0.6);
            outColor = vec4(col * end_fade, 0.9 * end_fade);
        } else {
            float glow_t = (across - v_core_w) / (v_glow_radius - v_core_w);
            float glow = (1.0 - glow_t) * (1.0 - glow_t);
            vec3 glow_col = mirror_color * glow * 0.25;
            outColor = vec4(glow_col * end_fade, glow * 0.4 * end_fade);
        }
        return;
    }

    // ── Circle force object rendering ───────────────────────────────────────
    float d = length(v_local) * v_aura_radius; // distance from center in pixels
    if (d > v_aura_radius) discard;

    // Per-type colors (must match compute shader render_force_object)
    vec3 aura_color, core_color;
    if (v_fo_type == 0u) {
        aura_color = vec3(0.3, 0.5, 1.0);   core_color = vec3(0.4, 0.6, 1.0);   // EM: blue
    } else if (v_fo_type == 1u) {
        aura_color = vec3(0.1, 0.8, 0.3);   core_color = vec3(0.2, 0.9, 0.4);   // Strong: green
    } else if (v_fo_type == 2u) {
        aura_color = vec3(0.6, 0.2, 0.9);   core_color = vec3(0.7, 0.3, 1.0);   // Weak: purple
    } else if (v_fo_type == 3u) {
        aura_color = vec3(0.8, 0.5, 0.2);   core_color = vec3(0.9, 0.6, 0.3);   // Gravity: amber
    } else if (v_fo_type == 6u) {
        aura_color = vec3(0.9, 0.3, 0.3);   core_color = vec3(1.0, 0.4, 0.3);   // Coulomb: red
    } else if (v_fo_type == 7u) {
        aura_color = vec3(0.2, 0.8, 0.9);   core_color = vec3(0.3, 0.9, 1.0);   // Vortex: cyan
    } else if (v_fo_type == 8u) {
        aura_color = vec3(0.9, 0.9, 0.2);   core_color = vec3(1.0, 1.0, 0.3);   // Well: yellow
    } else {
        aura_color = vec3(1.0, 0.3, 0.1);   core_color = vec3(1.0, 0.5, 0.2);   // Heat: red-orange
    }

    if (d < v_core_radius) {
        // Solid core with inner glow
        float inner = 1.0 - d / v_core_radius;
        inner *= inner;
        vec3 col = mix(core_color * 0.8, vec3(1.0), inner * 0.5);
        float edge = 1.0 - smoothstep(v_core_radius * 0.65, v_core_radius, d);
        col *= edge;

        // Specular highlight (offset toward upper-right)
        vec2 hi_off = v_local * v_aura_radius + vec2(v_core_radius * 0.25);
        float hi_d = length(hi_off);
        if (hi_d < v_core_radius * 0.3) {
            float hi = 1.0 - hi_d / (v_core_radius * 0.3);
            col = mix(col, vec3(1.0), hi * hi * 0.5);
        }
        outColor = vec4(col, edge);
    } else {
        // Aura glow
        float t = (d - v_core_radius) / (v_aura_radius - v_core_radius);
        float glow = (1.0 - t) * (1.0 - t);
        vec3 glow_col = aura_color * glow * 0.4 * v_fo_strength;
        outColor = vec4(glow_col, glow * 0.5);
    }
}
