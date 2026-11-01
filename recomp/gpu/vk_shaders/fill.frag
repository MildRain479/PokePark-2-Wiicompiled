#version 450
// Clears: one color (vk_backend.c).

layout(push_constant) uniform Quad { vec4 src; vec4 color; ivec4 mode; float depth; uint tex; } q;
layout(location = 0) out vec4 o_color;

void main() { o_color = q.color; }
