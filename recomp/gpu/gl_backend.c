/* OpenGL backend for the GX decoder.
 *
 * The EFB (the GPU's embedded framebuffer, 640x528) is an offscreen
 * framebuffer rendered at a multiple of the native resolution (the
 * settings' internal resolution, by default whatever fits the window),
 * wider again in 16:9, with multisample anti-aliasing if the
 * settings ask for it. TEV, the GPU's programmable color combiner, runs
 * in one "uber" fragment shader driven by uniforms, so any stage setup
 * draws without compiling a new shader. Copies of the EFB to the XFB are
 * presented in a window, or borderless fullscreen, at the shape they have
 * on a TV, with the settings menu over them while it is open;
 * RECOMP_GX_DUMP=<dir> also writes them as BMPs.
 *
 * Environment:
 *   RECOMP_GX_SCALE=n          internal resolution multiplier, over the settings
 *   RECOMP_GX_HIDDEN=1         do not show the window
 *   RECOMP_GX_DUMP=<dir>       write presented frames as BMP files
 *   RECOMP_GX_DUMP_EVERY=n     ...every nth frame (default 30) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>


#include "gx.h"
#include "host_settings.h"

#define EFB_W 640
#define EFB_H 528

/* ------------------------------------------------------------------------ */
/* GL 2+ entry points */

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
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
#define GL_LOWER_LEFT 0x8CA1
#define GL_ZERO_TO_ONE 0x935F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_MAX_SAMPLES 0x8D57
#define GL_BGRA 0x80E1

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
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint*)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glBlendEquation, (GLenum)) \
    X(void, glClipControl, (GLenum, GLenum)) \
    X(void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))

#define DECLARE(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name p_##name;
GL_FUNCS(DECLARE)
#undef DECLARE

static void gl_forget(void);
static uint64_t frame_due(void);
/* wglSwapIntervalEXT. Set to 0: presenting never waits for the display.
 * The game paces itself by its emulated retrace (and the frames between
 * by frame_render), and in a window or borderless window the compositor
 * shows only whole frames anyway. Waiting held up the game thread: with
 * a display whose refresh stalls (a virtual display with nobody
 * watching), every present took 110 ms and the game ran at 7 fps. */
static BOOL(WINAPI* pSwapInterval)(int);
static void frame_render(void);

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
    /* GX depth runs from -w (near) to 0 (far). The EFB depth is stored
     * reversed (see gxb_init), so the far plane, where GX scenes keep
     * nearly everything, gets the float precision: 0..w, far to near. */
    "  gl_Position = vec4(clip.x, clip.y, -clip.z, clip.w);\n"
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
static GLuint sProgram, sVao, sVbo, sIbo, sXfUbo;
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
    p_glGenBuffers(1, &sIbo);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sIbo); /* part of the vertex array's state */
    for (i = 0; i < 3; i++) {
        p_glEnableVertexAttribArray(i);
        p_glVertexAttribPointer(i, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GXVertex), (void*)(offsetof(GXVertex, mtx) + i * 4));
    }
    p_glEnableVertexAttribArray(3);
    p_glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, pos));
    p_glEnableVertexAttribArray(4);
    p_glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, nrm));
    p_glEnableVertexAttribArray(5);
    p_glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GXVertex), (void*)offsetof(GXVertex, color[0]));
    p_glEnableVertexAttribArray(6);
    p_glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GXVertex), (void*)offsetof(GXVertex, color[1]));
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

/* EFB copies: a full-screen triangle that samples the EFB rectangle and
 * converts it to the copy format, stored as the texture reads back. The
 * value of a narrow format is truncated to its bits and expanded again,
 * as the console's copy and texture fetch do. */
static const char* kCopyVertexShader =
    "#version 330 core\n"
    "uniform vec4 u_src;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) * 2, (gl_VertexID >> 1) * 2);\n"
    "    v_uv = mix(u_src.xy, u_src.zw, p);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* kCopyFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_efb;\n"
    "uniform ivec4 u_mode; /* copy format, intensity, EFB has alpha */\n"
    "float bits(float v, int n) {\n"
    "    float i = float(int(v * 255.0 + 0.5) >> (8 - n));\n"
    "    return floor(i * 255.0 / float((1 << n) - 1) + 0.5) / 255.0;\n"
    "}\n"
    "void main() {\n"
    "    vec4 c = texture(u_efb, v_uv);\n"
    "    int f = u_mode.x;\n"
    "    float i;\n"
    "    if (u_mode.z == 0) c.a = 1.0;\n"
    "    /* The intensity formats store luma (BT.601, studio range). */\n"
    "    i = u_mode.y != 0 ? clamp(dot(c.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0, 0.0, 1.0) : c.r;\n"
    "    if (f == 0) o_color = vec4(bits(i, 4));\n"
    "    else if (f == 1 || f == 8) o_color = vec4(i);\n"
    "    else if (f == 2) o_color = vec4(vec3(bits(i, 4)), bits(c.a, 4));\n"
    "    else if (f == 3) o_color = vec4(vec3(i), c.a);\n"
    "    else if (f == 4) o_color = vec4(bits(c.r, 5), bits(c.g, 6), bits(c.b, 5), 1.0);\n"
    "    else if (f == 5 && c.a >= 224.0 / 255.0) o_color = vec4(bits(c.r, 5), bits(c.g, 5), bits(c.b, 5), 1.0);\n"
    "    else if (f == 5) o_color = vec4(bits(c.r, 4), bits(c.g, 4), bits(c.b, 4), bits(c.a, 3));\n"
    "    else if (f == 7) o_color = vec4(c.a);\n"
    "    else if (f == 9) o_color = vec4(c.g);\n"
    "    else if (f == 10) o_color = vec4(c.b);\n"
    "    else if (f == 11) o_color = vec4(vec3(c.g), c.r); /* IA8 layout: A = R, I = G */\n"
    "    else if (f == 12) o_color = vec4(vec3(c.b), c.g);\n"
    "    else o_color = c;\n"
    "}\n";

static GLuint sCopyProgram, sCopyVao;
static GLint uCopySrc, uCopyMode;

