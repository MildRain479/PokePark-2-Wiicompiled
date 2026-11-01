#version 450
// A texture drawn as it is: the EFB into the window, the settings menu
// over it (vk_backend.c).

layout(set = 0, binding = 0) uniform sampler2D textures[16384];
layout(push_constant) uniform Quad { vec4 src; vec4 color; ivec4 mode; float depth; uint tex; } q;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() { o_color = texture(textures[q.tex], v_uv); }
