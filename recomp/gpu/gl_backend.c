/* OpenGL backend for the GX decoder.
 *
 * The EFB (the GPU's embedded framebuffer, 640x528) is an offscreen
 * framebuffer rendered at RECOMP_GX_SCALE times the native resolution
 * (default 2). TEV, the GPU's programmable color combiner, runs in one
 * "uber" fragment shader driven by uniforms, so any stage setup draws
 * without compiling a new shader. Copies of the EFB to the XFB are
 * presented in a window; RECOMP_GX_DUMP=<dir> also writes them as BMPs.
 *
 * Environment:
 *   RECOMP_GX_SCALE=n          internal resolution multiplier
 *   RECOMP_GX_HIDDEN=1         do not show the window
 *   RECOMP_GX_DUMP=<dir>       write presented frames as BMP files
 *   RECOMP_GX_DUMP_EVERY=n     ...every nth frame (default 30) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "gx.h"

#define EFB_W 640
#define EFB_H 528

/* ------------------------------------------------------------------------ */
/* GL 2+ entry points */

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_FUNCS(X) \
    X(void, glGenBuffers, (GLsizei, GLuint*)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(void, glBindBufferBase, (GLenum, GLuint, GLuint)) \
    X(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar*)) \
    X(void, glUniformBlockBinding, (GLuint, GLuint, GLuint)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint*)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, glUseProgram, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform1iv, (GLint, GLsizei, const GLint*)) \
    X(void, glUniform4iv, (GLint, GLsizei, const GLint*)) \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint*)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint*)) \
    X(void, glBindRenderbuffer, (GLenum, GLuint)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glBlendEquation, (GLenum))

#define DECLARE(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name p_##name;
GL_FUNCS(DECLARE)
#undef DECLARE

