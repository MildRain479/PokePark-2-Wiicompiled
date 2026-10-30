/* GX, the Wii's GPU (Flipper/Hollywood), decoded from its command FIFO.
 *
 * The game writes GX commands to the write-gather pipe (0xCC008000). The
 * decoder (gx_fifo.c) keeps the GPU's register state (CP: vertex formats,
 * XF: transforms, BP: rasterizer, textures and TEV) and turns draw
 * commands into triangles in clip space, which a backend (gl_backend.c)
 * draws. Textures are decoded to RGBA8 by gx_texture.c. */
#pragma once

#include <stdint.h>

#include "ppc.h"

/* One vertex as the game sent it, before transformation: the backend's
 * vertex shader transforms it with the XF state (matrices, lights,
 * texture coordinate generation), as the GPU's transform unit does. */
typedef struct GXVertex {
    uint8_t mtx[12];     /* position matrix index, texture matrix indices 0..7, unused */
    float pos[3];        /* object space */
    float nrm[3];
    uint8_t color[2][4]; /* vertex colors 0 and 1, RGBA (255 when absent) */
    float tex[8][2];     /* texture coordinates as sent */
} GXVertex;

/* The XF state as the vertex shader reads it: XF memory words 0x000-0x67F
 * (matrices, normal matrices, post-transform matrices, lights) followed by
 * XF registers 0x1000-0x105F. */
#define GX_XF_MEM_WORDS 0x680
#define GX_XF_REG_WORDS 0x60
#define GX_XF_WORDS (GX_XF_MEM_WORDS + GX_XF_REG_WORDS)

/* One TEV stage, unpacked from the BP registers. */
typedef struct GXTevStage {
    uint8_t color_in[4], color_op, color_bias, color_scale, color_clamp, color_dest;
    uint8_t alpha_in[4], alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_dest;
    uint8_t texmap, texcoord, tex_enable, channel;
    uint8_t kcolor_sel, kalpha_sel;
    uint8_t ras_swap, tex_swap;
} GXTevStage;

/* Everything the backend needs to draw with the current state. */
typedef struct GXDrawState {
    int num_stages;
    GXTevStage stage[16];
    float tev_reg[4][4];   /* PREV, REG0..REG2 initial values (RGBA) */
    float konst[4][4];     /* KONST0..3 */
    uint8_t swap_table[4][4];
    int num_texcoords;
    uint32_t texture[8];   /* backend texture handles, 0 = none */
    int alpha_func[2], alpha_ref[2], alpha_logic;
    int blend_enable, blend_src, blend_dst, blend_subtract, logic_enable, logic_op;
    int color_update, alpha_update;
    int z_enable, z_func, z_update;
    int cull_mode;
    float viewport[6];     /* x, y, width, height, near, far in EFB pixels */
    int scissor[4];        /* x0, y0, x1, y1 */
    int efb_alpha;         /* the EFB pixel format has an alpha channel (RGBA6_Z24) */
    const uint32_t* xf;    /* GX_XF_WORDS words, laid out as above */
    int xf_dirty_lo, xf_dirty_hi; /* words changed since the last draw (lo >= hi: none) */
    /* What the batch draws, the same from frame to frame (its display
     * lists and their draws), for matching it with the last frame's. */
    uint64_t key;
} GXDrawState;

/* Backend interface. */
void gxb_init(void);
uint32_t gxb_texture(uint64_t key, int width, int height, const uint8_t* rgba, int wrap_s, int wrap_t, int linear);
/* Replaces a texture's contents (same size). */
void gxb_texture_update(uint32_t handle, int width, int height, const uint8_t* rgba);
void gxb_texture_free(uint32_t handle);
/* Draws triangles: count indices from index + first_index, three to a
 * triangle, into the vertices from v + first_vertex. */
void gxb_draw_triangles(const GXDrawState* st, const GXVertex* v, int first_vertex, int vertices, const uint32_t* index,
                        int first_index, int count);
/* Whether the backend records frames to draw them again (interpolation):
 * then the decoder keeps a whole frame's vertices and indices, draws give
 * their place in them, and gxb_frame_data hands them over before the
 * frame is presented. Asked at the start of each frame. */
int gxb_recording(void);
void gxb_frame_data(const GXVertex* v, int vertices, const uint32_t* index, int count);
void gxb_clear(int color, int alpha, int depth, uint32_t rgba, uint32_t z);
/* Copies an EFB rectangle to a texture, converted to copy format `fmt`
 * (exec bits 3-6) and stored as the game reads it back: an I8 copy as
 * intensity in every channel, an RGB565 copy with opaque alpha. */
void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha);
/* Shows the EFB rectangle the game copied to the XFB. */
void gxb_present(int x, int y, int w, int h);

/* Texture decoding (gx_texture.c). Output is RGBA8, row-major. */
int gx_texture_size(int fmt, int width, int height);
void gx_decode_texture(uint8_t* out, const uint8_t* src, int fmt, int width, int height,
                       const uint8_t* tlut, int tlut_fmt);

/* FIFO entry points (gx_fifo.c). */
void gx_init(void);
void gx_flush(void); /* decode everything written so far */