static void build_copy_program(void) {
    GLint ok;
    sCopyProgram = p_glCreateProgram();
    p_glAttachShader(sCopyProgram, compile(GL_VERTEX_SHADER, kCopyVertexShader));
    p_glAttachShader(sCopyProgram, compile(GL_FRAGMENT_SHADER, kCopyFragmentShader));
    p_glLinkProgram(sCopyProgram);
    p_glGetProgramiv(sCopyProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(sCopyProgram, sizeof(log), NULL, log);
        fprintf(stderr, "gl: link error:\n%s\n", log);
        exit(1);
    }
    p_glUseProgram(sCopyProgram);
    p_glUniform1i(p_glGetUniformLocation(sCopyProgram, "u_efb"), 0);
    uCopySrc = p_glGetUniformLocation(sCopyProgram, "u_src");
    uCopyMode = p_glGetUniformLocation(sCopyProgram, "u_mode");
    p_glGenVertexArrays(1, &sCopyVao);
    p_glUseProgram(sProgram);
    p_glBindVertexArray(sVao);
}

/* The settings menu, an image with alpha laid over the presented picture.
 * Its rows run top to bottom. */
static const char* kOverlayVertexShader =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) * 2, (gl_VertexID >> 1) * 2);\n"
    "    v_uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* kOverlayFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_image;\n"
    "void main() { o_color = texture(u_image, v_uv); }\n";

static GLuint sOverlayProgram, sOverlayTex;