static void load_gl(void) {
#define LOAD(ret, name, args) \
    p_##name = (PFN_##name)wglGetProcAddress(#name); \
    if (!p_##name) { fprintf(stderr, "gl: missing %s\n", #name); exit(1); }
    GL_FUNCS(LOAD)
#undef LOAD
}

/* ------------------------------------------------------------------------ */
/* Shaders */

#define STR2(x) #x
#define STR(x) STR2(x)
#define GX_XF_WORDS_STR STR(GX_XF_WORDS)
#define GX_XF_MEM_WORDS_STR STR(GX_XF_MEM_WORDS)

/* The GPU's transform unit (XF), per vertex: position and normal
 * transform by the vertex's matrices, projection, the two color channels
 * with lighting, and texture coordinate generation. All state comes from
 * the XF memory and registers (gx.h), in a uniform buffer.
 *
 * Lighting follows the hardware model as Dolphin documents it: a lit
 * channel is material * clamp(ambient + sum of its lights), where each
 * light contributes color * attenuation * diffuse term; attenuation is
 * none, specular or spot, the diffuse term none, signed or clamped
 * N.L. */
static const char* kVertexShader =
    "#version 330 compatibility\n"
    "layout(std140) uniform XF { uvec4 xfm[" GX_XF_WORDS_STR " / 4]; };\n"
    "in vec4 a_mtx0, a_mtx1, a_mtx2; in vec3 a_pos, a_nrm; in vec4 a_col0, a_col1;\n"
    "in vec4 a_tex01, a_tex23, a_tex45, a_tex67;\n"
    "out vec4 v_col0; out vec4 v_col1; out vec3 v_tex[8];\n"
    "uint xw(int a) { int i = a < 0x1000 ? a : a - 0x1000 + " GX_XF_MEM_WORDS_STR "; return xfm[i >> 2][i & 3]; }\n"
    "float xfl(int a) { return uintBitsToFloat(xw(a)); }\n"
    "vec4 row(int a) { return uintBitsToFloat(xfm[a >> 2]); }\n" /* rows of 4 start on multiples of 4 */
    "vec3 vec3at(int a) { return vec3(xfl(a), xfl(a + 1), xfl(a + 2)); }\n"
    "vec4 unpack(uint c) { return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0; }\n"
    "vec4 light(int j, uint ctrl, vec3 pos, vec3 n) {\n"
    "  int b = 0x600 + j * 16;\n"
    "  vec3 cosatt = vec3at(b + 4), distatt = vec3at(b + 7), lpos = vec3at(b + 10), ldirv = vec3at(b + 13);\n"
    "  uint dfn = (ctrl >> 7) & 3u, afn = (ctrl >> 9) & 3u;\n"
    "  vec3 ldir; float attn;\n"
    "  if (afn == 1u) {\n" /* specular */
    "    ldir = normalize(lpos - pos);\n"
    "    attn = dot(n, ldir) >= 0.0 ? max(0.0, dot(n, ldirv)) : 0.0;\n"
    "    vec3 da = dfn == 0u ? distatt : normalize(distatt);\n"
    "    attn = max(0.0, dot(cosatt, vec3(1.0, attn, attn * attn))) / dot(da, vec3(1.0, attn, attn * attn));\n"
    "  } else if (afn == 3u) {\n" /* spot */
    "    vec3 d = lpos - pos; float dist2 = dot(d, d), dist = sqrt(dist2); ldir = d / dist;\n"
    "    attn = max(0.0, dot(ldir, ldirv));\n"
    "    attn = max(0.0, cosatt.x + cosatt.y * attn + cosatt.z * attn * attn) / dot(distatt, vec3(1.0, dist, dist2));\n"
    "  } else {\n" /* none, directional */
    "    vec3 d = lpos - pos; ldir = length(d) == 0.0 ? n : normalize(d); attn = 1.0;\n"
    "  }\n"
    "  float diff = dfn == 0u ? 1.0 : (dfn == 1u ? dot(ldir, n) : max(0.0, dot(ldir, n)));\n"
    "  return attn * diff * unpack(xw(b + 3));\n"
    "}\n"
    "vec4 lights(uint ctrl, vec3 pos, vec3 n) {\n"
    "  uint mask = ((ctrl >> 2) & 15u) | (((ctrl >> 11) & 15u) << 4);\n"
    "  vec4 sum = vec4(0.0);\n"
    "  for (int j = 0; j < 8; j++) if ((mask & (1u << uint(j))) != 0u) sum += light(j, ctrl, pos, n);\n"
    "  return sum;\n"
    "}\n"
    "vec4 channel(int i, vec4 vcol, vec3 pos, vec3 n) {\n"
    "  uint cc = xw(0x100E + i), ac = xw(0x1010 + i);\n"
    "  vec4 matr = unpack(xw(0x100C + i)), ambr = unpack(xw(0x100A + i)), r;\n"
    "  vec3 mat = (cc & 1u) != 0u ? vcol.rgb : matr.rgb;\n"
    "  if ((cc & 2u) != 0u) {\n"
    "    vec3 amb = (cc & 64u) != 0u ? vcol.rgb : ambr.rgb;\n"
    "    r.rgb = mat * clamp(amb + lights(cc, pos, n).rgb, 0.0, 1.0);\n"
    "  } else r.rgb = mat;\n"
    "  float mata = (ac & 1u) != 0u ? vcol.a : matr.a;\n"
    "  if ((ac & 2u) != 0u) {\n"
    "    float amba = (ac & 64u) != 0u ? vcol.a : ambr.a;\n"
    "    r.a = mata * clamp(amba + lights(ac, pos, n).a, 0.0, 1.0);\n"
    "  } else r.a = mata;\n"
    "  return clamp(r, 0.0, 1.0);\n"
    "}\n"
    "int texmtx(int i) { return int(i < 3 ? a_mtx0[i + 1] : i < 7 ? a_mtx1[i - 3] : a_mtx2.x); }\n"
    "vec2 texin(int i) { vec4 p = i < 2 ? a_tex01 : i < 4 ? a_tex23 : i < 6 ? a_tex45 : a_tex67; return (i & 1) == 0 ? p.xy : p.zw; }\n"
    "void main() {\n"
    "  int pn = int(a_mtx0.x);\n"
    "  vec4 in4 = vec4(a_pos, 1.0);\n"
    "  vec3 world = vec3(dot(row(pn * 4), in4), dot(row(pn * 4 + 4), in4), dot(row(pn * 4 + 8), in4));\n"
    "  int nb = 0x400 + (pn & 31) * 3;\n"
    "  vec3 n = vec3(dot(vec3at(nb), a_nrm), dot(vec3at(nb + 3), a_nrm), dot(vec3at(nb + 6), a_nrm));\n"
    "  if (dot(n, n) > 0.0) n = normalize(n);\n"
    "  float p0 = xfl(0x1020), p1 = xfl(0x1021), p2 = xfl(0x1022), p3 = xfl(0x1023), p4 = xfl(0x1024), p5 = xfl(0x1025);\n"
    "  vec4 clip = xw(0x1026) == 0u\n"
    "    ? vec4(p0 * world.x + p1 * world.z, p2 * world.y + p3 * world.z, p4 * world.z + p5, -world.z)\n"
    "    : vec4(p0 * world.x + p1, p2 * world.y + p3, p4 * world.z + p5, 1.0);\n"
    /* GX depth runs from -w (near) to 0 (far); GL wants -w..w. */
    "  gl_Position = vec4(clip.x, clip.y, 2.0 * clip.z + clip.w, clip.w);\n"
    "  v_col0 = channel(0, a_col0, world, n);\n"
    "  v_col1 = channel(1, a_col1, world, n);\n"
    "  int ntex = int(xw(0x103F) & 15u);\n"
    "  bool dual = (xw(0x1012) & 1u) != 0u;\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    if (i >= ntex) { v_tex[i] = vec3(0.0, 0.0, 1.0); continue; }\n"
    "    uint info = xw(0x1040 + i);\n"
    "    int src_row = int((info >> 7) & 31u), type = int((info >> 4) & 7u);\n"
    "    vec4 src = vec4(0.0, 0.0, 1.0, 1.0);\n"
    "    if (src_row == 0) src = in4;\n"
    "    else if (src_row == 1) src.xyz = a_nrm;\n"
    "    else if (src_row >= 5 && src_row <= 12) src.xy = texin(src_row - 5);\n"
    "    vec3 stq;\n"
    "    if (type == 0) {\n"
    "      int m = texmtx(i) * 4;\n"
    "      stq = vec3(dot(row(m), src), dot(row(m + 4), src), ((info >> 1) & 1u) != 0u ? dot(row(m + 8), src) : 1.0);\n"
    "    } else stq = vec3(src.xy, 1.0);\n"
    "    if (dual) {\n" /* post-transform */
    "      uint pt = xw(0x1050 + i);\n"
    "      vec4 v = vec4(stq, 1.0);\n"
    "      if (((pt >> 8) & 1u) != 0u && dot(v.xyz, v.xyz) > 0.0) v.xyz = normalize(v.xyz);\n"
    "      int m = 0x500 + int(pt & 63u) * 4;\n"
    "      stq = vec3(dot(row(m), v), dot(row(m + 4), v), dot(row(m + 8), v));\n"
    "    }\n"
    "    v_tex[i] = stq;\n"
    "  }\n"
    "}\n";

/* TEV: each stage computes, per color channel and for alpha,
 *   d + lerp(a, b, c) (or d - lerp(...)), plus a bias, times a scale,
 * optionally clamped, into one of four registers; or, with bias 3, a
 * comparison of a and b selecting c or 0. Inputs are registers, the
 * texture, the rasterized color, constants and "konst" colors. */
static const char* kFragmentShader =
    "#version 330 compatibility\n"
    "in vec4 v_col0; in vec4 v_col1; in vec3 v_tex[8];\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_tex0, u_tex1, u_tex2, u_tex3, u_tex4, u_tex5, u_tex6, u_tex7;\n"
    "uniform int u_num_stages;\n"
    "uniform ivec4 u_cin[16], u_ain[16], u_cop[16], u_aop[16], u_map[16], u_sel[16];\n"
    "uniform vec4 u_reg[4], u_konst[4];\n"
    "uniform ivec4 u_swap[4];\n"
    "uniform ivec4 u_alpha;\n"
    "uniform vec4 u_alpha_ref;\n"
    "vec4 sample_map(int m, vec2 uv) {\n"
    "  if (m == 0) return texture(u_tex0, uv); if (m == 1) return texture(u_tex1, uv);\n"
    "  if (m == 2) return texture(u_tex2, uv); if (m == 3) return texture(u_tex3, uv);\n"
    "  if (m == 4) return texture(u_tex4, uv); if (m == 5) return texture(u_tex5, uv);\n"
    "  if (m == 6) return texture(u_tex6, uv); return texture(u_tex7, uv);\n"
    "}\n"
    "vec4 swizzle(vec4 c, int t) { ivec4 s = u_swap[t]; return vec4(c[s.x], c[s.y], c[s.z], c[s.w]); }\n"
    "float kfrac(int s) { return s < 8 ? float(8 - s) / 8.0 : 0.0; }\n"
    "vec3 kcolor(int s) {\n"
    "  if (s < 8) return vec3(kfrac(s));\n"
    "  if (s >= 12 && s < 16) return u_konst[s - 12].rgb;\n"
    "  if (s >= 16) { int k = (s - 16) & 3; int c = (s - 16) >> 2; return vec3(u_konst[k][c]); }\n"
    "  return vec3(0.0);\n"
    "}\n"
    "float kalpha(int s) {\n"
    "  if (s < 8) return kfrac(s);\n"
    "  if (s >= 16) { int k = (s - 16) & 3; int c = (s - 16) >> 2; return u_konst[k][c]; }\n"
    "  return 0.0;\n"
    "}\n"
    "bool acmp(int f, float a, float r) {\n"
    "  if (f == 0) return false; if (f == 1) return a < r; if (f == 2) return a == r;\n"
    "  if (f == 3) return a <= r; if (f == 4) return a > r; if (f == 5) return a != r;\n"
    "  if (f == 6) return a >= r; return true;\n"
    "}\n"
    "float q8(float x) { return floor(x * 255.0 + 0.5) / 255.0; }\n"
    "vec3 q8(vec3 x) { return floor(x * 255.0 + 0.5) / 255.0; }\n"
    "void main() {\n"
    "  vec4 reg[4]; reg[0] = u_reg[0]; reg[1] = u_reg[1]; reg[2] = u_reg[2]; reg[3] = u_reg[3];\n"
    "  vec4 last = reg[0];\n"
    "  for (int i = 0; i < u_num_stages; i++) {\n"
    "    ivec4 map = u_map[i]; ivec4 sel = u_sel[i];\n"
    "    vec4 tex = vec4(1.0);\n"
    "    if (map.z != 0) { vec3 tc = v_tex[map.y]; tex = sample_map(map.x, tc.xy / (tc.z == 0.0 ? 1.0 : tc.z)); }\n"
    "    tex = swizzle(tex, sel.w & 3);\n"
    "    vec4 ras = map.w == 0 ? v_col0 : (map.w == 1 ? v_col1 : vec4(0.0));\n"
    "    ras = swizzle(ras, sel.w >> 2);\n"
    "    vec3 kc = kcolor(sel.z & 31); float ka = kalpha(sel.z >> 5);\n"
    "    vec3 cin[16];\n"
    "    cin[0] = reg[0].rgb; cin[1] = vec3(reg[0].a); cin[2] = reg[1].rgb; cin[3] = vec3(reg[1].a);\n"
    "    cin[4] = reg[2].rgb; cin[5] = vec3(reg[2].a); cin[6] = reg[3].rgb; cin[7] = vec3(reg[3].a);\n"
    "    cin[8] = tex.rgb; cin[9] = vec3(tex.a); cin[10] = ras.rgb; cin[11] = vec3(ras.a);\n"
    "    cin[12] = vec3(1.0); cin[13] = vec3(0.5); cin[14] = kc; cin[15] = vec3(0.0);\n"
    "    float ain[8];\n"
    "    ain[0] = reg[0].a; ain[1] = reg[1].a; ain[2] = reg[2].a; ain[3] = reg[3].a;\n"
    "    ain[4] = tex.a; ain[5] = ras.a; ain[6] = ka; ain[7] = 0.0;\n"
    "    ivec4 ci = u_cin[i]; ivec4 co = u_cop[i];\n"
    "    vec3 a = clamp(cin[ci.x], 0.0, 1.0), b = clamp(cin[ci.y], 0.0, 1.0), c = clamp(cin[ci.z], 0.0, 1.0), d = cin[ci.w];\n"
    "    vec3 rc;\n"
    "    if (co.y == 3) {\n"
    "      vec3 r;\n"
    "      if (co.z == 3) r = vec3(co.x == 0 ? greaterThan(q8(a), q8(b)) : equal(q8(a), q8(b)));\n"
    "      else { float av = dot(q8(a), vec3(65536.0, 256.0, 1.0)), bv = dot(q8(b), vec3(65536.0, 256.0, 1.0));\n"
    "        if (co.z == 0) { av = q8(a.r); bv = q8(b.r); }\n"
    "        else if (co.z == 1) { av = q8(a.r) + q8(a.g) * 256.0; bv = q8(b.r) + q8(b.g) * 256.0; }\n"
    "        else { av = q8(a.r) + q8(a.g) * 256.0 + q8(a.b) * 65536.0; bv = q8(b.r) + q8(b.g) * 256.0 + q8(b.b) * 65536.0; }\n"
    "        r = vec3(co.x == 0 ? float(av > bv) : float(av == bv)); }\n"
    "      rc = d + r * c;\n"
    "    } else {\n"
    "      vec3 l = mix(a, b, c);\n"
    "      rc = co.x == 0 ? d + l : d - l;\n"
    "      rc += co.y == 1 ? 0.5 : (co.y == 2 ? -0.5 : 0.0);\n"
    "      rc *= co.z == 1 ? 2.0 : (co.z == 2 ? 4.0 : (co.z == 3 ? 0.5 : 1.0));\n"
    "    }\n"
    "    rc = co.w != 0 ? clamp(rc, 0.0, 1.0) : clamp(rc, -1024.0 / 255.0, 1023.0 / 255.0);\n"
    "    ivec4 ai = u_ain[i]; ivec4 ao = u_aop[i];\n"
    "    float aa = clamp(ain[ai.x], 0.0, 1.0), ab = clamp(ain[ai.y], 0.0, 1.0), ac = clamp(ain[ai.z], 0.0, 1.0), ad = ain[ai.w];\n"
    "    float ra;\n"
    "    if (ao.y == 3) {\n"
    "      float r = ao.x == 0 ? float(q8(aa) > q8(ab)) : float(q8(aa) == q8(ab));\n"
    "      ra = ad + r * ac;\n"
    "    } else {\n"
    "      float l = mix(aa, ab, ac);\n"
    "      ra = ao.x == 0 ? ad + l : ad - l;\n"
    "      ra += ao.y == 1 ? 0.5 : (ao.y == 2 ? -0.5 : 0.0);\n"
    "      ra *= ao.z == 1 ? 2.0 : (ao.z == 2 ? 4.0 : (ao.z == 3 ? 0.5 : 1.0));\n"
    "    }\n"
    "    ra = ao.w != 0 ? clamp(ra, 0.0, 1.0) : clamp(ra, -1024.0 / 255.0, 1023.0 / 255.0);\n"
    "    reg[sel.x].rgb = rc; reg[sel.y].a = ra;\n"
    "    last = vec4(rc, ra);\n"
    "  }\n"
    "  last = clamp(last, 0.0, 1.0);\n"
    "  float a8 = floor(last.a * 255.0 + 0.5);\n"
    "  bool p0 = acmp(u_alpha.x, a8, u_alpha_ref.x), p1 = acmp(u_alpha.y, a8, u_alpha_ref.y);\n"
    "  bool pass = u_alpha.z == 0 ? (p0 && p1) : (u_alpha.z == 1 ? (p0 || p1) : (u_alpha.z == 2 ? (p0 != p1) : (p0 == p1)));\n"
    "  if (!pass) discard;\n"
    "  o_color = last;\n"
    "}\n";

extern volatile long g_in_gl;
static void print_gl_stats(void);

/* Time and calls per entry point, for RECOMP_FPS=1. */
enum { GLK_TEXTURE, GLK_TEXTURE_UPDATE, GLK_DRAW, GLK_CLEAR, GLK_COPY, GLK_PRESENT, GL_KINDS };
static const char* const kGlKind[GL_KINDS] = {"texture", "update", "draw", "clear", "copy", "present"};
static LONGLONG sGlTicks[GL_KINDS];
static unsigned sGlCalls[GL_KINDS];

static LONGLONG qpc(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
#define GL_ENTER() LONGLONG t0_ = (g_in_gl = 1, qpc())
#define GL_LEAVE(kind) (sGlTicks[kind] += qpc() - t0_, sGlCalls[kind]++, g_in_gl = 0)

/* Prints and resets the per-entry-point totals. */
static void print_gl_stats(void) {
    LARGE_INTEGER f;
    int k;
    QueryPerformanceFrequency(&f);
    fprintf(stderr, "  gl:");
    for (k = 0; k < GL_KINDS; k++) {
        fprintf(stderr, " %s %u calls %.0f ms%s", kGlKind[k], sGlCalls[k], sGlTicks[k] * 1000.0 / f.QuadPart,
                k + 1 < GL_KINDS ? "," : "\n");
        sGlTicks[k] = 0;
        sGlCalls[k] = 0;
    }
}
static GLuint sProgram, sVao, sVbo, sXfUbo;
static GLint uNumStages, uCin, uAin, uCop, uAop, uMap, uSel, uReg, uKonst, uSwap, uAlpha, uAlphaRef;

static GLuint compile(GLenum type, const char* src) {
    GLuint s = p_glCreateShader(type);
    GLint ok;
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "gl: shader error:\n%s\n", log);
        exit(1);
    }
    return s;
}

