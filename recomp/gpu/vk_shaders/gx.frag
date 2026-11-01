#version 450
// TEV, the GPU's color combiner, for vk_backend.c: as gl_backend.c's
// fragment shader, which says more. The TEV state is in the frame's data
// buffer at word pc.tev (vk_backend.c's tev_block): header (stages, alpha
// compare functions and logic), alpha references, registers, konst colors,
// swap tables, then per stage its color and alpha inputs and operations,
// texture map and selections, 24 words each. Textures are pc.tex[map] of
// all the device's textures.

layout(std430, set = 1, binding = 0) readonly buffer Data { uint words[]; };
layout(set = 0, binding = 0) uniform sampler2D textures[16384];
layout(push_constant) uniform Draw { uint xf, tev; uint tex[8]; } pc;

layout(location = 0) in vec4 v_col0;
layout(location = 1) in vec4 v_col1;
layout(location = 2) in vec3 v_tex[8];
layout(location = 0) out vec4 o_color;

ivec4 ti(uint w) {
    uint b = pc.tev + w;
    return ivec4(words[b], words[b + 1], words[b + 2], words[b + 3]);
}
vec4 tf(uint w) { return intBitsToFloat(ti(w)); }

ivec4 gSwap[4];
vec4 gKonst[4];

vec4 sample_map(int m, vec2 uv) { return texture(textures[pc.tex[m]], uv); }
vec4 swizzle(vec4 c, int t) {
    ivec4 s = gSwap[t];
    return vec4(c[s.x], c[s.y], c[s.z], c[s.w]);
}
float kfrac(int s) { return s < 8 ? float(8 - s) / 8.0 : 0.0; }
vec3 kcolor(int s) {
    if (s < 8) return vec3(kfrac(s));
    if (s >= 12 && s < 16) return gKonst[s - 12].rgb;
    if (s >= 16) {
        int k = (s - 16) & 3, c = (s - 16) >> 2;
        return vec3(gKonst[k][c]);
    }
    return vec3(0.0);
}
float kalpha(int s) {
    if (s < 8) return kfrac(s);
    if (s >= 16) {
        int k = (s - 16) & 3, c = (s - 16) >> 2;
        return gKonst[k][c];
    }
    return 0.0;
}
bool acmp(int f, float a, float r) {
    if (f == 0) return false;
    if (f == 1) return a < r;
    if (f == 2) return a == r;
    if (f == 3) return a <= r;
    if (f == 4) return a > r;
    if (f == 5) return a != r;
    if (f == 6) return a >= r;
    return true;
}
float q8(float x) { return floor(x * 255.0 + 0.5) / 255.0; }
vec3 q8(vec3 x) { return floor(x * 255.0 + 0.5) / 255.0; }

