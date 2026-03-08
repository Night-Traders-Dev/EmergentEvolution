#version 450

// Push constants — camera/viewport data
layout(push_constant) uniform OverlayPC {
    vec2  region_size;        // render texture logical size (includes padding)
    vec2  camera_origin;      // world-space camera center
    float camera_zoom;        // render-space zoom (includes rscale)
    float particle_radius;    // base particle radius
    uint  force_object_count;
} pc;

// Force object buffer (same struct as compute shader)
struct ForceObj {
    float x, y;
    float strength;
    float radius;
    uint  force_type;
    uint  is_active;
    float end_x;    // mirror endpoint B x (stored in _pad0)
    float end_y;    // mirror endpoint B y (stored in _pad1)
};
layout(set = 0, binding = 0, std430) readonly buffer ForceObjectBuffer {
    ForceObj data[];
} force_objects;

layout(location = 0) out vec2      v_local;       // [-1,1] quad space
layout(location = 1) out flat uint  v_fo_type;
layout(location = 2) out flat float v_fo_strength;
layout(location = 3) out flat float v_core_radius; // in UV-normalized space
layout(location = 4) out flat float v_aura_radius; // in UV-normalized space
layout(location = 5) out flat float v_glow_radius; // mirror glow (normalized)
layout(location = 6) out flat float v_core_w;      // mirror core width (normalized)

void main() {
    uint fo_idx = gl_InstanceIndex;
    uint vert   = gl_VertexIndex;

    ForceObj fo = force_objects.data[fo_idx];
    v_fo_type     = fo.force_type;
    v_fo_strength = fo.strength;

    // Quad corners: triangle strip (0=TL, 1=TR, 2=BL, 3=BR)
    vec2 offsets[4] = vec2[4](
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0)
    );
    v_local = offsets[vert];

    // Discard inactive force objects by placing off-screen
    if (fo.is_active == 0u) {
        gl_Position = vec4(-10.0, -10.0, 0.0, 1.0);
        return;
    }

    vec2 screen_center = pc.region_size * 0.5;

    if (fo.force_type == 5u) {
        // Mirror: line segment from (x,y) to (end_x,end_y)
        vec2 sa = screen_center + (vec2(fo.x, fo.y) - pc.camera_origin) * pc.camera_zoom;
        vec2 sb = screen_center + (vec2(fo.end_x, fo.end_y) - pc.camera_origin) * pc.camera_zoom;

        float glow_r = max(4.0, 2.0 * pc.camera_zoom);
        float core_w = max(1.5, 0.8 * pc.camera_zoom);
        v_glow_radius = glow_r;
        v_core_w = core_w;
        v_core_radius = 0.0;
        v_aura_radius = 0.0;

        // Build oriented quad along the line segment
        vec2 mid = (sa + sb) * 0.5;
        vec2 seg = sb - sa;
        float seg_len = length(seg);
        if (seg_len < 1.0) seg_len = 1.0;
        vec2 tangent = seg / seg_len;
        vec2 normal = vec2(-tangent.y, tangent.x);

        float half_len = seg_len * 0.5 + glow_r;
        vec2 pos_px = mid + tangent * (v_local.x * half_len) + normal * (v_local.y * glow_r);

        // Convert to NDC: pixel / region_size * 2 - 1
        gl_Position = vec4(pos_px / pc.region_size * 2.0 - 1.0, 0.0, 1.0);

        // Remap v_local to segment-relative coordinates
        // x: along segment [-1,1], y: across segment [-1,1]
        vec2 rel = pos_px - mid;
        v_local = vec2(dot(rel, tangent) / half_len, dot(rel, normal) / glow_r);
    } else {
        // Circle force object
        float core_radius = pc.particle_radius * pc.camera_zoom * 0.7;
        float aura_radius = min(core_radius * (3.0 + fo.strength * 1.5), 40.0);
        v_core_radius = core_radius;
        v_aura_radius = aura_radius;
        v_glow_radius = 0.0;
        v_core_w = 0.0;

        vec2 center = screen_center + (vec2(fo.x, fo.y) - pc.camera_origin) * pc.camera_zoom;
        vec2 pos_px = center + v_local * aura_radius;

        gl_Position = vec4(pos_px / pc.region_size * 2.0 - 1.0, 0.0, 1.0);
    }
}
