#version 450
// The GPU's transform unit (XF), per vertex, for vk_backend.c: as
// gl_backend.c's vertex shader, which says more. The XF state is a copy in
// the frame's data buffer, at word pc.xf, packed (xw): XF memory 0x000-0x0FF
// (position and texture matrices), 0x400-0x47F (normal matrices),
// 0x500-0x5FF (post-transform matrices), 0x600-0x67F (lights), then the XF
// registers 0x1000-0x105F.

layout(std430, set = 1, binding = 0) readonly buffer Data { uint words[]; };
layout(push_constant) uniform Draw { uint xf, tev; uint tex[8]; } pc;

layout(location = 0) in uvec4 a_mtx0;
layout(location = 1) in uvec4 a_mtx1;
layout(location = 2) in uvec4 a_mtx2;
layout(location = 3) in vec3 a_pos;
layout(location = 4) in vec3 a_nrm;
layout(location = 5) in vec4 a_col0;
layout(location = 6) in vec4 a_col1;
layout(location = 7) in vec4 a_tex01;
layout(location = 8) in vec4 a_tex23;
layout(location = 9) in vec4 a_tex45;
layout(location = 10) in vec4 a_tex67;

layout(location = 0) out vec4 v_col0;
layout(location = 1) out vec4 v_col1;
layout(location = 2) out vec3 v_tex[8];

uint xw(int a) {
    int i = a >= 0x1000 ? a - 0x1000 + 0x300 : a >= 0x500 ? a - 0x380 : a >= 0x400 ? a - 0x300 : a;
    return words[pc.xf + uint(i)];
}
float xfl(int a) { return uintBitsToFloat(xw(a)); }
vec4 row(int a) { return vec4(xfl(a), xfl(a + 1), xfl(a + 2), xfl(a + 3)); }
vec3 vec3at(int a) { return vec3(xfl(a), xfl(a + 1), xfl(a + 2)); }
vec4 unpack(uint c) {
    return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0;
}

vec4 light(int j, uint ctrl, vec3 pos, vec3 n) {
    int b = 0x600 + j * 16;
    vec3 cosatt = vec3at(b + 4), distatt = vec3at(b + 7), lpos = vec3at(b + 10), ldirv = vec3at(b + 13);
    uint dfn = (ctrl >> 7) & 3u, afn = (ctrl >> 9) & 3u;
    vec3 ldir;
    float attn;
    if (afn == 1u) { // specular
        ldir = normalize(lpos - pos);
        attn = dot(n, ldir) >= 0.0 ? max(0.0, dot(n, ldirv)) : 0.0;
        vec3 da = dfn == 0u ? distatt : normalize(distatt);
        attn = max(0.0, dot(cosatt, vec3(1.0, attn, attn * attn))) / dot(da, vec3(1.0, attn, attn * attn));
    } else if (afn == 3u) { // spot
        vec3 d = lpos - pos;
        float dist2 = dot(d, d), dist = sqrt(dist2);
        ldir = d / dist;
        attn = max(0.0, dot(ldir, ldirv));
        attn = max(0.0, cosatt.x + cosatt.y * attn + cosatt.z * attn * attn) / dot(distatt, vec3(1.0, dist, dist2));
    } else { // none, directional
        vec3 d = lpos - pos;
        ldir = length(d) == 0.0 ? n : normalize(d);
        attn = 1.0;
    }
    float diff = dfn == 0u ? 1.0 : (dfn == 1u ? dot(ldir, n) : max(0.0, dot(ldir, n)));
    return attn * diff * unpack(xw(b + 3));
}

vec4 lights(uint ctrl, vec3 pos, vec3 n) {
    uint mask = ((ctrl >> 2) & 15u) | (((ctrl >> 11) & 15u) << 4);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 8; j++)
        if ((mask & (1u << uint(j))) != 0u) sum += light(j, ctrl, pos, n);
    return sum;
}

