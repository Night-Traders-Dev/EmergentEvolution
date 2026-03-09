#version 450

// ── Push constants (128 bytes) ──────────────────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 mvp;             // 64 bytes — model-view-projection
    vec4 cam_pos_time;    // 16 bytes — xyz=camera pos (camera-relative), w=time
    vec4 body_params;     // 16 bytes — x=render_class, y=temperature, z=ocean_level, w=radius
    vec4 color_params;    // 16 bytes — rgb=base color, a=emissive
    vec4 surface_params;  // 16 bytes — x=terrain_amp, y=ice_coverage, z=rock_frac, w=ocean_frac
};

// ── Vertex inputs ───────────────────────────────────────────────────────────
layout(location = 0) in vec3 in_position;  // body-relative displaced position
layout(location = 1) in vec3 in_normal;    // unit sphere normal
layout(location = 2) in vec2 in_uv;        // texture coordinates

// ── Outputs to fragment shader ──────────────────────────────────────────────
layout(location = 0) out vec3 frag_normal;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_pos;    // body-relative position (for lighting)
layout(location = 3) out float frag_height; // displacement from sphere surface

void main() {
    frag_pos    = in_position;
    frag_normal = in_normal;
    frag_uv     = in_uv;

    // Height = distance from body center (at origin) minus nominal radius
    frag_height = length(in_position) - body_params.w;

    gl_Position = mvp * vec4(in_position, 1.0);
}