static void build_program(void) {
    static const char* attribs[] = {"a_mtx0", "a_mtx1", "a_mtx2", "a_pos", "a_nrm", "a_col0",
                                    "a_col1", "a_tex01", "a_tex23", "a_tex45", "a_tex67"};
    GLint ok;
    int i;
    sProgram = p_glCreateProgram();
    p_glAttachShader(sProgram, compile(GL_VERTEX_SHADER, kVertexShader));
    p_glAttachShader(sProgram, compile(GL_FRAGMENT_SHADER, kFragmentShader));
    for (i = 0; i < 11; i++) p_glBindAttribLocation(sProgram, (GLuint)i, attribs[i]);
    p_glLinkProgram(sProgram);
    p_glGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(sProgram, sizeof(log), NULL, log);
        fprintf(stderr, "gl: link error:\n%s\n", log);
        exit(1);
    }
    p_glUseProgram(sProgram);
    for (i = 0; i < 8; i++) {
        char name[16];
        snprintf(name, sizeof(name), "u_tex%d", i);
        p_glUniform1i(p_glGetUniformLocation(sProgram, name), i);
    }
    uNumStages = p_glGetUniformLocation(sProgram, "u_num_stages");
    uCin = p_glGetUniformLocation(sProgram, "u_cin");
    uAin = p_glGetUniformLocation(sProgram, "u_ain");
    uCop = p_glGetUniformLocation(sProgram, "u_cop");
    uAop = p_glGetUniformLocation(sProgram, "u_aop");
    uMap = p_glGetUniformLocation(sProgram, "u_map");
    uSel = p_glGetUniformLocation(sProgram, "u_sel");
    uReg = p_glGetUniformLocation(sProgram, "u_reg");
    uKonst = p_glGetUniformLocation(sProgram, "u_konst");
    uSwap = p_glGetUniformLocation(sProgram, "u_swap");
    uAlpha = p_glGetUniformLocation(sProgram, "u_alpha");
    uAlphaRef = p_glGetUniformLocation(sProgram, "u_alpha_ref");

    p_glGenVertexArrays(1, &sVao);
    p_glBindVertexArray(sVao);
    p_glGenBuffers(1, &sVbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    for (i = 0; i < 3; i++) {
        p_glEnableVertexAttribArray(i);
        p_glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)(offsetof(GXVertex, mtx) + i * 4 * sizeof(float)));
    }
    p_glEnableVertexAttribArray(3);
    p_glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, pos));
    p_glEnableVertexAttribArray(4);
    p_glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, nrm));
    p_glEnableVertexAttribArray(5);
    p_glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, color[0]));
    p_glEnableVertexAttribArray(6);
    p_glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, color[1]));
    for (i = 0; i < 4; i++) {
        p_glEnableVertexAttribArray(7 + i);
        p_glVertexAttribPointer(7 + i, 4, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)(offsetof(GXVertex, tex) + i * 4 * sizeof(float)));
    }

    /* The XF state, for the vertex shader. */
    p_glGenBuffers(1, &sXfUbo);
    p_glBindBuffer(GL_UNIFORM_BUFFER, sXfUbo);
    p_glBufferData(GL_UNIFORM_BUFFER, GX_XF_WORDS * 4, NULL, GL_DYNAMIC_DRAW);
    p_glUniformBlockBinding(sProgram, p_glGetUniformBlockIndex(sProgram, "XF"), 0);
    p_glBindBufferBase(GL_UNIFORM_BUFFER, 0, sXfUbo);
}

