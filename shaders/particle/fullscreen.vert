#version 450

layout(location = 0) out vec2 fragUV;

// Fullscreen triangle - no vertex buffer needed
void main() {
    // gl_VertexIndex: 0 → (-1,-1), 1 → (3,-1), 2 → (-1,3)
    vec2 uv     = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUV      = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