void main() {
    ivec4 head = ti(0);
    vec4 alpha_ref = tf(4);
    vec4 reg[4];
    for (int k = 0; k < 4; k++) {
        reg[k] = tf(8 + 4 * k);
        gKonst[k] = tf(24 + 4 * k);
        gSwap[k] = ti(40 + 4 * k);
    }
    vec4 last = reg[0];
    for (int i = 0; i < head.x; i++) {
        uint s = 56u + 24u * uint(i);
        ivec4 ci = ti(s), ai = ti(s + 4), co = ti(s + 8), ao = ti(s + 12), map = ti(s + 16), sel = ti(s + 20);
        vec4 tex = vec4(1.0);
        if (map.z != 0) {
            vec3 tc = v_tex[map.y];
            tex = sample_map(map.x, tc.xy / (tc.z == 0.0 ? 1.0 : tc.z));
        }
        tex = swizzle(tex, sel.w & 3);
        vec4 ras = map.w == 0 ? v_col0 : (map.w == 1 ? v_col1 : vec4(0.0));
        ras = swizzle(ras, sel.w >> 2);
        vec3 kc = kcolor(sel.z & 31);
        float ka = kalpha(sel.z >> 5);
        vec3 cin[16];
        cin[0] = reg[0].rgb; cin[1] = vec3(reg[0].a); cin[2] = reg[1].rgb; cin[3] = vec3(reg[1].a);
        cin[4] = reg[2].rgb; cin[5] = vec3(reg[2].a); cin[6] = reg[3].rgb; cin[7] = vec3(reg[3].a);
        cin[8] = tex.rgb; cin[9] = vec3(tex.a); cin[10] = ras.rgb; cin[11] = vec3(ras.a);
        cin[12] = vec3(1.0); cin[13] = vec3(0.5); cin[14] = kc; cin[15] = vec3(0.0);
        float ain[8];
        ain[0] = reg[0].a; ain[1] = reg[1].a; ain[2] = reg[2].a; ain[3] = reg[3].a;
        ain[4] = tex.a; ain[5] = ras.a; ain[6] = ka; ain[7] = 0.0;
        vec3 a = clamp(cin[ci.x], 0.0, 1.0), b = clamp(cin[ci.y], 0.0, 1.0), c = clamp(cin[ci.z], 0.0, 1.0), d = cin[ci.w];
        vec3 rc;
        if (co.y == 3) {
            vec3 r;
            if (co.z == 3) r = vec3(co.x == 0 ? greaterThan(q8(a), q8(b)) : equal(q8(a), q8(b)));
            else {
                float av, bv;
                if (co.z == 0) { av = q8(a.r); bv = q8(b.r); }
                else if (co.z == 1) { av = q8(a.r) + q8(a.g) * 256.0; bv = q8(b.r) + q8(b.g) * 256.0; }
                else { av = q8(a.r) + q8(a.g) * 256.0 + q8(a.b) * 65536.0; bv = q8(b.r) + q8(b.g) * 256.0 + q8(b.b) * 65536.0; }
                r = vec3(co.x == 0 ? float(av > bv) : float(av == bv));
            }
            rc = d + r * c;
        } else {
            vec3 l = mix(a, b, c);
            rc = co.x == 0 ? d + l : d - l;
            rc += co.y == 1 ? 0.5 : (co.y == 2 ? -0.5 : 0.0);
            rc *= co.z == 1 ? 2.0 : (co.z == 2 ? 4.0 : (co.z == 3 ? 0.5 : 1.0));
        }
        rc = co.w != 0 ? clamp(rc, 0.0, 1.0) : clamp(rc, -1024.0 / 255.0, 1023.0 / 255.0);
        float aa = clamp(ain[ai.x], 0.0, 1.0), ab = clamp(ain[ai.y], 0.0, 1.0), ac = clamp(ain[ai.z], 0.0, 1.0), ad = ain[ai.w];
        float ra;
        if (ao.y == 3) {
            float r = ao.x == 0 ? float(q8(aa) > q8(ab)) : float(q8(aa) == q8(ab));
            ra = ad + r * ac;
        } else {
            float l = mix(aa, ab, ac);
            ra = ao.x == 0 ? ad + l : ad - l;
            ra += ao.y == 1 ? 0.5 : (ao.y == 2 ? -0.5 : 0.0);
            ra *= ao.z == 1 ? 2.0 : (ao.z == 2 ? 4.0 : (ao.z == 3 ? 0.5 : 1.0));
        }
        ra = ao.w != 0 ? clamp(ra, 0.0, 1.0) : clamp(ra, -1024.0 / 255.0, 1023.0 / 255.0);
        reg[sel.x].rgb = rc;
        reg[sel.y].a = ra;
        last = vec4(rc, ra);
    }
    last = clamp(last, 0.0, 1.0);
    float a8 = floor(last.a * 255.0 + 0.5);
    bool p0 = acmp(head.y, a8, alpha_ref.x), p1 = acmp(head.z, a8, alpha_ref.y);
    bool pass = head.w == 0 ? (p0 && p1) : (head.w == 1 ? (p0 || p1) : (head.w == 2 ? (p0 != p1) : (p0 == p1)));
    if (!pass) discard;
    o_color = last;
}