vec4 channel(int i, vec4 vcol, vec3 pos, vec3 n) {
    uint cc = xw(0x100E + i), ac = xw(0x1010 + i);
    vec4 matr = unpack(xw(0x100C + i)), ambr = unpack(xw(0x100A + i)), r;
    vec3 mat = (cc & 1u) != 0u ? vcol.rgb : matr.rgb;
    if ((cc & 2u) != 0u) {
        vec3 amb = (cc & 64u) != 0u ? vcol.rgb : ambr.rgb;
        r.rgb = mat * clamp(amb + lights(cc, pos, n).rgb, 0.0, 1.0);
    } else r.rgb = mat;
    float mata = (ac & 1u) != 0u ? vcol.a : matr.a;
    if ((ac & 2u) != 0u) {
        float amba = (ac & 64u) != 0u ? vcol.a : ambr.a;
        r.a = mata * clamp(amba + lights(ac, pos, n).a, 0.0, 1.0);
    } else r.a = mata;
    return clamp(r, 0.0, 1.0);
}

int texmtx(int i) { return int(i < 3 ? a_mtx0[i + 1] : i < 7 ? a_mtx1[i - 3] : a_mtx2.x); }
vec2 texin(int i) {
    vec4 p = i < 2 ? a_tex01 : i < 4 ? a_tex23 : i < 6 ? a_tex45 : a_tex67;
    return (i & 1) == 0 ? p.xy : p.zw;
}

void main() {
    int pn = int(a_mtx0.x);
    vec4 in4 = vec4(a_pos, 1.0);
    vec3 world = vec3(dot(row(pn * 4), in4), dot(row(pn * 4 + 4), in4), dot(row(pn * 4 + 8), in4));
    int nb = 0x400 + (pn & 31) * 3;
    vec3 n = vec3(dot(vec3at(nb), a_nrm), dot(vec3at(nb + 3), a_nrm), dot(vec3at(nb + 6), a_nrm));
    if (dot(n, n) > 0.0) n = normalize(n);
    float p0 = xfl(0x1020), p1 = xfl(0x1021), p2 = xfl(0x1022), p3 = xfl(0x1023), p4 = xfl(0x1024), p5 = xfl(0x1025);
    vec4 clip = xw(0x1026) == 0u
        ? vec4(p0 * world.x + p1 * world.z, p2 * world.y + p3 * world.z, p4 * world.z + p5, -world.z)
        : vec4(p0 * world.x + p1, p2 * world.y + p3, p4 * world.z + p5, 1.0);
    // Depth reversed, as in gl_backend.c: 0..w, far to near.
    gl_Position = vec4(clip.x, clip.y, -clip.z, clip.w);
    v_col0 = channel(0, a_col0, world, n);
    v_col1 = channel(1, a_col1, world, n);
    int ntex = int(xw(0x103F) & 15u);
    bool dual = (xw(0x1012) & 1u) != 0u;
    for (int i = 0; i < 8; i++) {
        if (i >= ntex) {
            v_tex[i] = vec3(0.0, 0.0, 1.0);
            continue;
        }
        uint info = xw(0x1040 + i);
        int src_row = int((info >> 7) & 31u), type = int((info >> 4) & 7u);
        vec4 src = vec4(0.0, 0.0, 1.0, 1.0);
        if (src_row == 0) src = in4;
        else if (src_row == 1) src.xyz = a_nrm;
        else if (src_row >= 5 && src_row <= 12) src.xy = texin(src_row - 5);
        vec3 stq;
        if (type == 0) {
            int m = texmtx(i) * 4;
            stq = vec3(dot(row(m), src), dot(row(m + 4), src), ((info >> 1) & 1u) != 0u ? dot(row(m + 8), src) : 1.0);
        } else stq = vec3(src.xy, 1.0);
        if (dual) { // post-transform
            uint pt = xw(0x1050 + i);
            vec4 v = vec4(stq, 1.0);
            if (((pt >> 8) & 1u) != 0u && dot(v.xyz, v.xyz) > 0.0) v.xyz = normalize(v.xyz);
            int m = 0x500 + int(pt & 63u) * 4;
            stq = vec3(dot(row(m), v), dot(row(m + 4), v), dot(row(m + 8), v));
        }
        v_tex[i] = stq;
    }
}
