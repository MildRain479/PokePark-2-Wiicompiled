#version 450
// A triangle covering the viewport, for vk_backend.c's clears, EFB copies
// and presenting: v_uv runs from q.src.xy at the top left to q.src.zw at
// the bottom right; the depth is q.depth.

layout(push_constant) uniform Quad { vec4 src; vec4 color; ivec4 mode; float depth; uint tex; } q;
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = vec2((gl_VertexIndex & 1) * 2, (gl_VertexIndex >> 1) * 2);
    v_uv = mix(q.src.xy, q.src.zw, p);
    gl_Position = vec4(p * 2.0 - 1.0, q.depth, 1.0);
}
