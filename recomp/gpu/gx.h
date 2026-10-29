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

/* One vertex after transformation, ready for the backend. */
typedef struct GXVertex {
    float pos[4];      /* clip space, GL convention */
    float color[2][4]; /* rasterized channels 0 and 1, RGBA 0..1 */
    float tex[8][3];   /* texture coordinates s, t, q */
} GXVertex;

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
} GXDrawState;

/* Backend interface. */
void gxb_init(void);
uint32_t gxb_texture(uint64_t key, int width, int height, const uint8_t* rgba, int wrap_s, int wrap_t, int linear);
/* Replaces a texture's contents (same size). */
void gxb_texture_update(uint32_t handle, int width, int height, const uint8_t* rgba);
void gxb_texture_free(uint32_t handle);
void gxb_draw_triangles(const GXDrawState* st, const GXVertex* v, int count);
void gxb_clear(int color, int alpha, int depth, uint32_t rgba, uint32_t z);
void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int clear);
/* Shows the EFB rectangle the game copied to the XFB. */
void gxb_present(int x, int y, int w, int h);

/* Texture decoding (gx_texture.c). Output is RGBA8, row-major. */
int gx_texture_size(int fmt, int width, int height);
void gx_decode_texture(uint8_t* out, const uint8_t* src, int fmt, int width, int height,
                       const uint8_t* tlut, int tlut_fmt);

/* FIFO entry points (gx_fifo.c). */
void gx_init(void);
void gx_flush(void); /* decode everything written so far */