static void build_overlay_program(void) {
    GLint ok;
    sOverlayProgram = p_glCreateProgram();
    p_glAttachShader(sOverlayProgram, compile(GL_VERTEX_SHADER, kOverlayVertexShader));
    p_glAttachShader(sOverlayProgram, compile(GL_FRAGMENT_SHADER, kOverlayFragmentShader));
    p_glLinkProgram(sOverlayProgram);
    p_glGetProgramiv(sOverlayProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(sOverlayProgram, sizeof(log), NULL, log);
        fprintf(stderr, "gl: link error:\n%s\n", log);
        exit(1);
    }
    p_glUseProgram(sOverlayProgram);
    p_glUniform1i(p_glGetUniformLocation(sOverlayProgram, "u_image"), 0);
    p_glUseProgram(sProgram);
    glGenTextures(1, &sOverlayTex);
    glBindTexture(GL_TEXTURE_2D, sOverlayTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* ------------------------------------------------------------------------ */
/* Window and EFB */

static HWND sWnd;
static HDC sDC;
/* EFB pixels to GL pixels. In 16:9 the game squeezes a wide picture into
 * the EFB's 640 columns for the TV to stretch out: its cameras project a
 * 16:9 picture onto the 640x456 it shows. So the EFB gets as many more
 * pixels across, and is shown at 16:9: the stretched picture is then at
 * full resolution and fills a 16:9 screen. (Shown 4/3 as wide, 1.87:1,
 * it was 5% too wide, with bars above and below.) */
static double sScaleX = 2, sScaleY = 2;
static double sPixelAspect = 1; /* width of an EFB pixel on screen, over its height */
#define GLX(x) ((GLint)floor((x) * sScaleX + 0.5))
#define GLY(y) ((GLint)floor((y) * sScaleY + 0.5))
/* Draws go to sDrawFbo. Without anti-aliasing that is sEfbFbo, whose
 * color texture the copies and the presented picture read; with it, a
 * multisampled framebuffer resolved into sEfbFbo when they need it. */
static GLuint sEfbFbo, sEfbColor, sEfbDepth;
static GLuint sDrawFbo, sMsFbo, sMsColor, sMsDepth;
static int sMsDirty;
static GLuint sWhite;
static const char* sDumpDir;
static int sDumpEvery = 30;
static unsigned sPresented;
/* The settings as applied. */
static int sScale, sMsaa = 1, sMaxSamples = 1, sFullscreen, sHidden;
static WINDOWPLACEMENT sWindowed;

int display_scale(void) { return sScale; }
int display_msaa(void) {
    int m;
    for (m = g_settings.msaa; m > sMaxSamples; m /= 2) {}
    return m;
}

/* Borderless, covering the monitor the window is on; back to where the
 * window was before. */
static void set_fullscreen(int on) {
    DWORD style = (DWORD)GetWindowLongA(sWnd, GWL_STYLE);
    if (on == sFullscreen) return;
    sFullscreen = on;
    if (on) {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        sWindowed.length = sizeof(sWindowed);
        GetWindowPlacement(sWnd, &sWindowed);
        GetMonitorInfoA(MonitorFromWindow(sWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(sWnd, GWL_STYLE, (LONG)((style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(sWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(sWnd, GWL_STYLE, (LONG)((style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(sWnd, &sWindowed);
        SetWindowPos(sWnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) exit(0);
    /* Alt+Enter toggles fullscreen; Alt alone doesn't open the window
     * menu, which would hold the game's keys. */
    if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1 << 29))) {
        if (!(lp & (1 << 30))) {
            g_settings.fullscreen = !g_settings.fullscreen;
            settings_save();
        }
        return 0;
    }
    if (msg == WM_SYSCHAR || (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_KEYMENU)) return 0;
    if (menu_window_message(h, msg, wp, lp)) return 0;
    return DefWindowProcA(h, msg, wp, lp);
}

/* The internal resolution for a client area h pixels tall: RECOMP_GX_SCALE,
 * else the settings', else the multiple whose picture (456 lines shown)
 * comes nearest the window, rounding up unless within a quarter step. */
static int wanted_scale(int h) {
    const char* v = getenv("RECOMP_GX_SCALE");
    int s = v && atoi(v) > 0 ? atoi(v) : g_settings.scale;
    if (!s) s = (int)ceil(h / 456.0 - 0.25);
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

/* (Re)allocates the EFB at the current scale and sample count, and the
 * copy textures at the matching scale (keeping their names: the texture
 * cache holds them). What they held is lost; the game redraws it. */
static void alloc_efb(void);

static void apply_settings(void) {
    RECT r;
    int s, m;
    if (!sHidden) set_fullscreen(g_settings.fullscreen);
    GetClientRect(sWnd, &r);
    if (r.bottom - r.top <= 0) return; /* minimized */
    s = wanted_scale(r.bottom - r.top);
    for (m = g_settings.msaa; m > sMaxSamples; m /= 2) {}
    if (s == sScale && m == sMsaa) return;
    sScale = s;
    sMsaa = m;
    sScaleY = s;
    sScaleX = s * sPixelAspect;
    alloc_efb();
    fprintf(stderr, "gl: internal resolution %dx (%d x %d), anti-aliasing %s\n", s, GLX(EFB_W), GLY(EFB_H),
            m > 1 ? (m == 2 ? "2x" : m == 4 ? "4x" : "8x") : "off");
}

/* Brings sEfbFbo up to date with the multisampled draws. */
static void resolve(void) {
    gl_forget();
    if (sDrawFbo == sEfbFbo || !sMsDirty) return;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, sMsFbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sEfbFbo);
    glDisable(GL_SCISSOR_TEST);
    p_glBlitFramebuffer(0, 0, GLX(EFB_W), GLY(EFB_H), 0, 0, GLX(EFB_W), GLY(EFB_H), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
    sMsDirty = 0;
}

void gxb_init(void) {
    WNDCLASSA wc;
    PIXELFORMATDESCRIPTOR pfd;
    RECT r = {0, 0, 640 * 2, 456 * 2}; /* the 456 lines the game shows, so the picture fills it */
    HGLRC rc;
    const char* v;
    uint32_t white = 0xFFFFFFFF;
    extern int hle_widescreen(void);
    if (hle_widescreen()) {
        sPixelAspect = 16.0 / 9.0 * 456.0 / 640.0;
        r.right = (LONG)(r.right * sPixelAspect + 0.5);
    }
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
    sHidden = v && *v && *v != '0';
    if (!sHidden) ShowWindow(sWnd, SW_SHOW);
    load_gl();
    fprintf(stderr, "gl: %s, %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    glGetIntegerv(GL_MAX_SAMPLES, &sMaxSamples);
    build_program();
    build_copy_program();
    build_overlay_program();
    g_host_frame_due = frame_due;
    g_host_frame_render = frame_render;
    pSwapInterval = (BOOL(WINAPI*)(int))(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (pSwapInterval) pSwapInterval(0);

    glGenTextures(1, &sEfbColor);
    glBindTexture(GL_TEXTURE_2D, sEfbColor);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    p_glGenRenderbuffers(1, &sEfbDepth);
    p_glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    p_glGenFramebuffers(1, &sEfbFbo);
    apply_settings();

    glGenTextures(1, &sWhite);
    glBindTexture(GL_TEXTURE_2D, sWhite);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
}

/* ------------------------------------------------------------------------ */
/* Textures */

static GLint wrap_mode(int w) { return w == 0 ? GL_CLAMP_TO_EDGE : (w == 1 ? GL_REPEAT : GL_MIRRORED_REPEAT); }

static uint32_t gxb_texture_gl(uint64_t key, int width, int height, const uint8_t* rgba, int wrap_s, int wrap_t, int linear) {
    GLuint t;
    gl_forget();
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
    gl_forget();
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
/* Without an alpha channel (RGB8 and RGB565 EFB formats) the EFB's
 * alpha reads as 1: the bloom in Mew's intro weights its copies by it. */
static GLenum blend_factor(GLenum f, int efb_alpha) {
    if (efb_alpha) return f;
    if (f == GL_DST_ALPHA) return GL_ONE;
    if (f == GL_ONE_MINUS_DST_ALPHA) return GL_ZERO;
    return f;
}
/* GL state as draws last set it, so a draw changes only what differs
 * (consecutive batches differ in a few fields; each GL call costs driver
 * time). Anything else that changes GL state (copies, clears, presenting,
 * making textures) calls gl_forget, and the next draw sets it all. The
 * TEV uniforms belong to the draw program alone and are never forgotten. */
static struct {
    int valid;
    GLuint tex[8];
    int blend;
    GLenum blend_src, blend_dst, blend_eq;
    GLboolean color_mask[4], depth_mask;
    int depth;
    GLenum depth_func;
    int cull;
    GLenum cull_face;
    GLint viewport[4], scissor[4];
    double depth_range[2];
} sGl;

static void gl_forget(void) { sGl.valid = 0; }

/* Uploads a uniform array only when it differs from what was uploaded. */
#define UNIFORM_IF_CHANGED(fn, loc, n, values, last)                                                          \
    do {                                                                                                     \
        if (!sUniformsSet || memcmp(values, last, (size_t)(n) * 4 * sizeof(values[0])) != 0) {               \
            memcpy(last, values, (size_t)(n) * 4 * sizeof(values[0]));                                       \
            fn(loc, n, values);                                                                              \
        }                                                                                                    \
    } while (0)

/* Mirrored for the reversed depth. */
static const GLenum kDepthFunc[8] = {GL_NEVER, GL_GREATER, GL_EQUAL, GL_GEQUAL, GL_LESS, GL_NOTEQUAL, GL_LEQUAL, GL_ALWAYS};

/* Sets everything a draw needs but its XF state and vertices. */
static void apply_draw_state(const GXDrawState* st) {
    static GLint lcin[64], lain[64], lcop[64], laop[64], lmap[64], lsel[64], lswap[16], lalpha[4];
    static GLfloat lreg[16], lkonst[16], laref[4];
    static int sUniformsSet, lstages;
    GLint cin[64], ain[64], cop[64], aop[64], map[64], sel[64], swap[16], alpha[4];
    GLfloat reg[16], konst[16], aref[4];
    int i, all = !sGl.valid;
    sMsDirty = 1;
    if (all) {
        p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
        p_glUseProgram(sProgram);
        p_glBindVertexArray(sVao);
        p_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
        p_glBindBuffer(GL_UNIFORM_BUFFER, sXfUbo);
    }

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

    if (!sUniformsSet || lstages != st->num_stages) {
        p_glUniform1i(uNumStages, st->num_stages);
        lstages = st->num_stages;
        sUniformsSet = 0; /* the arrays' lengths changed: upload them all */
    }
    UNIFORM_IF_CHANGED(p_glUniform4iv, uCin, st->num_stages, cin, lcin);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uAin, st->num_stages, ain, lain);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uCop, st->num_stages, cop, lcop);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uAop, st->num_stages, aop, laop);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uMap, st->num_stages, map, lmap);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uSel, st->num_stages, sel, lsel);
    UNIFORM_IF_CHANGED(p_glUniform4fv, uReg, 4, reg, lreg);
    UNIFORM_IF_CHANGED(p_glUniform4fv, uKonst, 4, konst, lkonst);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uSwap, 4, swap, lswap);
    UNIFORM_IF_CHANGED(p_glUniform4iv, uAlpha, 1, alpha, lalpha);
    UNIFORM_IF_CHANGED(p_glUniform4fv, uAlphaRef, 1, aref, laref);
    sUniformsSet = 1;

    for (i = 0; i < 8; i++) {
        GLuint t = st->texture[i] ? st->texture[i] : sWhite;
        if (all || sGl.tex[i] != t) {
            p_glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, t);
            sGl.tex[i] = t;
        }
    }
    p_glActiveTexture(GL_TEXTURE0);

    {
        int blend = st->blend_enable;
        GLenum src = blend_factor(kBlendSrc[st->blend_src], st->efb_alpha);
        GLenum dst = blend_factor(kBlendDst[st->blend_dst], st->efb_alpha);
        GLenum eq = st->blend_subtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD;
        if (st->blend_subtract) src = dst = GL_ONE;
        if (all || sGl.blend != blend) {
            if (blend) glEnable(GL_BLEND);
            else glDisable(GL_BLEND);
            sGl.blend = blend;
        }
        if (blend && (all || sGl.blend_src != src || sGl.blend_dst != dst)) {
            glBlendFunc(src, dst);
            sGl.blend_src = src;
            sGl.blend_dst = dst;
        }
        if (blend && (all || sGl.blend_eq != eq)) {
            p_glBlendEquation(eq);
            sGl.blend_eq = eq;
        }
        if (!blend && all) sGl.blend_src = sGl.blend_dst = sGl.blend_eq = 0; /* set again when blending is on */
    }
    {
        GLboolean c = (GLboolean)st->color_update, a = (GLboolean)(st->alpha_update && st->efb_alpha);
        if (all || sGl.color_mask[0] != c || sGl.color_mask[3] != a) {
            glColorMask(c, c, c, a);
            sGl.color_mask[0] = sGl.color_mask[1] = sGl.color_mask[2] = c;
            sGl.color_mask[3] = a;
        }
    }
    {
        int depth = st->z_enable;
        GLboolean mask = depth && st->z_update ? GL_TRUE : GL_FALSE;
        if (all || sGl.depth != depth) {
            if (depth) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);
            sGl.depth = depth;
        }
        if (depth && (all || sGl.depth_func != kDepthFunc[st->z_func])) {
            glDepthFunc(kDepthFunc[st->z_func]);
            sGl.depth_func = kDepthFunc[st->z_func];
        }
        if (!depth && all) sGl.depth_func = 0;
        if (all || sGl.depth_mask != mask) {
            glDepthMask(mask);
            sGl.depth_mask = mask;
        }
    }
    {
        /* Edges are scaled and rounded, rather than the size, so
         * neighbouring rectangles still meet at a fractional scale. */
        GLint vp[4], sc[4];
        double dr[2];
        vp[0] = GLX(st->viewport[0]);
        vp[1] = GLY(EFB_H - (st->viewport[1] + st->viewport[3]));
        vp[2] = GLX(st->viewport[0] + st->viewport[2]) - GLX(st->viewport[0]);
        vp[3] = GLY(EFB_H - st->viewport[1]) - GLY(EFB_H - (st->viewport[1] + st->viewport[3]));
        dr[0] = 1.0 - (st->viewport[5] > 1 ? 1 : st->viewport[5]);
        dr[1] = 1.0 - (st->viewport[4] < 0 ? 0 : st->viewport[4]);
        sc[0] = GLX(st->scissor[0]);
        sc[1] = GLY(EFB_H - st->scissor[3]);
        sc[2] = GLX(st->scissor[2]) - GLX(st->scissor[0]);
        sc[3] = GLY(EFB_H - st->scissor[1]) - GLY(EFB_H - st->scissor[3]);
        if (all || memcmp(vp, sGl.viewport, sizeof(vp)) != 0) {
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            memcpy(sGl.viewport, vp, sizeof(vp));
        }
        if (all || memcmp(dr, sGl.depth_range, sizeof(dr)) != 0) {
            glDepthRange(dr[0], dr[1]);
            memcpy(sGl.depth_range, dr, sizeof(dr));
        }
        if (all) glEnable(GL_SCISSOR_TEST);
        if (all || memcmp(sc, sGl.scissor, sizeof(sc)) != 0) {
            glScissor(sc[0], sc[1], sc[2], sc[3]);
            memcpy(sGl.scissor, sc, sizeof(sc));
        }
    }
    /* Face culling (GX_CULL_NONE, FRONT, BACK, ALL). GX's front faces wind
     * counter-clockwise in GL's window coordinates, GL's default. Games
     * rely on it: the picture confirmation dialog hides a placeholder pane
     * by turning it away from the camera. */
    {
        static const GLenum kCull[4] = {GL_BACK, GL_FRONT, GL_BACK, GL_FRONT_AND_BACK};
        int cull = st->cull_mode != 0;
        if (all || sGl.cull != cull) {
            if (cull) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);
            sGl.cull = cull;
        }
        if (cull && (all || sGl.cull_face != kCull[st->cull_mode])) {
            glCullFace(kCull[st->cull_mode]);
            sGl.cull_face = kCull[st->cull_mode];
        }
        if (!cull && all) sGl.cull_face = 0;
    }
    sGl.valid = 1;
}

/* The XF uniform buffer holds what the decoder's draws last left in it,
 * unless frames were drawn again from a recording since: then its next
 * draw sends all of it (sXfStale), and the recording's draws go by their
 * own copy of what it holds (sXfShadow). */
static int sXfStale;
static uint32_t sXfShadow[GX_XF_WORDS];
static int sXfShadowValid;

static void gxb_draw_triangles_gl(const GXDrawState* st, const GXVertex* v, int first_vertex, int vertices,
                                  const uint32_t* index, int first_index, int count) {
    apply_draw_state(st);
    if (st->xf && sXfStale) {
        p_glBufferSubData(GL_UNIFORM_BUFFER, 0, GX_XF_WORDS * 4, st->xf);
        sXfStale = 0;
    } else if (st->xf && st->xf_dirty_lo < st->xf_dirty_hi) {
        /* Only the XF words written since the last draw. */
        p_glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)st->xf_dirty_lo * 4,
                          (GLsizeiptr)(st->xf_dirty_hi - st->xf_dirty_lo) * 4, st->xf + st->xf_dirty_lo);
    }
    sXfShadowValid = 0;
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertices * sizeof(GXVertex), v + first_vertex, GL_STREAM_DRAW);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(uint32_t), index + first_index, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, NULL);
}

