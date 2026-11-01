/* What draws for the GX decoder: a graphics API (gl_backend.c) behind
 * gx_frames.c, which takes the gxb_* calls (gx.h), records frames to draw
 * them again between the game's, and paces them. A device only draws:
 * the EFB, textures, copies and the presented picture. The window it
 * shows them in is window_win32.c's.
 *
 * The device's calls are made on the game thread, with g_in_gl set
 * (gx_frames.c), as the sampling profiler wants. */
#pragma once

#include <stdint.h>

#include "gx.h"

#define EFB_W 640
#define EFB_H 528

/* Device (gl_backend.c). */
void gxd_init(void);
uint32_t gxd_texture(int width, int height, int levels, const uint8_t* rgba, uint32_t mode0);
void gxd_texture_update(uint32_t handle, int width, int height, int levels, const uint8_t* rgba);
void gxd_texture_delete(const uint32_t* handles, int n);
/* A draw as the decoder makes it: its vertices and indices with it, and
 * st->xf (the XF state) with the words changed since the last draw. */
void gxd_draw(const GXDrawState* st, const GXVertex* v, int first_vertex, int vertices, const uint32_t* index,
              int first_index, int count);
/* A recorded frame's vertices and indices, which gxd_draw_recorded draws
 * from until the next frame's. */
void gxd_frame_data(const GXVertex* v, int vertices, const uint32_t* index, int count);
/* A draw of the recorded frame, with its XF state as xf (GX_XF_WORDS
 * words) gives it. */
void gxd_draw_recorded(const GXDrawState* st, const uint32_t* xf, int first_vertex, int first_index, int count);
void gxd_clear(int color, int alpha, int depth, uint32_t rgba, uint32_t z);
/* The texture copies of a w x h rectangle to addr go to: a number for
 * gxd_copy, made the first time; -1 if there is no room for more. Its
 * texture handle is gxd_copy_texture's. */
int gxd_copy_target(uint32_t addr, int w, int h);
uint32_t gxd_copy_texture(int target);
void gxd_copy(int target, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha);
/* Shows the EFB rectangle x, y, w x h in the window, with the settings
 * menu over it while it's open; may_drop: when the display isn't ready
 * for it, the frame may be left out rather than waited for (1 returned). */
int gxd_present(int x, int y, int w, int h, int may_drop);
/* Takes up the display settings (internal resolution, anti-aliasing), as
 * the window now is; between frames. */
void gxd_apply_settings(void);

/* Window (window_win32.c). */
/* Makes the game's window, shown unless RECOMP_GX_HIDDEN=1, sized for the
 * 456 lines the game shows at twice their size. */
void win_create(void);
void* win_handle(void); /* HWND */
int win_hidden(void);
/* Width of an EFB pixel on screen, over its height: 1 in 4:3, wider in 16:9. */
double win_pixel_aspect(void);
/* The client area; 0 x 0 while minimized. */
void win_size(int* w, int* h);
/* Borderless, covering the monitor the window is on, or back to where the
 * window was. */
void win_set_fullscreen(int on);
void win_set_title(const char* title);
/* Handles the window's messages. */
void win_pump(void);
/* The refresh rate of the monitor the window is on; 0 if unknown. */
double win_display_hz(void);
