#version 450
// EFB copies (vk_backend.c): the EFB rectangle converted to the copy
// format, stored as the texture reads back, as gl_backend.c's copy shader
// does. q.mode: copy format, intensity, EFB has alpha.

layout(set = 0, binding = 0) uniform sampler2D textures[16384];
layout(push_constant) uniform Quad { vec4 src; vec4 color; ivec4 mode; float depth; uint tex; } q;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

float bits(float v, int n) {
    float i = float(int(v * 255.0 + 0.5) >> (8 - n));
    return floor(i * 255.0 / float((1 << n) - 1) + 0.5) / 255.0;
}

void main() {
    // Only the EFB's pixels wholly inside the rectangle are read: at a scale
    // that isn't whole, filtering at its edges would take in the pixels past
    // them, which a copy of part of the EFB (the glow's) must not show.
    vec2 ts = vec2(textureSize(textures[q.tex], 0));
    vec2 lo = (ceil(q.src.xy * ts - 0.01) + 0.5) / ts, hi = (floor(q.src.zw * ts + 0.01) - 0.5) / ts;
    vec4 c = texture(textures[q.tex], clamp(v_uv, lo, max(lo, hi)));
    int f = q.mode.x;
    float i;
    if (q.mode.z == 0) c.a = 1.0;
    // The intensity formats store luma (BT.601, studio range).
    i = q.mode.y != 0 ? clamp(dot(c.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0, 0.0, 1.0) : c.r;
    if (f == 0) o_color = vec4(bits(i, 4));
    else if (f == 1 || f == 8) o_color = vec4(i);
    else if (f == 2) o_color = vec4(vec3(bits(i, 4)), bits(c.a, 4));
    else if (f == 3) o_color = vec4(vec3(i), c.a);
    else if (f == 4) o_color = vec4(bits(c.r, 5), bits(c.g, 6), bits(c.b, 5), 1.0);
    else if (f == 5 && c.a >= 224.0 / 255.0) o_color = vec4(bits(c.r, 5), bits(c.g, 5), bits(c.b, 5), 1.0);
    else if (f == 5) o_color = vec4(bits(c.r, 4), bits(c.g, 4), bits(c.b, 4), bits(c.a, 3));
    else if (f == 7) o_color = vec4(c.a);
    else if (f == 9) o_color = vec4(c.g);
    else if (f == 10) o_color = vec4(c.b);
    else if (f == 11) o_color = vec4(vec3(c.g), c.r); // IA8 layout: A = R, I = G
    else if (f == 12) o_color = vec4(vec3(c.b), c.g);
    else o_color = c;
}