/* ------------------------------------------------------------------------ */
/* Window and EFB */

static HWND sWnd;
static HDC sDC;
static int sScale = 2;
static GLuint sEfbFbo, sEfbColor, sEfbDepth;
static GLuint sWhite;
static const char* sDumpDir;
static int sDumpEvery = 30;
static unsigned sPresented;

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) exit(0);
    return DefWindowProcA(h, msg, wp, lp);
}

void gxb_init(void) {
    WNDCLASSA wc;
    PIXELFORMATDESCRIPTOR pfd;
    RECT r = {0, 0, 640 * 2, 480 * 2};
    HGLRC rc;
    const char* v;
    uint32_t white = 0xFFFFFFFF;
    if ((v = getenv("RECOMP_GX_SCALE")) && atoi(v) > 0) sScale = atoi(v);
    sDumpDir = getenv("RECOMP_GX_DUMP");
    if ((v = getenv("RECOMP_GX_DUMP_EVERY")) && atoi(v) > 0) sDumpEvery = atoi(v);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PokeParkRecomp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    sWnd = CreateWindowA("PokeParkRecomp", "PokePark (recompiled)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    sDC = GetDC(sWnd);
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    SetPixelFormat(sDC, ChoosePixelFormat(sDC, &pfd), &pfd);
    rc = wglCreateContext(sDC);
    wglMakeCurrent(sDC, rc);
    v = getenv("RECOMP_GX_HIDDEN");
    if (!(v && *v && *v != '0')) ShowWindow(sWnd, SW_SHOW);
    load_gl();
    fprintf(stderr, "gl: %s, %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    build_program();

    glGenTextures(1, &sEfbColor);
    glBindTexture(GL_TEXTURE_2D, sEfbColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W * sScale, EFB_H * sScale, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    p_glGenRenderbuffers(1, &sEfbDepth);
    p_glBindRenderbuffer(GL_RENDERBUFFER, sEfbDepth);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, EFB_W * sScale, EFB_H * sScale);
    p_glGenFramebuffers(1, &sEfbFbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, sEfbFbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sEfbColor, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sEfbDepth);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glGenTextures(1, &sWhite);
    glBindTexture(GL_TEXTURE_2D, sWhite);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
}

/* ------------------------------------------------------------------------ */
/* Textures */

static GLint wrap_mode(int w) { return w == 0 ? GL_CLAMP_TO_EDGE : (w == 1 ? GL_REPEAT : GL_MIRRORED_REPEAT); }

static uint32_t gxb_texture_gl(uint64_t key, int width, int height, const uint8_t* rgba, int wrap_s, int wrap_t, int linear) {
    GLuint t;
    (void)key;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode(wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode(wrap_t));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    return t;
}

static void gxb_texture_update_gl(uint32_t handle, int width, int height, const uint8_t* rgba) {
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* Textures are deleted after the frame is presented: a draw earlier in the
 * frame may still refer to one, and GL hands a deleted name straight to
 * the next texture created. */
#define MAX_PENDING_FREES 1024
static GLuint sPendingFree[MAX_PENDING_FREES];
static int sPendingFreeCount;

void gxb_texture_free(uint32_t handle) {
    if (sPendingFreeCount == MAX_PENDING_FREES) {
        glDeleteTextures(sPendingFreeCount, sPendingFree);
        sPendingFreeCount = 0;
    }
    sPendingFree[sPendingFreeCount++] = handle;
}

/* ------------------------------------------------------------------------ */
/* Drawing */

static const GLenum kBlendSrc[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
static const GLenum kBlendDst[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
static const GLenum kDepthFunc[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};

static void gxb_draw_triangles_gl(const GXDrawState* st, const GXVertex* v, int count) {
    GLint cin[64], ain[64], cop[64], aop[64], map[64], sel[64], swap[16], alpha[4];
    GLfloat reg[16], konst[16], aref[4];
    int i;
    p_glBindFramebuffer(GL_FRAMEBUFFER, sEfbFbo);
    p_glUseProgram(sProgram);
    p_glBindVertexArray(sVao);

    for (i = 0; i < st->num_stages; i++) {
        const GXTevStage* s = &st->stage[i];
        int k;
        for (k = 0; k < 4; k++) {
            cin[4 * i + k] = s->color_in[k];
            ain[4 * i + k] = s->alpha_in[k];
        }
        cop[4 * i + 0] = s->color_op;
        cop[4 * i + 1] = s->color_bias;
        cop[4 * i + 2] = s->color_scale;
        cop[4 * i + 3] = s->color_clamp;
        aop[4 * i + 0] = s->alpha_op;
        aop[4 * i + 1] = s->alpha_bias;
        aop[4 * i + 2] = s->alpha_scale;
        aop[4 * i + 3] = s->alpha_clamp;
        map[4 * i + 0] = s->texmap;
        map[4 * i + 1] = s->texcoord;
        map[4 * i + 2] = s->tex_enable;
        map[4 * i + 3] = s->channel == 7 ? 7 : (s->channel & 1);
        sel[4 * i + 0] = s->color_dest;
        sel[4 * i + 1] = s->alpha_dest;
        sel[4 * i + 2] = s->kcolor_sel | (s->kalpha_sel << 5);
        sel[4 * i + 3] = s->tex_swap | (s->ras_swap << 2);
    }
    for (i = 0; i < 4; i++) {
        int k;
        for (k = 0; k < 4; k++) {
            reg[4 * i + k] = st->tev_reg[i][k];
            konst[4 * i + k] = st->konst[i][k];
            swap[4 * i + k] = st->swap_table[i][k];
        }
    }
    alpha[0] = st->alpha_func[0];
    alpha[1] = st->alpha_func[1];
    alpha[2] = st->alpha_logic;
    alpha[3] = 0;
    aref[0] = (float)st->alpha_ref[0];
    aref[1] = (float)st->alpha_ref[1];
    aref[2] = aref[3] = 0;

    p_glUniform1i(uNumStages, st->num_stages);
    p_glUniform4iv(uCin, st->num_stages, cin);
    p_glUniform4iv(uAin, st->num_stages, ain);
    p_glUniform4iv(uCop, st->num_stages, cop);
    p_glUniform4iv(uAop, st->num_stages, aop);
    p_glUniform4iv(uMap, st->num_stages, map);
    p_glUniform4iv(uSel, st->num_stages, sel);
    p_glUniform4fv(uReg, 4, reg);
    p_glUniform4fv(uKonst, 4, konst);
    p_glUniform4iv(uSwap, 4, swap);
    p_glUniform4iv(uAlpha, 1, alpha);
    p_glUniform4fv(uAlphaRef, 1, aref);

    for (i = 0; i < 8; i++) {
        p_glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, st->texture[i] ? st->texture[i] : sWhite);
    }
    p_glActiveTexture(GL_TEXTURE0);

    if (st->blend_enable) {
        glEnable(GL_BLEND);
        glBlendFunc(kBlendSrc[st->blend_src], kBlendDst[st->blend_dst]);
        p_glBlendEquation(st->blend_subtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        if (st->blend_subtract) glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }
    glColorMask(st->color_update, st->color_update, st->color_update, st->alpha_update);
    if (st->z_enable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(kDepthFunc[st->z_func]);
        glDepthMask(st->z_update ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    glDisable(GL_CULL_FACE);

    glViewport((GLint)(st->viewport[0] * sScale), (GLint)((EFB_H - (st->viewport[1] + st->viewport[3])) * sScale),
               (GLsizei)(st->viewport[2] * sScale), (GLsizei)(st->viewport[3] * sScale));
    glDepthRange(st->viewport[4] < 0 ? 0 : st->viewport[4], st->viewport[5] > 1 ? 1 : st->viewport[5]);
    /* Face culling (GX_CULL_NONE, FRONT, BACK, ALL). GX's front faces wind
     * counter-clockwise in GL's window coordinates, GL's default. Games
     * rely on it: the picture confirmation dialog hides a placeholder pane
     * by turning it away from the camera. */
    if (st->cull_mode == 0) {
        glDisable(GL_CULL_FACE);
    } else {
        static const GLenum kCull[4] = {GL_BACK, GL_FRONT, GL_BACK, GL_FRONT_AND_BACK};
        glEnable(GL_CULL_FACE);
        glCullFace(kCull[st->cull_mode]);
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(st->scissor[0] * sScale, (EFB_H - st->scissor[3]) * sScale, (st->scissor[2] - st->scissor[0]) * sScale,
              (st->scissor[3] - st->scissor[1]) * sScale);

    if (st->xf && st->xf_dirty_lo < st->xf_dirty_hi) {
        /* Only the XF words written since the last draw. */
        p_glBindBuffer(GL_UNIFORM_BUFFER, sXfUbo);
        p_glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)st->xf_dirty_lo * 4,
                          (GLsizeiptr)(st->xf_dirty_hi - st->xf_dirty_lo) * 4, st->xf + st->xf_dirty_lo);
    }
    p_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(GXVertex), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, count);
}

static void gxb_clear_gl(int color, int alpha, int depth, uint32_t rgba, uint32_t z) {
    GLbitfield mask = 0;
    p_glBindFramebuffer(GL_FRAMEBUFFER, sEfbFbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(color, color, color, alpha);
    glDepthMask(depth ? GL_TRUE : GL_FALSE);
    glClearColor(((rgba >> 24) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f, ((rgba >> 8) & 0xFF) / 255.0f,
                 (rgba & 0xFF) / 255.0f);
    glClearDepth(z / 16777215.0);
    if (color || alpha) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    if (mask) glClear(mask);
}

void gx_register_copy(uint32_t addr, uint32_t handle, int width, int height, int fmt);

/* EFB copies to textures. GX textures have row 0 at the top while the EFB
 * framebuffer has it at the bottom, so the copy flips. */
typedef struct CopyTarget {
    uint32_t addr;
    int w, h;
    GLuint tex, fbo;
} CopyTarget;
static CopyTarget sTargets[64];
static int sTargetCount;

static void gxb_copy_to_texture_gl(uint32_t addr, int x, int y, int w, int h, int fmt, int clear) {
    CopyTarget* t = NULL;
    int i;
    (void)clear;
    for (i = 0; i < sTargetCount; i++) {
        if (sTargets[i].addr == addr && sTargets[i].w == w && sTargets[i].h == h) t = &sTargets[i];
    }
    if (!t) {
        if (sTargetCount == 64) return;
        t = &sTargets[sTargetCount++];
        t->addr = addr;
        t->w = w;
        t->h = h;
        glGenTextures(1, &t->tex);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w * sScale, h * sScale, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p_glGenFramebuffers(1, &t->fbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    }
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, sEfbFbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t->fbo);
    glDisable(GL_SCISSOR_TEST);
    p_glBlitFramebuffer(x * sScale, (EFB_H - y) * sScale, (x + w) * sScale, (EFB_H - y - h) * sScale, 0, 0, w * sScale,
                        h * sScale, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    gx_register_copy(addr, t->tex, w, h, fmt);
}

static void dump_bmp(const char* path, int w, int h) {
    uint8_t* px = (uint8_t*)malloc((size_t)w * h * 4);
    FILE* f = fopen(path, "wb");
    uint8_t hdr[54] = {'B', 'M'};
    int i;
    if (!f) {
        free(px);
        return;
    }
    glReadPixels(0, 0, w, h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
    *(uint32_t*)(hdr + 2) = 54 + w * h * 4;
    hdr[10] = 54;
    hdr[14] = 40;
    *(int32_t*)(hdr + 18) = w;
    *(int32_t*)(hdr + 22) = h;
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    for (i = 0; i < w * h; i++) px[i * 4 + 3] = 255;
    fwrite(px, 4, (size_t)w * h, f);
    fclose(f);
    free(px);
}

static void gxb_present_gl(int x, int y, int cw, int ch) {
    RECT r;
    MSG msg;
    int w, h;
    GetClientRect(sWnd, &r);
    w = r.right - r.left;
    h = r.bottom - r.top;
    /* The copied rectangle of the EFB, scaled to the window. */
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, sEfbFbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    p_glBlitFramebuffer(x * sScale, (EFB_H - y - ch) * sScale, (x + cw) * sScale, (EFB_H - y) * sScale, 0, 0, w, h,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
    if (sDumpDir && sPresented % (unsigned)sDumpEvery == 0) {
        char path[512];
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        snprintf(path, sizeof(path), "%s/frame_%05u.bmp", sDumpDir, sPresented);
        dump_bmp(path, w, h);
    }
    SwapBuffers(sDC);
    sPresented++;
    if (sPendingFreeCount) {
        glDeleteTextures(sPendingFreeCount, sPendingFree);
        sPendingFreeCount = 0;
    }
    {
        /* Frames per second and the longest frame of the last second, in
         * the window title and, with RECOMP_FPS=1, on stderr. */
        static LARGE_INTEGER freq, since, last;
        static unsigned frames;
        static double worst;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!freq.QuadPart) {
            QueryPerformanceFrequency(&freq);
            since = last = now;
        } else {
            double ms = (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart;
            if (ms > worst) worst = ms;
            frames++;
            last = now;
        }
        if (now.QuadPart - since.QuadPart >= freq.QuadPart) {
            char title[96];
            double fps = frames * (double)freq.QuadPart / (now.QuadPart - since.QuadPart);
            const char* e = getenv("RECOMP_FPS");
            /* The share of the second the game thread was not waiting
             * for an event: how much headroom is left. */
            extern uint64_t g_wait_ticks;
            static uint64_t lastWait;
            double secs = (double)(now.QuadPart - since.QuadPart) / freq.QuadPart;
            double busy = 100.0 * (1.0 - (double)(g_wait_ticks - lastWait) / PPC_TB_HZ / secs);
            lastWait = g_wait_ticks;
            snprintf(title, sizeof(title), "PokePark (recompiled) - %.2f fps, slowest frame %.1f ms, busy %.0f%%", fps,
                     worst, busy);
            SetWindowTextA(sWnd, title);
            if (e && *e == '1') fprintf(stderr, "fps: %.2f, slowest frame %.1f ms, busy %.0f%%\n", fps, worst, busy);
            if (e && *e == '1') print_gl_stats();
            since = now;
            frames = 0;
            worst = 0;
        }
    }
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* ------------------------------------------------------------------------ */
/* Entry points. Each marks the thread as inside OpenGL, so the sampling
 * profiler (boot.c) doesn't hold it suspended inside the driver. */

volatile long g_in_gl;

uint32_t gxb_texture(uint64_t key, int width, int height, const uint8_t* rgba, int wrap_s, int wrap_t, int linear) {
    uint32_t r;
    GL_ENTER();
    r = gxb_texture_gl(key, width, height, rgba, wrap_s, wrap_t, linear);
    GL_LEAVE(GLK_TEXTURE);
    return r;
}

void gxb_texture_update(uint32_t handle, int width, int height, const uint8_t* rgba) {
    GL_ENTER();
    gxb_texture_update_gl(handle, width, height, rgba);
    GL_LEAVE(GLK_TEXTURE_UPDATE);
}

void gxb_draw_triangles(const GXDrawState* st, const GXVertex* v, int count) {
    GL_ENTER();
    gxb_draw_triangles_gl(st, v, count);
    GL_LEAVE(GLK_DRAW);
}

void gxb_clear(int color, int alpha, int depth, uint32_t rgba, uint32_t z) {
    GL_ENTER();
    gxb_clear_gl(color, alpha, depth, rgba, z);
    GL_LEAVE(GLK_CLEAR);
}

void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int clear) {
    GL_ENTER();
    gxb_copy_to_texture_gl(addr, x, y, w, h, fmt, clear);
    GL_LEAVE(GLK_COPY);
}

void gxb_present(int x, int y, int cw, int ch) {
    GL_ENTER();
    gxb_present_gl(x, y, cw, ch);
    GL_LEAVE(GLK_PRESENT);
}