static void gxb_clear_gl(int color, int alpha, int depth, uint32_t rgba, uint32_t z) {
    GLbitfield mask = 0;
    gl_forget();
    p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
    sMsDirty = 1;
    glDisable(GL_SCISSOR_TEST);
    glColorMask(color, color, color, alpha);
    glDepthMask(depth ? GL_TRUE : GL_FALSE);
    glClearColor(((rgba >> 24) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f, ((rgba >> 8) & 0xFF) / 255.0f,
                 (rgba & 0xFF) / 255.0f);
    glClearDepth(1.0 - z / 16777215.0);
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

/* The texture that copies of a w x h rectangle to addr go to, made the
 * first time. */
static CopyTarget* copy_target(uint32_t addr, int w, int h) {
    CopyTarget* t = NULL;
    int i;
    gl_forget();
    for (i = 0; i < sTargetCount; i++) {
        if (sTargets[i].addr == addr && sTargets[i].w == w && sTargets[i].h == h) t = &sTargets[i];
    }
    if (!t) {
        if (sTargetCount == 64) return NULL;
        t = &sTargets[sTargetCount++];
        t->addr = addr;
        t->w = w;
        t->h = h;
        glGenTextures(1, &t->tex);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLX(w), GLY(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p_glGenFramebuffers(1, &t->fbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
        p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
    }
    return t;
}

/* Draws the EFB rectangle into the copy's texture. */
static void copy_draw(CopyTarget* t, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha) {
    GLint mode[4];
    GLfloat src[4];
    gl_forget();
    resolve();
    /* Row 0 of the texture is the top row of the rectangle. */
    p_glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, GLX(w), GLY(h));
    p_glUseProgram(sCopyProgram);
    p_glBindVertexArray(sCopyVao);
    p_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sEfbColor);
    src[0] = (float)x / EFB_W;
    src[1] = (float)(EFB_H - y) / EFB_H;
    src[2] = (float)(x + w) / EFB_W;
    src[3] = (float)(EFB_H - y - h) / EFB_H;
    p_glUniform4fv(uCopySrc, 1, src);
    mode[0] = fmt;
    mode[1] = intensity;
    mode[2] = efb_alpha;
    mode[3] = 0;
    p_glUniform4iv(uCopyMode, 1, mode);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glUseProgram(sProgram);
    p_glBindVertexArray(sVao);
    p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
}

static void gxb_copy_to_texture_gl(uint32_t addr, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha) {
    CopyTarget* t = copy_target(addr, w, h);
    if (!t) return;
    copy_draw(t, x, y, w, h, fmt, intensity, efb_alpha);
    gx_register_copy(addr, t->tex, w, h, fmt);
}

static void alloc_efb(void) {
    int w = GLX(EFB_W), h = GLY(EFB_H), i;
    gl_forget();
    glBindTexture(GL_TEXTURE_2D, sEfbColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    p_glBindRenderbuffer(GL_RENDERBUFFER, sEfbDepth);
    /* Depth is stored reversed, 1 - GX depth, in a float buffer: GX
     * scenes put their depths within a few millionths of 1, where a 24-bit
     * buffer fed by float math can't tell surfaces one GX step apart (the
     * picture in the picture confirmation dialog lost to its frame). */
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, sMsaa > 1 ? 1 : w, sMsaa > 1 ? 1 : h);
    p_glBindFramebuffer(GL_FRAMEBUFFER, sEfbFbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sEfbColor, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sMsaa > 1 ? 0 : sEfbDepth);
    if (sMsFbo) {
        p_glDeleteFramebuffers(1, &sMsFbo);
        p_glDeleteRenderbuffers(1, &sMsColor);
        p_glDeleteRenderbuffers(1, &sMsDepth);
        sMsFbo = sMsColor = sMsDepth = 0;
    }
    sDrawFbo = sEfbFbo;
    if (sMsaa > 1) {
        p_glGenRenderbuffers(1, &sMsColor);
        p_glBindRenderbuffer(GL_RENDERBUFFER, sMsColor);
        p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, sMsaa, GL_RGBA8, w, h);
        p_glGenRenderbuffers(1, &sMsDepth);
        p_glBindRenderbuffer(GL_RENDERBUFFER, sMsDepth);
        p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, sMsaa, GL_DEPTH_COMPONENT32F, w, h);
        p_glGenFramebuffers(1, &sMsFbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, sMsFbo);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sMsColor);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sMsDepth);
        sDrawFbo = sMsFbo;
    }
    for (i = 0; i < sTargetCount; i++) {
        glBindTexture(GL_TEXTURE_2D, sTargets[i].tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLX(sTargets[i].w), GLY(sTargets[i].h), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClearDepth(0.0); /* the far plane, reversed */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sMsDirty = 0;
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

static void present_output(int x, int y, int cw, int ch);
static void gxb_present_gl(int x, int y, int cw, int ch) { present_output(x, y, cw, ch); }

static void present_output(int x, int y, int cw, int ch) {
    RECT r;
    MSG msg;
    int w, h, dw, dh;
    double aspect;
    gl_forget();
    GetClientRect(sWnd, &r);
    w = r.right - r.left;
    h = r.bottom - r.top;
    /* The copied rectangle of the EFB, as large as fits in the window at
     * the shape it has on a TV (an EFB pixel is square in 4:3, wider in
     * 16:9), with black bars on the sides left over. */
    aspect = ch > 0 ? cw * sPixelAspect / ch : 1;
    dw = w;
    dh = (int)(w / aspect + 0.5);
    if (dh > h) {
        dh = h;
        dw = (int)(h * aspect + 0.5);
    }
    resolve();
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, sEfbFbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    p_glBlitFramebuffer(GLX(x), GLY(EFB_H - y - ch), GLX(x + cw), GLY(EFB_H - y), (w - dw) / 2, (h - dh) / 2,
                        (w - dw) / 2 + dw, (h - dh) / 2 + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    /* Nothing slow belongs in here: presenting happens in the GX flush at
     * a retrace, before the retrace interrupt, and a few milliseconds more
     * every frame (reading a controller that isn't plugged in, for one)
     * can leave the game showing every fourth retrace (15 fps). */
    {
        const uint32_t* menu = menu_draw(w, h);
        if (menu) {
            p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
            p_glUseProgram(sOverlayProgram);
            p_glBindVertexArray(sCopyVao);
            p_glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sOverlayTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, menu);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            p_glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDisable(GL_BLEND);
            p_glUseProgram(sProgram);
            p_glBindVertexArray(sVao);
        }
    }
    if (sDumpDir && sPresented % (unsigned)sDumpEvery == 0) {
        char path[512];
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        snprintf(path, sizeof(path), "%s/frame_%05u.bmp", sDumpDir, sPresented);
        dump_bmp(path, w, h);
    }
    SwapBuffers(sDC);
    sPresented++;
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
    /* Between frames, so a change doesn't lose the one being drawn. */
    apply_settings();
    p_glBindFramebuffer(GL_FRAMEBUFFER, sDrawFbo);
}

/* ------------------------------------------------------------------------ */
/* Frames between the game's
 *
 * Above 60 fps the game still draws 60 frames a second (hle_game.c), and
 * the frames between are drawn here: the newest of the game's frames is
 * recorded (its draws with their state and XF state, clears, EFB copies)
 * and drawn again, each draw's matrices blended from those of the same
 * draw in the frame before, by how far through the frame interval the
 * moment shown is. Shown frames run one game frame behind.
 *
 * A draw is recognized across frames by its key (gx_fifo.c: its display
 * lists and their draws) and, among draws with the same key, by order.
 * Blended are the position, normal and texture matrices, the
 * post-transform matrices, the projection and the lights' position and
 * direction: everything that moves with the camera or the model. A
 * matrix whose rotation or translation changed too much to be the same
 * object moving (draws matched wrongly, a cut) is shown as it is. Draws
 * with no match, and everything else (vertices, textures), are the
 * newest frame's. */

enum { OP_DRAW, OP_CLEAR, OP_COPY, OP_PRESENT };

typedef struct RecOp {
    int op;
    GXDrawState st;   /* OP_DRAW (st.xf unused) */
    int xf;           /* OP_DRAW: its XF state, at words xf of the frame's */
    int match;        /* OP_DRAW: the last frame's op of the same draw, or -1 */
    int a[8];         /* the other ops' arguments; OP_DRAW: first vertex, vertices, first index, count */
} RecOp;

typedef struct Frame {
    RecOp* ops;
    int count, cap;
    uint32_t* xf;
    int xf_words, xf_cap;
} Frame;

/* Recording, the newest complete frame and the one before. */
static Frame sFrames[3];
static Frame *sRec = &sFrames[0], *sCur = &sFrames[1], *sPrev = &sFrames[2];
static int sOutputRate = 60, sRecording, sHaveFrame;
static uint64_t sCurAt, sNextOut, sFramePeriod; /* time base */
static double sLastT = -1;
/* Textures freed while a frame was recorded are still drawn by its
 * replays until the next frame is complete. */
static GLuint sFreeLater[MAX_PENDING_FREES];
static int sFreeLaterCount;

static RecOp* rec_op(int op) {
    RecOp* r;
    if (sRec->count == sRec->cap) {
        sRec->cap = sRec->cap ? sRec->cap * 2 : 1024;
        sRec->ops = (RecOp*)realloc(sRec->ops, (size_t)sRec->cap * sizeof(RecOp));
    }
    r = &sRec->ops[sRec->count++];
    r->op = op;
    r->match = -1;
    return r;
}

static void rec_draw(const GXDrawState* st, int first_vertex, int vertices, int first_index, int count) {
    RecOp* r = rec_op(OP_DRAW);
    r->st = *st;
    r->st.xf = NULL;
    if (sRec->xf_words + GX_XF_WORDS > sRec->xf_cap) {
        sRec->xf_cap = (sRec->xf_words + GX_XF_WORDS) * 2;
        sRec->xf = (uint32_t*)realloc(sRec->xf, (size_t)sRec->xf_cap * 4);
    }
    r->xf = sRec->xf_words;
    memcpy(sRec->xf + sRec->xf_words, st->xf, GX_XF_WORDS * 4);
    sRec->xf_words += GX_XF_WORDS;
    r->a[0] = first_vertex;
    r->a[1] = vertices;
    r->a[2] = first_index;
    r->a[3] = count;
}

/* Pairs each draw of sCur with the same draw of sPrev: the first not yet
 * taken with the same key. */
static void match_draws(void) {
    enum { SLOTS = 8192 };
    static uint64_t keys[SLOTS];
    static uint8_t used[SLOTS];
    static int head[SLOTS], *next;
    static int nextCap;
    int i;
    memset(used, 0, sizeof(used));
    if (sPrev->count > nextCap) {
        nextCap = sPrev->count * 2;
        next = (int*)realloc(next, (size_t)nextCap * sizeof(int));
    }
    /* Chains in frame order: walk backwards, pushing on the front. */
    for (i = sPrev->count - 1; i >= 0; i--) {
        uint64_t k = sPrev->ops[i].st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        if (sPrev->ops[i].op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (!used[slot]) {
            used[slot] = 1;
            keys[slot] = k;
            head[slot] = -1;
        }
        next[i] = head[slot];
        head[slot] = i;
    }
    for (i = 0; i < sCur->count; i++) {
        RecOp* r = &sCur->ops[i];
        uint64_t k = r->st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        r->match = -1;
        if (r->op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (used[slot] && head[slot] >= 0) {
            r->match = head[slot];
            head[slot] = next[head[slot]];
        }
    }
}

static float wordf(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static uint32_t fword(float f) {
    uint32_t w;
    memcpy(&w, &f, 4);
    return w;
}

static void lerp_words(uint32_t* out, const uint32_t* a, const uint32_t* b, int from, int to, float t) {
    int i;
    for (i = from; i < to; i++) {
        if (a[i] != b[i]) out[i] = fword(wordf(a[i]) + (wordf(b[i]) - wordf(a[i])) * t);
    }
}

/* Blends the XF state of a draw from prev (the frame before) to cur. */
static void blend_xf(uint32_t* out, const uint32_t* prev, const uint32_t* cur, float t) {
    int m, j;
    memcpy(out, cur, GX_XF_WORDS * 4);
    /* Position and texture matrices, 3x4, at multiples of 12 words. */
    for (m = 0; m + 12 <= 0x100; m += 12) {
        const uint32_t *a = prev + m, *b = cur + m;
        float scale = 0, rot = 0, dt = 0, tlen = 0;
        int same = 1;
        for (j = 0; j < 12; j++) {
            float x = wordf(a[j]), y = wordf(b[j]);
            if (a[j] != b[j]) same = 0;
            if (!(x == x) || !(y == y)) { same = -1; break; } /* not floats */
            if ((j & 3) == 3) {
                dt += (y - x) * (y - x);
                tlen += y * y;
            } else {
                if (fabsf(y) > scale) scale = fabsf(y);
                if (fabsf(y - x) > rot) rot = fabsf(y - x);
            }
        }
        if (same) continue;
        /* The same object moving turns and moves a little in a frame;
         * another object (a wrong match, a cut) differs wholesale. */
        if (rot > 0.3f * scale || sqrtf(dt) > 0.5f * sqrtf(tlen) + 200.0f) continue;
        lerp_words(out, prev, cur, m, m + 12, t);
    }
    lerp_words(out, prev, cur, 0x400, 0x460, t); /* normal matrices */
    lerp_words(out, prev, cur, 0x500, 0x600, t); /* post-transform matrices */
    for (j = 0; j < 8; j++) lerp_words(out, prev, cur, 0x600 + 16 * j + 10, 0x600 + 16 * j + 16, t); /* lights */
    if (prev[GX_XF_MEM_WORDS + 0x26] == cur[GX_XF_MEM_WORDS + 0x26]) {
        lerp_words(out, prev, cur, GX_XF_MEM_WORDS + 0x20, GX_XF_MEM_WORDS + 0x26, t); /* projection */
    }
}

/* Sends the words of xf that differ from what the buffer holds. */
static void upload_xf(const uint32_t* xf) {
    int lo = 0, hi = GX_XF_WORDS;
    if (sXfShadowValid) {
        while (lo < GX_XF_WORDS && xf[lo] == sXfShadow[lo]) lo++;
        if (lo == GX_XF_WORDS) return;
        while (hi > lo && xf[hi - 1] == sXfShadow[hi - 1]) hi--;
    }
    p_glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)lo * 4, (GLsizeiptr)(hi - lo) * 4, xf + lo);
    memcpy(sXfShadow + lo, xf + lo, (size_t)(hi - lo) * 4);
    sXfShadowValid = 1;
    sXfStale = 1;
}

static void present_output(int x, int y, int cw, int ch);

/* Draws sCur, its matrices t of the way from sPrev's. */
static void render_frame(float t) {
    static uint32_t xf[GX_XF_WORDS];
    int i;
    for (i = 0; i < sCur->count; i++) {
        const RecOp* r = &sCur->ops[i];
        switch (r->op) {
        case OP_DRAW: {
            const uint32_t* cur = sCur->xf + r->xf;
            if (r->match >= 0 && t < 1.0f) {
                blend_xf(xf, sPrev->xf + sPrev->ops[r->match].xf, cur, t);
                cur = xf;
            }
            apply_draw_state(&r->st);
            upload_xf(cur);
            p_glDrawElementsBaseVertex(GL_TRIANGLES, r->a[3], GL_UNSIGNED_INT, (void*)((size_t)r->a[2] * 4), r->a[0]);
            break;
        }
        case OP_CLEAR: gxb_clear_gl(r->a[0], r->a[1], r->a[2], (uint32_t)r->a[3], (uint32_t)r->a[4]); break;
        case OP_COPY:
            copy_draw(&sTargets[r->a[0]], r->a[1], r->a[2], r->a[3], r->a[4], r->a[5], r->a[6], r->a[7]);
            break;
        default: present_output(r->a[0], r->a[1], r->a[2], r->a[3]); break;
        }
    }
}

static void free_textures(GLuint* list, int* count) {
    if (*count) {
        glDeleteTextures(*count, list);
        *count = 0;
    }
}

/* The game finished a frame: it becomes the one shown. */
static void frame_end(void) {
    Frame* f = sPrev;
    uint64_t now = ppc_timebase();
    sPrev = sCur;
    sCur = sRec;
    sRec = f;
    sRec->count = sRec->xf_words = 0;
    match_draws();
    if (sHaveFrame) {
        /* The game's frame interval, smoothed, within reason. */
        uint64_t d = now - sCurAt, lo = PPC_TB_HZ / 120, hi = PPC_TB_HZ / 20;
        if (d < lo) d = lo;
        if (d > hi) d = hi;
        sFramePeriod = sFramePeriod ? (sFramePeriod * 7 + d) / 8 : d;
    } else {
        sFramePeriod = PPC_TB_HZ / 60;
        sNextOut = now;
    }
    sCurAt = now;
    sHaveFrame = 1;
    sLastT = -1;
    free_textures(sFreeLater, &sFreeLaterCount);
    memcpy(sFreeLater, sPendingFree, (size_t)sPendingFreeCount * sizeof(GLuint));
    sFreeLaterCount = sPendingFreeCount;
    sPendingFreeCount = 0;
}

static uint64_t frame_due(void) { return sRecording && sHaveFrame ? sNextOut : 0; }

/* The refresh rate of the monitor the window is on (the compositor's own
 * rate is another monitor's when there are several); 0 if unknown. Whole
 * numbers just under a multiple of 10 are the NTSC-style rates (119 is
 * 119.88 Hz). Asked again every few seconds, as the window can move. */
static double display_hz(void) {
    static double hz;
    static DWORD checked;
    DWORD now = GetTickCount();
    MONITORINFOEXA mi;
    DEVMODEA dm;
    if (checked && now - checked < 3000) return hz;
    checked = now ? now : 1;
    hz = 0;
    mi.cbSize = sizeof(mi);
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoA(MonitorFromWindow(sWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi) &&
        EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
        hz = dm.dmDisplayFrequency;
        if (dm.dmDisplayFrequency % 10 == 9) hz = (hz + 1) * 1000.0 / 1001.0;
    }
    return hz;
}

/* Frames shown per second: the setting, or the display's refresh rate if
 * lower (more would never be seen). */
static double output_rate(void) {
    static int ignore = -1;
    double hz;
    if (ignore < 0) ignore = getenv("RECOMP_IGNORE_REFRESH") != NULL; /* for measuring */
    hz = ignore ? 0 : display_hz();
    return hz >= 30 && hz < sOutputRate ? hz : (double)sOutputRate;
}

int gxb_display_hz(void) { return (int)(display_hz() + 0.5); }

static void frame_render(void) {
    uint64_t now = ppc_timebase(), step = (uint64_t)(PPC_TB_HZ / output_rate());
    float t = (float)((double)(int64_t)(now - sCurAt) / (double)sFramePeriod);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    sNextOut += step;
    if ((int64_t)(now - sNextOut) > (int64_t)step) sNextOut = now + step; /* fell behind: start again from now */
    if (t == 1.0f && sLastT == 1.0f) return; /* the game is late: nothing new to show */
    sLastT = t;
    GL_ENTER();
    render_frame(t);
    GL_LEAVE(GLK_PRESENT);
}

int gxb_recording(void) {
    int want = sOutputRate > 60;
    if (sRecording && !want && sHaveFrame) {
        /* Stopping: show the newest frame as it is. */
        GL_ENTER();
        render_frame(1.0f);
        GL_LEAVE(GLK_PRESENT);
    }
    if (want != sRecording) {
        sRecording = want;
        sHaveFrame = 0;
        sRec->count = sRec->xf_words = 0;
        sCur->count = sCur->xf_words = 0;
        sPrev->count = sPrev->xf_words = 0;
        sXfShadowValid = 0;
        sXfStale = 1;
    }
    return sRecording;
}

void gxb_set_output_rate(int fps) { sOutputRate = fps < 30 ? 30 : fps > 360 ? 360 : fps; }

void gxb_frame_data(const GXVertex* v, int vertices, const uint32_t* index, int count) {
    GL_ENTER();
    p_glBindVertexArray(sVao);
    p_glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertices * sizeof(GXVertex), v, GL_STREAM_DRAW);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(uint32_t), index, GL_STREAM_DRAW);
    GL_LEAVE(GLK_DRAW);
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

void gxb_draw_triangles(const GXDrawState* st, const GXVertex* v, int first_vertex, int vertices, const uint32_t* index,
                        int first_index, int count) {
    if (sRecording) {
        rec_draw(st, first_vertex, vertices, first_index, count);
        return;
    }
    GL_ENTER();
    gxb_draw_triangles_gl(st, v, first_vertex, vertices, index, first_index, count);
    GL_LEAVE(GLK_DRAW);
}

void gxb_clear(int color, int alpha, int depth, uint32_t rgba, uint32_t z) {
    if (sRecording) {
        RecOp* r = rec_op(OP_CLEAR);
        r->a[0] = color;
        r->a[1] = alpha;
        r->a[2] = depth;
        r->a[3] = (int)rgba;
        r->a[4] = (int)z;
        return;
    }
    GL_ENTER();
    gxb_clear_gl(color, alpha, depth, rgba, z);
    GL_LEAVE(GLK_CLEAR);
}

void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha) {
    if (sRecording) {
        /* The texture is made now, for the draws that follow to name;
         * the copy into it is drawn with the frame. */
        CopyTarget* t;
        RecOp* r;
        GL_ENTER();
        t = copy_target(addr, w, h);
        GL_LEAVE(GLK_COPY);
        if (!t) return;
        r = rec_op(OP_COPY);
        r->a[0] = (int)(t - sTargets);
        r->a[1] = x;
        r->a[2] = y;
        r->a[3] = w;
        r->a[4] = h;
        r->a[5] = fmt;
        r->a[6] = intensity;
        r->a[7] = efb_alpha;
        gx_register_copy(addr, t->tex, w, h, fmt);
        return;
    }
    GL_ENTER();
    gxb_copy_to_texture_gl(addr, x, y, w, h, fmt, intensity, efb_alpha);
    GL_LEAVE(GLK_COPY);
}

void gxb_present(int x, int y, int cw, int ch) {
    if (sRecording) {
        RecOp* r = rec_op(OP_PRESENT);
        r->a[0] = x;
        r->a[1] = y;
        r->a[2] = cw;
        r->a[3] = ch;
        GL_ENTER();
        frame_end();
        GL_LEAVE(GLK_PRESENT);
        return;
    }
    GL_ENTER();
    gxb_present_gl(x, y, cw, ch);
    free_textures(sPendingFree, &sPendingFreeCount);
    GL_LEAVE(GLK_PRESENT);
}
