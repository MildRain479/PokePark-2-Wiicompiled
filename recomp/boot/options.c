/* The Options tab of the pause menu: the settings (settings.c), in a
 * book like the PokéPark Pad's and drawn with its pictures and the game's
 * font, over the Pad. While the Pad is on its main page an "Options" tab
 * shows beside its title; right opens it (left and right do nothing on
 * the Pad), and left or Back on its first column goes back.
 *
 *   The left page lists the sections, the right page their settings.
 *   Up and down choose; left and right change a setting or move between a
 *   binding's two slots; Select rebinds (press the key, mouse button or
 *   controller button, or Esc to cancel); Delete, or Y, clears.
 *
 * Its controls are the game's, as bound (Select is 2, Back is 1, as the
 * Pad's bar says), and the arrow keys, Enter and Esc; the mouse clicks
 * what it points at. The book is drawn into an image laid over the
 * picture (gl_backend.c), only again when something on it changes,
 * leaving the bar at the bottom clear: the game's, which shows the
 * player's keys (hle_prompts.c), shows through. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "host_input.h"
#include "host_settings.h"

int hle_pause_main_page(void); /* hle_pause.c */
int hle_file_select_shown(void);
const uint32_t* hle_pause_image(const char* name, const char* alpha, int* w, int* h);
int hle_font_metrics(int* cellW, int* cellH, int* baseline); /* hle_prompts.c */
int hle_font_glyph(uint16_t code, uint8_t* cov, int* left, int* advance);
int display_scale(void); /* gx_frames.c: as applied */
int display_msaa(void);
int display_renderer(void); /* RENDERER_*: the one drawing */
int gxb_display_hz(void);

/* ------------------------------------------------------------------------ */
/* What's on it */

#ifdef __ANDROID__
/* No keyboard: its section isn't there. Dual screen is, in its place,
 * only on a handheld with two screens. */
enum { SEC_DISPLAY, SEC_CONTROLLER, SEC_GAMEPLAY, SEC_DUAL, SECTIONS, SEC_KEYBOARD = -1 };
static const char* const kSection[SECTIONS] = {"Display", "Controller", "Gameplay", "Dual screen"};
static const int kSectionColor[SECTIONS] = {0, 2, 3, 1}; /* which of the Pad's button colors */
static int sections(void) { return second_screen_device() ? SECTIONS : SEC_DUAL; }
#else
enum { SEC_DISPLAY, SEC_KEYBOARD, SEC_CONTROLLER, SEC_GAMEPLAY, SECTIONS, SEC_DUAL = -1 };
static const char* const kSection[SECTIONS] = {"Display", "Keyboard", "Controller", "Gameplay"};
static const int kSectionColor[SECTIONS] = {0, 1, 2, 3};
static int sections(void) { return SECTIONS; }
#endif

/* A row: a setting (settings_step), a binding (an action), resetting the
 * controls, or closing the game (selected twice, as unsaved progress is
 * lost). */
enum { ROW_SETTING, ROW_BINDING, ROW_RESET, ROW_CLOSE };
typedef struct {
    int kind, which;
    const char* label;
} Row;

/* On Android the game fills the screen, and Vulkan always draws. */
static const Row kDisplay[] = {
#ifndef __ANDROID__
    {ROW_SETTING, SET_FULLSCREEN, "Fullscreen"},
#endif
    {ROW_SETTING, SET_SCALE, "Resolution"},
    {ROW_SETTING, SET_MSAA, "Anti-aliasing"},     {ROW_SETTING, SET_FRAME_RATE, "Frame rate"},
    {ROW_SETTING, SET_FOV, "Field of view"},       {ROW_SETTING, SET_ANISO, "Filtering"},
    {ROW_SETTING, SET_MOVIES, "Movies"},
#ifndef __ANDROID__
    {ROW_SETTING, SET_RENDERER, "Renderer"},
#endif
    {ROW_SETTING, SET_UPDATES, "Updates"},
    {ROW_CLOSE, 0, "Close game"},
};
static const Row kGameplay[] = {
    {ROW_SETTING, SET_TEXT_SPEED, "Text speed"},
    {ROW_SETTING, SET_BERRIES, "Berries"},
    {ROW_SETTING, SET_PRICES, "Prices"},
    {ROW_SETTING, SET_DAMAGE_DEALT, "Damage dealt"},
    {ROW_SETTING, SET_DAMAGE_TAKEN, "Damage taken"},
    {ROW_SETTING, SET_CHASE_SPEED, "Chase speed"},
};
static const Row kDual[] = {
    {ROW_SETTING, SET_DUAL_SCREEN, "Dual screen"},
    {ROW_SETTING, SET_SWAP_SCREENS, "Swap screens"},
    {ROW_SETTING, SET_MINIMAP_TOP, "Map on main"},
};
static const char* const kAction[ACT_COUNT] = {
    "Up", "Down", "Left", "Right", "2: Jump", "1: Dash", "A: Call", "B: Look", "+: Pad", "-: Photos", "HOME",
    "Shake", "Shake 2",
};

/* Android's Controller section starts with the touch controls' row, where
 * a touch reaches it (touches don't scroll). */
#ifdef __ANDROID__
#define TOUCH_ROW 1
#else
#define TOUCH_ROW 0
#endif

static int row_count(int sec) {
    return sec == SEC_DISPLAY    ? (int)(sizeof(kDisplay) / sizeof(kDisplay[0]))
           : sec == SEC_GAMEPLAY ? (int)(sizeof(kGameplay) / sizeof(kGameplay[0]))
           : sec == SEC_DUAL     ? (int)(sizeof(kDual) / sizeof(kDual[0]))
           : sec == SEC_KEYBOARD ? ACT_COUNT + 3 /* camera speed, invert, reset */
                                 : TOUCH_ROW + ACT_COUNT + 2; /* (touch controls,) rumble, reset */
}

static Row row_at(int sec, int i) {
    Row r = {ROW_RESET, 0, "Reset all controls"};
    if (sec == SEC_DISPLAY) return kDisplay[i];
    if (sec == SEC_GAMEPLAY) return kGameplay[i];
    if (sec == SEC_DUAL) return kDual[i];
    if (TOUCH_ROW && sec == SEC_CONTROLLER && i-- == 0) {
        r.kind = ROW_SETTING;
        r.which = SET_TOUCH;
        r.label = "Touch controls";
        return r;
    }
    if (i < ACT_COUNT) {
        r.kind = ROW_BINDING;
        r.which = i;
        r.label = kAction[i];
    } else if (sec == SEC_KEYBOARD && i == ACT_COUNT) {
        r.kind = ROW_SETTING;
        r.which = SET_SENSITIVITY;
        r.label = "Camera speed";
    } else if (sec == SEC_CONTROLLER && i == ACT_COUNT) {
        r.kind = ROW_SETTING;
        r.which = SET_RUMBLE;
        r.label = "Rumble";
    } else if (sec == SEC_KEYBOARD && i == ACT_COUNT + 1) {
        r.kind = ROW_SETTING;
        r.which = SET_INVERT;
        r.label = "Invert camera";
    }
    return r;
}

static void setting_text(int which, char* out, size_t n) {
    int s;
    switch (which) {
    case SET_FULLSCREEN: snprintf(out, n, g_settings.fullscreen ? "On" : "Off"); break;
    case SET_SCALE:
        s = g_settings.scale ? g_settings.scale : display_scale();
        snprintf(out, n, g_settings.scale ? "%dx" : "Auto (%dx)", s);
        break;
    case SET_MSAA:
        if (g_settings.msaa == 1) snprintf(out, n, "Off");
        else if (display_msaa() < g_settings.msaa) snprintf(out, n, "%dx (GPU %dx)", g_settings.msaa, display_msaa());
        else snprintf(out, n, "%dx", g_settings.msaa);
        break;
    case SET_FRAME_RATE: snprintf(out, n, "%d fps", g_settings.frame_rate); break;
    case SET_FOV: snprintf(out, n, "%d%%", g_settings.fov); break;
    case SET_SENSITIVITY: snprintf(out, n, "%.1f", g_settings.mouse_sensitivity); break;
    case SET_INVERT: snprintf(out, n, g_settings.invert_tilt ? "On" : "Off"); break;
    case SET_TOUCH: snprintf(out, n, g_settings.touch_controls ? "On" : "Off"); break;
    case SET_MINIMAP_TOP: snprintf(out, n, g_settings.minimap_top ? "On" : "Off"); break;
    case SET_DUAL_SCREEN: snprintf(out, n, g_settings.dual_screen ? "On" : "Off"); break;
    case SET_SWAP_SCREENS: snprintf(out, n, g_settings.swap_screens ? "On" : "Off"); break;
    case SET_ANISO: snprintf(out, n, g_settings.aniso == 1 ? "Original" : "%dx", g_settings.aniso); break;
    case SET_MOVIES: {
        static const char* const kMovies[3] = {"Original", "Sharpened", "AI upscaled"};
        snprintf(out, n, "%s", kMovies[g_settings.movie_upscale % 3]);
        break;
    }
    case SET_RENDERER:
        snprintf(out, n, "%s%s", g_settings.renderer == RENDERER_VULKAN ? "Vulkan" : "OpenGL",
                 g_settings.renderer != display_renderer() ? " (restart)" : "");
        break;
    case SET_RUMBLE: {
        static const char* const kRumble[4] = {"Off", "Light", "Normal", "Strong"};
        snprintf(out, n, "%s", kRumble[g_settings.rumble & 3]);
        break;
    }
    case SET_TEXT_SPEED:
        if (g_settings.text_speed == 0) snprintf(out, n, "Instant");
        else snprintf(out, n, g_settings.text_speed == 1 ? "Original" : "%dx", g_settings.text_speed);
        break;
    case SET_BERRIES: snprintf(out, n, g_settings.berries == 100 ? "Original" : "%d%%", g_settings.berries); break;
    case SET_PRICES:
        if (g_settings.prices == 0) snprintf(out, n, "Free");
        else snprintf(out, n, g_settings.prices == 100 ? "Original" : "%d%%", g_settings.prices);
        break;
    case SET_DAMAGE_DEALT:
        snprintf(out, n, g_settings.damage_dealt == 100 ? "Original" : "%d%%", g_settings.damage_dealt);
        break;
    case SET_DAMAGE_TAKEN:
        if (g_settings.damage_taken == 0) snprintf(out, n, "None");
        else snprintf(out, n, g_settings.damage_taken == 100 ? "Original" : "%d%%", g_settings.damage_taken);
        break;
    case SET_CHASE_SPEED:
        snprintf(out, n, g_settings.chase_speed == 100 ? "Original" : "%d%%", g_settings.chase_speed);
        break;
    case SET_UPDATES:
        s = update_state();
        if (s == UPDATE_NEWER) snprintf(out, n, "%s is out", update_tag());
        else snprintf(out, n, "%s", s == UPDATE_OFF ? "Off" : s == UPDATE_CHECKING ? "Checking" : s == UPDATE_CURRENT ? "Up to date" : "Can't check");
        break;
    default: *out = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* State */

enum { CAPTURE_NONE, CAPTURE_KEY, CAPTURE_PAD };
#define VISIBLE_ROWS 8

static volatile int sOpen;
/* What it was opened from: the Pad, or the save file screen. */
enum { FROM_PAD, FROM_FILES };
static int sFrom;
static int sSection, sOnRows, sRow, sSlot, sTop, sCapture;
static volatile unsigned sChanges; /* what's drawn is redrawn when this changes */

int options_is_open(void) { return sOpen; }

static void changed(void) { sChanges++; }

/* "Close game" was selected once, and nothing has changed since. */
static unsigned sCloseArmed = ~0u;
static int close_armed(void) { return sCloseArmed == sChanges; }

/* Whether what it opens from is showing. */
static int shown(int from) { return from == FROM_PAD ? hle_pause_main_page() : hle_file_select_shown(); }

static void set_open(int open);

static void open_from(int from) {
    sFrom = from;
    set_open(1);
}

static void set_open(int open) {
    sOpen = open;
    sCapture = CAPTURE_NONE;
    sOnRows = 0;
    changed();
}

static void show_row(void) {
    if (sRow < sTop) sTop = sRow;
    if (sRow >= sTop + VISIBLE_ROWS) sTop = sRow - VISIBLE_ROWS + 1;
}

static void up_down(int d) {
    if (!sOnRows) {
        sSection = (sSection + d + sections()) % sections();
        sRow = sTop = sSlot = 0;
    } else {
        sRow = (sRow + d + row_count(sSection)) % row_count(sSection);
        show_row();
    }
    changed();
}

static void enter_rows(void) {
    sOnRows = 1;
    changed();
}

static void back(void) {
    if (sOnRows) {
        sOnRows = 0;
        changed();
    } else {
        set_open(0);
    }
}

static void left_right(int d) {
    Row r;
    if (!sOnRows) {
        if (d < 0) set_open(0);
        else enter_rows();
        return;
    }
    r = row_at(sSection, sRow);
    if (r.kind == ROW_SETTING) {
        settings_step(r.which, d);
    } else if (r.kind == ROW_BINDING && sSlot + d >= 0 && sSlot + d < BIND_SLOTS) {
        sSlot += d;
    } else if (d < 0) {
        sOnRows = 0;
    }
    changed();
}

static void select_(void) {
    Row r;
    if (!sOnRows) {
        enter_rows();
        return;
    }
    r = row_at(sSection, sRow);
    if (r.kind == ROW_SETTING && r.which == SET_UPDATES && update_state() == UPDATE_NEWER) {
        update_open(); /* its page, to download it from */
    } else if (r.kind == ROW_SETTING) {
        settings_step(r.which, 1);
    } else if (r.kind == ROW_BINDING) {
        sCapture = sSection == SEC_CONTROLLER ? CAPTURE_PAD : CAPTURE_KEY;
    } else if (r.kind == ROW_CLOSE) {
        if (close_armed()) exit(0); /* as closing the window does */
        changed();
        sCloseArmed = sChanges;
        return;
    } else {
        settings_default_controls();
        settings_save();
    }
    changed();
}

static void clear(void) {
    Row r = row_at(sSection, sRow);
    if (!sOnRows || r.kind != ROW_BINDING) return;
    if (sSection == SEC_CONTROLLER) g_settings.pad[r.which][sSlot] = 0;
    else g_settings.key[r.which][sSlot] = 0;
    settings_save();
    changed();
}

static void bind(int vk, uint32_t pad) {
    settings_bind(row_at(sSection, sRow).which, sSlot, vk, pad);
    sCapture = CAPTURE_NONE;
    changed();
}

/* ------------------------------------------------------------------------ */
/* Where things are: in a 1621 x 912 picture, where the Pad was measured,
 * scaled to the window's height and centred, as the game's picture is. */

#define REF_W 1621
#define REF_H 912

typedef struct {
    float x0, y0, x1, y1;
} Rect;

/* Its Pad, from its layouts (below, draw_pages). */
static const Rect kTab = {960, 38, 1262, 100}; /* the other tab, over the right page's top */
/* the sections' buttons' frames (MnPsTop's Frame0n, at LX, LY below) */
static const Rect kButton[4] = {{330.6f, 167.2f, 747.8f, 250.8f},
                                {349.0f, 269.8f, 766.2f, 353.4f},
                                {330.6f, 374.3f, 747.8f, 457.9f},
                                {349.0f, 476.9f, 766.2f, 560.5f}};
static const Rect kPage = {846, 110, 1300, 800}; /* the right page's writing */
#define ROWS_TOP 236.0f
#define ROW_PITCH 64.0f
#define ROW_HEIGHT 54.0f

static float sScale, sOffX, sOffY;

static float X(float x) { return sOffX + x * sScale; }
static float Y(float y) { return sOffY + y * sScale; }

static Rect row_rect(int visible) {
    Rect r = {kPage.x0, ROWS_TOP + visible * ROW_PITCH, kPage.x1, ROWS_TOP + visible * ROW_PITCH + ROW_HEIGHT};
    return r;
}

/* Where a binding row's slot k is centred. */
static float slot_x(int k) { return k ? 1238.0f : 1122.0f; }

/* ------------------------------------------------------------------------ */
/* Drawing: an image, top row first, 0xAARRGGBB, not premultiplied */

static uint32_t* sPx;
static int sW, sH;

static void blend(uint32_t* d, uint32_t c, float alpha) {
    float sa = (c >> 24) / 255.0f * alpha, da = (*d >> 24) / 255.0f, oa = sa + da * (1 - sa);
    int k;
    uint32_t out;
    if (sa <= 0) return;
    out = (uint32_t)(oa * 255 + 0.5f) << 24;
    for (k = 0; k < 24; k += 8) {
        float sc = (float)(c >> k & 255), dc = (float)(*d >> k & 255);
        out |= (uint32_t)((sc * sa + dc * da * (1 - sa)) / oa + 0.5f) << k;
    }
    *d = out;
}

static uint32_t sample(const uint32_t* img, int w, int h, float u, float v) {
    int x0 = (int)floorf(u - 0.5f), y0 = (int)floorf(v - 0.5f), k, i;
    float fx = u - 0.5f - x0, fy = v - 0.5f - y0, acc[4] = {0}, wsum = 0;
    for (i = 0; i < 4; i++) {
        int x = x0 + (i & 1), y = y0 + (i >> 1);
        float wt = ((i & 1) ? fx : 1 - fx) * ((i >> 1) ? fy : 1 - fy);
        uint32_t c;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= w) x = w - 1;
        if (y >= h) y = h - 1;
        c = img[y * w + x];
        /* premultiplied while filtering, so clear texels add no colour */
        acc[3] += wt * (c >> 24);
        for (k = 0; k < 3; k++) acc[k] += wt * (c >> (8 * k) & 255) * (c >> 24) / 255.0f;
        wsum += wt;
    }
    if (acc[3] < 0.5f) return 0;
    return (uint32_t)(acc[3] + 0.5f) << 24 | (uint32_t)(acc[2] * 255 / acc[3] + 0.5f) << 16 |
           (uint32_t)(acc[1] * 255 / acc[3] + 0.5f) << 8 | (uint32_t)(acc[0] * 255 / acc[3] + 0.5f);
}

/* Draws part src of an image into dst (picture coordinates), its colour
 * times tint (0xRRGGBB), flipped left to right if mirror. */
static void draw_image(const uint32_t* img, int w, int h, Rect src, Rect dst, uint32_t tint, int mirror) {
    int x0 = (int)X(dst.x0), y0 = (int)Y(dst.y0), x1 = (int)ceilf(X(dst.x1)), y1 = (int)ceilf(Y(dst.y1)), x, y;
    float dw = X(dst.x1) - X(dst.x0), dh = Y(dst.y1) - Y(dst.y0);
    if (!img || dw <= 0 || dh <= 0) return;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < sH; y++) {
        float v = src.y0 + (y + 0.5f - Y(dst.y0)) / dh * (src.y1 - src.y0);
        for (x = x0 < 0 ? 0 : x0; x < x1 && x < sW; x++) {
            float t = (x + 0.5f - X(dst.x0)) / dw, u = src.x0 + (mirror ? 1 - t : t) * (src.x1 - src.x0);
            uint32_t c;
            if (t < 0 || t > 1 || v < src.y0 || v > src.y1) continue;
            c = sample(img, w, h, u, v);
            if (tint != 0xFFFFFF) {
                int k;
                uint32_t o = c & 0xFF000000u;
                for (k = 0; k < 24; k += 8) o |= ((c >> k & 255) * (tint >> k & 255) / 255) << k;
                c = o;
            }
            blend(&sPx[y * sW + x], c, 1.0f);
        }
    }
}

static void draw_picture(const char* name, const char* alpha, Rect src, Rect dst, uint32_t tint) {
    int w, h;
    const uint32_t* img = hle_pause_image(name, alpha, &w, &h);
    if (src.x1 <= 0) {
        src.x0 = src.y0 = 0;
        src.x1 = (float)w;
        src.y1 = (float)h;
    }
    draw_image(img, w, h, src, dst, tint, 0);
}

/* How far window pixel (px, py) is outside rounded box r (negative
 * inside), and (qx, qy), how far past its straight part on each axis. */
static float box_distance(Rect r, float radius, float px, float py, float* qx, float* qy) {
    float rad = radius * sScale, hx = (X(r.x1) - X(r.x0)) / 2, hy = (Y(r.y1) - Y(r.y0)) / 2;
    float ax, ay;
    if (rad > hx) rad = hx;
    if (rad > hy) rad = hy;
    *qx = fabsf(px - (X(r.x0) + hx)) - (hx - rad);
    *qy = fabsf(py - (Y(r.y0) + hy)) - (hy - rad);
    ax = *qx > 0 ? *qx : 0;
    ay = *qy > 0 ? *qy : 0;
    return sqrtf(ax * ax + ay * ay) + (*qx > *qy ? (*qx < 0 ? *qx : 0) : (*qy < 0 ? *qy : 0)) - rad;
}

/* A rounded box shaded from c0 to c1 (0xAARRGGBB), top to bottom or, if
 * across, left to right; its edge softened. */
static void draw_box_shaded(Rect r, float radius, uint32_t c0, uint32_t c1, int across) {
    int x, y, k;
    for (y = (int)Y(r.y0) < 0 ? 0 : (int)Y(r.y0); y < (int)ceilf(Y(r.y1)) && y < sH; y++) {
        for (x = (int)X(r.x0) < 0 ? 0 : (int)X(r.x0); x < (int)ceilf(X(r.x1)) && x < sW; x++) {
            float qx, qy, d = box_distance(r, radius, x + 0.5f, y + 0.5f, &qx, &qy);
            float a = d < -1.5f ? 1.0f : d > 0 ? 0.0f : -d / 1.5f, t;
            uint32_t c = c0;
            if (a <= 0) continue;
            if (c1 != c0) {
                t = across ? (x + 0.5f - X(r.x0)) / (X(r.x1) - X(r.x0)) : (y + 0.5f - Y(r.y0)) / (Y(r.y1) - Y(r.y0));
                c = 0;
                for (k = 0; k < 32; k += 8)
                    c |= (uint32_t)((c0 >> k & 255) + ((float)(c1 >> k & 255) - (c0 >> k & 255)) * t + 0.5f) << k;
            }
            blend(&sPx[y * sW + x], c, a);
        }
    }
}

/* A rounded box of colour c (0xAARRGGBB), its edge softened. */
static void draw_box(Rect r, float radius, uint32_t c) { draw_box_shaded(r, radius, c, c, 0); }

/* A triangle pointing up or down (dir 1, -1), or left or right (-2, 2),
 * centred at (cx, cy). */
static void draw_triangle(float cx, float cy, float size, int dir, uint32_t c) {
    int x, y;
    for (y = (int)Y(cy - size); y < (int)Y(cy + size) + 1; y++) {
        for (x = (int)X(cx - size); x < (int)X(cx + size) + 1; x++) {
            float u = ((x + 0.5f) - X(cx)) / (size * sScale), v = ((y + 0.5f) - Y(cy)) / (size * sScale), t;
            if (x < 0 || y < 0 || x >= sW || y >= sH) continue;
            if (dir == 2 || dir == -2) {
                t = u;
                u = v;
                v = -t * dir / 2;
            } else {
                v *= dir;
            }
            if (v >= -0.6f && v <= 0.6f && fabsf(u) <= (v + 0.6f) / 1.2f) blend(&sPx[y * sW + x], c, 1.0f);
        }
    }
}

/* Text, in the game's font: size is its cell height in picture units. */
static int sCellW, sCellH, sBaseline;

static float text_width(const char* s, float size) {
    static uint8_t cov[64 * 64];
    float w = 0;
    int left, advance;
    for (; *s; s++) {
        if (hle_font_glyph((uint8_t)*s, cov, &left, &advance)) w += advance;
    }
    return w * size / sCellH;
}

/* Paints a coverage mask (bw x bh, 0-1) at window pixel (bx0, by0): the
 * edge, then the fill, each the coverage grown by a disc. */
static void paint_mask(const float* mask, int bw, int bh, int bx0, int by0, uint32_t fill, uint32_t edge,
                       float outline, int bold) {
    int j;
    for (j = 0; j < 2; j++) {
        float r = j == 0 ? outline * sScale + (bold ? 0.6f * sScale : 0) : (bold ? 0.6f * sScale : 0);
        uint32_t c = j == 0 ? edge : fill;
        int ri = (int)ceilf(r), a, b;
        if (j == 0 && outline <= 0) continue;
        for (b = 0; b < bh; b++) {
            int py = by0 + b;
            if (py < 0 || py >= sH) continue;
            for (a = 0; a < bw; a++) {
                int px = bx0 + a, dx, dy;
                float m = 0;
                if (px < 0 || px >= sW) continue;
                for (dy = -ri; dy <= ri; dy++) {
                    for (dx = -ri; dx <= ri; dx++) {
                        float d = sqrtf((float)(dx * dx + dy * dy)), w, v;
                        if (d > r + 1 || a + dx < 0 || b + dy < 0 || a + dx >= bw || b + dy >= bh) continue;
                        w = d <= r ? 1.0f : r + 1 - d;
                        v = mask[(b + dy) * bw + a + dx] * w;
                        if (v > m) m = v;
                    }
                }
                if (m > 0) blend(&sPx[py * sW + px], c, m > 1 ? 1 : m);
            }
        }
    }
}

enum { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };

/* Draws s with its middle at y, filled with fill, edged outline wide (in
 * picture units) with edge, and, if bold, thickened a little. Shrinks it
 * to fit in maxWidth. */
static void draw_text(const char* s, float x, float y, float size, int align, uint32_t fill, uint32_t edge,
                      float outline, int bold, float maxWidth) {
    static uint8_t cov[64 * 64];
    static float* mask;
    static int maskSize;
    float width = text_width(s, size), k, pen;
    int bx0, by0, bw, bh, i, j, pad;
    if (maxWidth > 0 && width > maxWidth) {
        size *= maxWidth / width;
        width = maxWidth;
    }
    k = size / sCellH;
    x = align == ALIGN_CENTRE ? x - width / 2 : align == ALIGN_RIGHT ? x - width : x;
    pad = (int)ceilf((outline + 2) * sScale) + 1;
    bx0 = (int)X(x) - pad;
    by0 = (int)Y(y - size / 2) - pad;
    bw = (int)(width * sScale) + 2 * pad + 2;
    bh = (int)(size * sScale) + 2 * pad + 2;
    if (bw * bh > maskSize) {
        free(mask);
        maskSize = bw * bh;
        mask = (float*)malloc((size_t)maskSize * sizeof(float));
        if (!mask) {
            maskSize = 0;
            return;
        }
    }
    memset(mask, 0, (size_t)bw * bh * sizeof(float));
    /* The glyphs' coverage, scaled. */
    for (pen = x; *s; s++) {
        int left, advance, gx0, gy0, gx1, gy1;
        if (!hle_font_glyph((uint8_t)*s, cov, &left, &advance)) continue;
        gx0 = (int)X(pen + left * k) - bx0;
        gy0 = (int)Y(y - size / 2) - by0;
        gx1 = gx0 + (int)(sCellW * k * sScale) + 1;
        gy1 = gy0 + (int)(size * sScale) + 1;
        for (j = gy0 < 0 ? 0 : gy0; j < gy1 && j < bh; j++) {
            float v = (j - gy0 + 0.5f) / (k * sScale);
            for (i = gx0 < 0 ? 0 : gx0; i < gx1 && i < bw; i++) {
                float u = (i - gx0 + 0.5f) / (k * sScale), c;
                int ux = (int)u, vy = (int)v;
                float fx = u - ux, fy = v - vy;
                if (ux >= sCellW || vy >= sCellH) continue;
                c = cov[vy * sCellW + ux] * (1 - fx) * (1 - fy) +
                    (ux + 1 < sCellW ? cov[vy * sCellW + ux + 1] : 0) * fx * (1 - fy) +
                    (vy + 1 < sCellH ? cov[(vy + 1) * sCellW + ux] : 0) * (1 - fx) * fy +
                    (ux + 1 < sCellW && vy + 1 < sCellH ? cov[(vy + 1) * sCellW + ux + 1] : 0) * fx * fy;
                c /= 255.0f;
                if (c > mask[j * bw + i]) mask[j * bw + i] = c;
            }
        }
        pen += advance * k;
    }
    paint_mask(mask, bw, bh, bx0, by0, fill, edge, outline, bold);
}

/* ------------------------------------------------------------------------ */
/* The book */

#define BROWN 0xFF8C2A10u
#define GREEN 0xFF448822u
#define CREAM 0xFFF2EAD2u
#define INK 0xFF2A2A2Au
#define PURPLE 0xFF5B2A9Eu
#define WAIT 0xFFE05A00u

static const Rect kWhole = {0, 0, 0, 0};

/* The icon of what's bound to a remote button (HOST_PAD_*), white with
 * an edge, in dst; 0 if there's none. */
static int draw_button_icon(uint32_t button, Rect dst, uint32_t edge) {
    static uint8_t cov[96 * 96];
    static float mask[160 * 160];
    int n = 96, bw, bh, bx0, by0, i, j, pad = (int)ceilf(4 * sScale) + 1;
    float dw = X(dst.x1) - X(dst.x0), dh = Y(dst.y1) - Y(dst.y0);
    if (!g_host_prompt_icon || !g_host_prompt_icon(button, cov, n, n)) return 0;
    bw = (int)dw + 2 * pad;
    bh = (int)dh + 2 * pad;
    if (bw * bh > (int)(sizeof(mask) / sizeof(mask[0]))) return 0;
    bx0 = (int)X(dst.x0) - pad;
    by0 = (int)Y(dst.y0) - pad;
    memset(mask, 0, sizeof(mask));
    for (j = pad; j < bh - pad; j++) {
        for (i = pad; i < bw - pad; i++) {
            int u = (int)((i - pad + 0.5f) / dw * n), v = (int)((j - pad + 0.5f) / dh * n);
            if (u < n && v < n) mask[j * bw + i] = cov[v * n + u] / 255.0f;
        }
    }
    paint_mask(mask, bw, bh, bx0, by0, 0xFFFFFFFFu, edge, 3.0f, 0);
    return 1;
}

/* The book as the Pad is: its pieces are its layouts' (MnPsBook, MnPsTop,
 * MnPsCaption in Archive/MenuField.dac), drawn from their textures
 * (hle_pad_art.c) where the layouts put them. Layout units are 878 x 480,
 * y up, about the picture's middle; the Pad sits ~5 units left of it. */
#define LX(x) (801.3f + (x) * 1.846f)
#define LY(y) (456.0f - (y) * 1.9f)
#define LW(w) ((w) * 1.846f)
#define LH(h) ((h) * 1.9f)

#define WORDS 0xFF645032u      /* a button's words: the layout's (100, 80, 50) */
#define WORDS_SOFT 0xFF9A8470u
#define RED 0xFFE02818u
#define BUTTON_MINT 0xFF8CFFB4u   /* Bg0n's colour */
#define BUTTON_ORANGE 0xFFFF8C19u /* the cursor's (CursorOnColor: green 128-190) */
#define ROW_TAN 0xFFE6C8AAu       /* the counts' bars (CountBgKado's colour) */

static const Rect kLeftPage = {LX(-316), LY(200), LX(-16), LY(-200)}, kRightPage = {LX(16), LY(200), LX(316), LY(-200)};

/* Beside the save file screen's buttons, one like the Pad's: Options,
 * with the icon of what opens it (the - button's key). */
#ifdef __ANDROID__
static const Rect kFileButton = {1332, 60, 1602, 153}; /* clear of the touch buttons, at the bottom right */
#else
static const Rect kFileButton = {1332, 655, 1602, 748};
#endif

/* Texture img (w x h) on the parallelogram from (ox, oy) along (ax, ay)
 * for its width and (bx, by) for its height, in picture units; us and vs
 * times across it (over 1 repeats it), clamped at its edges if clamp. A
 * texel is coloured from c0 where it's black to c1 where it's white (a
 * layout material's two colours: 0xFF000000 and 0xFFFFFFFF leave it be),
 * its alpha times c1's. */
static void draw_quad(const uint32_t* img, int w, int h, float ox, float oy, float ax, float ay, float bx, float by,
                      float us, float vs, int clamp, uint32_t c0, uint32_t c1) {
    float px[4] = {X(ox), X(ox + ax), X(ox + bx), X(ox + ax + bx)}, py[4] = {Y(oy), Y(oy + ay), Y(oy + by), Y(oy + ay + by)};
    float wax = ax * sScale, way = ay * sScale, wbx = bx * sScale, wby = by * sScale, det = wax * wby - way * wbx;
    float minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    int i, x, y, k;
    if (!img || fabsf(det) < 1e-6f) return;
    for (i = 1; i < 4; i++) {
        minx = px[i] < minx ? px[i] : minx;
        maxx = px[i] > maxx ? px[i] : maxx;
        miny = py[i] < miny ? py[i] : miny;
        maxy = py[i] > maxy ? py[i] : maxy;
    }
    for (y = miny < 0 ? 0 : (int)miny; y < (int)ceilf(maxy) && y < sH; y++) {
        for (x = minx < 0 ? 0 : (int)minx; x < (int)ceilf(maxx) && x < sW; x++) {
            float dx = x + 0.5f - px[0], dy = y + 0.5f - py[0];
            float u = (dx * wby - dy * wbx) / det, v = (wax * dy - way * dx) / det, tu, tv;
            uint32_t c, o;
            if (u < 0 || u > 1 || v < 0 || v > 1) continue;
            tu = u * us;
            tv = v * vs;
            if (clamp) {
                tu = tu > 1 ? 1 : tu;
                tv = tv > 1 ? 1 : tv;
            } else {
                tu -= floorf(tu);
                tv -= floorf(tv);
            }
            c = sample(img, w, h, tu * w, tv * h);
            if (!(c >> 24)) continue;
            o = (uint32_t)((c >> 24) * (c1 >> 24) / 255) << 24;
            for (k = 0; k < 24; k += 8) {
                int a = c0 >> k & 255, b = c1 >> k & 255;
                o |= (uint32_t)(a + (b - a) * (int)(c >> k & 255) / 255) << k;
            }
            blend(&sPx[y * sW + x], o, 1.0f);
        }
    }
}

/* A picture pane: texture name, w x h layout units, its position (x, y)
 * at its origin (0-8: left/centre/right of top/middle/bottom), rotated
 * deg anticlockwise and scaled (sx, sy, negative flips it) about it. */
static void draw_pane(const char* name, float x, float y, float w, float h, int origin, float deg, float sx, float sy,
                      float us, float vs, uint32_t c0, uint32_t c1) {
    int tw, th;
    const uint32_t* img = hle_pause_image(name, NULL, &tw, &th);
    float l = -(origin % 3) * w / 2, t = (origin / 3) * h / 2, r = (float)(deg * 3.14159265 / 180), cs = cosf(r), sn = sinf(r);
    /* corners in layout units: top-left, and the top and left edges */
    float ox = l * sx, oy = t * sy, axl = w * sx, byl = -h * sy;
    float Ox = x + ox * cs - oy * sn, Oy = y + ox * sn + oy * cs;
    float Ax = axl * cs, Ay = axl * sn, Bx = -byl * sn, By = byl * cs;
    if (!img) return;
    draw_quad(img, tw, th, LX(Ox), LY(Oy), LW(Ax), -LH(Ay), LW(Bx), -LH(By), us, vs, 0, c0, c1);
}

#define PLAIN 0xFF000000u, 0xFFFFFFFFu

/* A window pane of four frames (nw4r::lyt::Window): from (x0, y0) to
 * (x1, y1), y up, mirrored left to right if mirror. The top-left frame's
 * texture runs along the top, the top-right's down the right, the
 * bottom-left's up the left and the bottom-right's along the bottom, each
 * from its rect's top-left, clamped past its size; flip, the layout's: 0
 * none, 1 across, 2 up and down, 4 both. The content fills between. */
static void draw_window(float x0, float y0, float x1, float y1, int mirror, const char* content, const char* const frame[4],
                        const int flip[4]) {
    int i, fw[4], fh[4], cw, ch;
    const uint32_t* img[4];
    const uint32_t* fill = hle_pause_image(content, NULL, &cw, &ch);
    for (i = 0; i < 4; i++) {
        img[i] = hle_pause_image(frame[i], NULL, &fw[i], &fh[i]);
        if (!img[i]) return;
    }
    if (fill) {
        float l = x0 + fw[2], r = x1 - fw[1], t = y0 - fh[0], b = y1 + fh[3];
        if (mirror) {
            float m = l;
            l = x0 + x1 - r;
            r = x0 + x1 - m;
        }
        draw_quad(fill, cw, ch, LX(l), LY(t), LW(r - l), 0, 0, LH(t - b), 1, 1, 1, PLAIN);
    }
    for (i = 0; i < 4; i++) {
        /* the frame's rect, its texture's corner at (cx, cy) */
        float rx0, ry0, rx1, ry1, cx, cy, ax, ay, bx, by, w, h;
        int fx, fy;
        switch (i) {
        case 0: rx0 = x0; rx1 = x1 - fw[1]; ry0 = y0; ry1 = y0 - fh[0]; break;           /* top */
        case 1: rx0 = x1 - fw[1]; rx1 = x1; ry0 = y0; ry1 = y1 + fh[3]; break;           /* right */
        case 2: rx0 = x0; rx1 = x0 + fw[2]; ry0 = y0 - fh[0]; ry1 = y1; break;           /* left */
        default: rx0 = x0 + fw[2]; rx1 = x1; ry0 = y1 + fh[3]; ry1 = y1; break;          /* bottom */
        }
        /* the texture from the rect's top-left, from its right if flipped
         * across, from its bottom if flipped up and down */
        fx = flip[i] == 1 || flip[i] == 4;
        fy = flip[i] == 2 || flip[i] == 4;
        if (mirror) {
            float m = rx0;
            rx0 = x0 + x1 - rx1;
            rx1 = x0 + x1 - m;
            fx ^= 1;
        }
        w = rx1 - rx0;
        h = ry0 - ry1;
        cx = fx ? rx1 : rx0;
        cy = fy ? ry1 : ry0;
        ax = fx ? -w : w;
        ay = 0;
        bx = 0;
        by = fy ? h : -h;
        draw_quad(img[i], fw[i], fh[i], LX(cx), LY(cy), LW(ax), -LH(ay), LW(bx), -LH(by), w / fw[i], h / fh[i], 1,
                  PLAIN);
    }
}

/* The Pad's caption: its dark green line with the leaves about it, and
 * name on it, the line's left end at (x, y), k its size. */
static void draw_caption(const char* name, float x, float y, float k, int leaves) {
    if (leaves) {
        draw_pane("PauseCaptionLeaf", x + 5 * k, y - 17 * k, 78 * k, 128 * k, 7, 30, 0.6f, 0.6f, 1, 1, 0xFF96460Au, 0xFFF0E60Au);
        draw_pane("PauseCaptionLeaf", x + 1 * k, y - 14 * k, 78 * k, 128 * k, 7, 60, 0.8f, 0.8f, 1, 1, 0xFF0A6414u, 0xFF96E628u);
    }
    draw_pane("PauseCaptionLine", x, y, 210 * k, 26 * k, 3, 0, 1, 1, 1, 1, PLAIN);
    if (leaves)
        draw_pane("PauseCaptionLeaf", x + 198 * k, y - 16 * k, 78 * k, 128 * k, 7, -40, 0.5f, 0.5f, 1, 1, 0xFF0A823Cu,
                  0xFFD2DC14u);
    draw_text(name, LX(x + 105 * k), LY(y) + 1, LH(21 * k), ALIGN_CENTRE, 0xFFF4F4E8u, 0, 0, 1, LW(180 * k));
}

/* A button as the Pad's: a rounded bar of colour c, shaded to yellow
 * across (Grad: (255, 255, 85), 0 to 140 alpha), its icon and words; from
 * layout x at y, 220 x 40. */
static void draw_button(float x, float y, uint32_t c, const char* icon, const char* words, int lit) {
    Rect bar = {LX(x), LY(y + 19), LX(x + 220), LY(y - 19)};
    Rect grad = {LX(x + 7), LY(y + 19), LX(x + 217), LY(y - 19)};
    draw_box(bar, 12, c);
    draw_box_shaded(grad, 12, 0x00FFFF55u, 0x8CFFFF55u, 1);
    if (icon) draw_pane(icon, x + 23, y, 32, 32, 4, 0, 1, 1, 1, 1, PLAIN);
    if (lit) draw_text(words, LX(x + 42), LY(y) + 2, LH(24), ALIGN_LEFT, 0xFFFFFFFFu, 0xFFB04A10u, 3.0f, 1, LW(165));
    else draw_text(words, LX(x + 42), LY(y) + 2, LH(24), ALIGN_LEFT, WORDS, 0xFFF4FFF0u, 2.0f, 1, LW(165));
}

/* The stitched frame over a button (Frame0n), open on its left, from
 * layout x at y, 226 x 44: the band's open end (PauseTopListBg01), the
 * band (Bg02, seamless) and its corner (Bg00), each in the top half of
 * its tile, drawn F units square, as thick as the Pad shows them. */
static void draw_button_frame(float x, float y) {
    const float F = 11;
    float t = y + 22, b = y - 22, r = x + 226, run = r - x - 2 * F, side = t - b - 2 * F;
    int k;
    for (k = 0; k < 2; k++) { /* along the top, and flipped along the bottom */
        float at = k ? b : t, sy = k ? -1.0f : 1.0f;
        draw_pane("PauseTopListBg01", x, at, F, F, 0, 0, 1, sy, 1, 1, PLAIN);
        draw_pane("PauseTopListBg02", x + F, at, run, F, 0, 0, 1, sy, run / F, 1, PLAIN);
        draw_pane("PauseTopListBg00", r - F, at, F, F, 0, 0, 1, sy, 1, 1, PLAIN);
    }
    draw_pane("PauseTopListBg02", r, t - F, side, F, 0, -90, 1, 1, side / F, 1, PLAIN); /* down the right */
}

static void draw_file_button(void) {
    float x = (kFileButton.x0 - 801.3f) / 1.846f + 6, y = (456.0f - (kFileButton.y0 + kFileButton.y1) / 2) / 1.9f;
    Rect icon = {kFileButton.x0 + 22, kFileButton.y0 + 21, kFileButton.x0 + 74, kFileButton.y0 + 73};
    int hasIcon;
    draw_box(kFileButton, 18, 0xFFFFFFFFu);
    {
        Rect in = {kFileButton.x0 + 5, kFileButton.y0 + 5, kFileButton.x1 - 5, kFileButton.y1 - 5};
        draw_box(in, 14, BUTTON_MINT);
        draw_box_shaded(in, 14, 0x00FFFF55u, 0x8CFFFF55u, 1);
    }
    (void)x;
    (void)y;
    hasIcon = draw_button_icon(HOST_PAD_MINUS, icon, 0xFF645032u);
    draw_text("Options", hasIcon ? (icon.x1 + kFileButton.x1) / 2 : (kFileButton.x0 + kFileButton.x1) / 2,
              kFileButton.y0 + 48, 52, ALIGN_CENTRE, WORDS, 0xFFF4FFF0u, 3.0f, 1, hasIcon ? 170 : 220);
    /* Over it, a newer release's news (Options' Updates row opens its page). */
    if (update_state() == UPDATE_NEWER) {
        char news[64];
        snprintf(news, sizeof(news), "Update: %s", update_tag());
        draw_text(news, (kFileButton.x0 + kFileButton.x1) / 2, kFileButton.y0 - 23, 40, ALIGN_CENTRE, 0xFFFFFFFFu,
                  0xFF2E6A1Eu, 3.0f, 1, kFileButton.x1 - kFileButton.x0);
    }
}

/* The other tab: a caption line, with an arrowhead pointing the way. */
static void draw_tab(const char* name, int right) {
    float x = (kTab.x0 - 801.3f) / 1.846f, y = (456.0f - (kTab.y0 + kTab.y1) / 2) / 1.9f;
    float k = (kTab.x1 - kTab.x0) / 1.846f / 210;
    draw_pane("PauseCaptionLine", x, y, 210 * k, 26 * k * 1.25f, 3, 0, 1, 1, 1, 1, PLAIN);
    draw_text(name, (kTab.x0 + kTab.x1) / 2 + (right ? -12 : 12), (kTab.y0 + kTab.y1) / 2 + 2, 36, ALIGN_CENTRE,
              0xFFF4F4E8u, 0, 0, 1, kTab.x1 - kTab.x0 - 80);
    draw_triangle(right ? kTab.x1 - 26 : kTab.x0 + 26, (kTab.y0 + kTab.y1) / 2, 11, right ? 2 : -2, 0xFFF4F4E8u);
}

/* The book (MnPsBook, its SRT at (16, 0)) and the right page's paper
 * (MnPsTop's PageBgR). */
static void draw_book_pages(void) {
    static const char* const kPage4[4] = {"PauseTopBookBg00", "PauseTopBookBg01", "PauseTopBookBg00", "PauseTopBookBg01"};
    static const int kPageFlip[4] = {0, 0, 2, 2};
    static const char* const kNaka4[4] = {"PauseTopBookBg04", "PauseTopBookBg04", "PauseTopBookBg03", "PauseTopBookBg04"};
    static const int kNakaFlip[4] = {0, 0, 2, 2};
    static const char* const kSpine4[4] = {"PauseTopBookBg06", "PauseTopBookBg06", "PauseTopBookBg06", "PauseTopBookBg06"};
    static const char* const kSpineNaka4[4] = {"PauseTopBookBg09", "PauseTopBookBg09", "PauseTopBookBg09", "PauseTopBookBg09"};
    static const int kSpineFlip[4] = {0, 1, 2, 4};
    int side;
    /* The right page (mirrored), with its seams and corners and leaves. */
    draw_window(16, 200, 316, -200, 1, "PauseTopBookBg02", kPage4, kPageFlip);
    draw_window(16, 193, 310, -193, 1, "PauseTopBookBg05", kNaka4, kNakaFlip);
    draw_pane("PauseTopBg", 0, 0, 294, 361, 3, 0, 1, 1, 1, 1, PLAIN); /* the paper, as MnPsTop has it */
    draw_pane("PauseTopBookKanagu", 290, 174, 60, 60, 4, 0, -1, 1, 1, 1, PLAIN);
    draw_pane("PauseTopBookKanagu", 290, -173, 60, 60, 4, 0, -1, -1, 1, 1, PLAIN);
    draw_pane("PauseTopObj00", 335, 80, 90, 222, 4, 0, 1, 1, 1, 1, PLAIN);
    /* The spine, and the left page. */
    draw_window(-16, 200, 16, -200, 0, "PauseTopBookBg08", kSpine4, kSpineFlip);
    draw_window(-16, 193, 16, -193, 0, "PauseTopBookBg11", kSpineNaka4, kSpineFlip);
    draw_window(-316, 200, -16, -200, 0, "PauseTopBookBg02", kPage4, kPageFlip);
    draw_window(-310, 193, -16, -193, 0, "PauseTopBookBg05", kNaka4, kNakaFlip);
    for (side = 0; side < 2; side++) { /* the seams: top, outer edge, bottom */
        float s = side ? 1.0f : -1.0f;
        draw_pane("PauseTopBookNuime", s * 161, 182, 16, 290, 4, 90, 1, 1, 1, 290 / 16.0f, PLAIN);
        draw_pane("PauseTopBookNuime", s * (side ? 297 : 298), -5, 16, 380, 4, 0, 1, 1, 1, 380 / 16.0f, PLAIN);
        draw_pane("PauseTopBookNuime", s * 161, -183, 16, 290, 4, 90, 1, 1, 1, 290 / 16.0f, PLAIN);
    }
    draw_pane("PauseTopBookBg12", -272, -2, 32, 340, 4, 0, 1, 1, 1, 5, PLAIN); /* footprints */
    draw_pane("PauseTopBookKanagu", -290, 174, 60, 60, 4, 0, 1, 1, 1, 1, PLAIN);
    draw_pane("PauseTopBookKanagu", -290, -173, 60, 60, 4, 0, 1, -1, 1, 1, PLAIN);
}

/* What doesn't change: the book, the caption and the other tab. */
static void draw_pages(void) {
    draw_book_pages();
    draw_caption("Options", -276, 186, 0.9f, 1);
    if (sFrom == FROM_PAD) draw_tab("Pok\xE9Park Pad", 0); /* the font's Latin-1 */
}

static void draw_book(void) {
    /* The sections' icons: the Pad's. */
#ifdef __ANDROID__
    static const char* const kIcon[SECTIONS] = {"PauseTopListIcon04", "PauseTopListIcon01", "PauseTopListIcon00"};
#else
    static const char* const kIcon[SECTIONS] = {"PauseTopListIcon04", "PauseTopListIcon06", "PauseTopListIcon01",
                                                "PauseTopListIcon00"};
#endif
    static const float kListX[2] = {-238, -228}, kListY[4] = {130, 76, 21, -33};
    int i, n = row_count(sSection);
    char buf[64];

    /* The sections, as the Pad's buttons (List0n, Frame0n): the chosen one
     * orange and slid left out of its frame, paler while its settings are
     * being chosen. */
    for (i = 0; i < sections(); i++) {
        int lit = i == sSection;
        float x = kListX[i & 1] + (lit ? -40 : -14), y = kListY[i];
        draw_button(x, y, lit ? (sOnRows ? 0xFFFFB45Au : BUTTON_ORANGE) : BUTTON_MINT, kIcon[i], kSection[i], lit);
        draw_button_frame(kListX[i & 1] - 17, y);
    }

    /* The section's settings, as the Pad's counts are (CountBg0n). */
    draw_caption(kSection[sSection], 37, 166, 0.95f, 0);
    if (sSection != SEC_DISPLAY && sSection != SEC_GAMEPLAY) {
        draw_text("Slot 1", slot_x(0), 214, 24, ALIGN_CENTRE, WORDS_SOFT, 0, 0, 0, 0);
        draw_text("Slot 2", slot_x(1), 214, 24, ALIGN_CENTRE, WORDS_SOFT, 0, 0, 0, 0);
    }
    for (i = sTop; i < n && i < sTop + VISIBLE_ROWS; i++) {
        Row r = row_at(sSection, i);
        Rect bar = row_rect(i - sTop);
        float my = (bar.y0 + bar.y1) / 2;
        int chosen = sOnRows && i == sRow;
        uint32_t fill = chosen ? 0xFFFFFFFFu : WORDS, edge = chosen ? 0xFFB04A10u : 0;
        float outline = chosen ? 3.0f : 0;
        if (chosen) {
            draw_box(bar, 14, BUTTON_ORANGE);
            draw_box_shaded(bar, 14, 0x00FFFF55u, 0x8CFFFF55u, 1);
        } else {
            draw_box(bar, 14, ROW_TAN);
        }
        draw_text(r.label, bar.x0 + 18, my + 2, r.kind == ROW_BINDING ? 36 : 38, ALIGN_LEFT, fill, edge, outline, 1,
                  r.kind == ROW_SETTING ? 196 : 190);
        if (r.kind == ROW_SETTING) {
            float cx = 1180;
            setting_text(r.which, buf, sizeof(buf));
            draw_text(buf, cx, my + 2, 36, ALIGN_CENTRE, fill, edge, outline, 1, 180);
            if (chosen) {
                draw_triangle(cx - 104, my, 11, -2, RED);
                draw_triangle(cx + 104, my, 11, 2, RED);
            }
        } else if (r.kind == ROW_CLOSE) {
            if (close_armed()) draw_text("Again to close", 1180, my + 2, 34, ALIGN_CENTRE, RED, 0xFFFFFFFFu, 2.0f, 1, 220);
        } else if (r.kind == ROW_BINDING) {
            int k;
            for (k = 0; k < BIND_SLOTS; k++) {
                float cx = slot_x(k);
                int none, on = chosen && k == sSlot;
                if (on) {
                    Rect b = {cx - 54, bar.y0 + 7, cx + 54, bar.y1 - 7};
                    draw_box(b, 8, 0xF0FFFFFFu);
                }
                if (on && sCapture != CAPTURE_NONE) {
                    draw_text(sCapture == CAPTURE_KEY ? "Press a key" : "Press a button", cx, my + 2, 28, ALIGN_CENTRE,
                              RED, 0, 0, 1, 104);
                    continue;
                }
                if (sSection == SEC_CONTROLLER) {
                    snprintf(buf, sizeof(buf), "%s", settings_pad_name(g_settings.pad[r.which][k]));
                } else {
                    settings_key_name(g_settings.key[r.which][k], buf, sizeof(buf));
                }
                none = !strcmp(buf, "-");
                if (none) draw_text(buf, cx, my + 2, 32, ALIGN_CENTRE, WORDS_SOFT, 0, 0, 0, 104);
                else if (chosen && !on) draw_text(buf, cx, my + 2, 32, ALIGN_CENTRE, fill, edge, outline, 1, 104);
                else draw_text(buf, cx, my + 2, 32, ALIGN_CENTRE, WORDS, 0, 0, 1, 104);
            }
        }
    }
    /* More above or below. */
    if (sTop > 0) draw_triangle((kPage.x0 + kPage.x1) / 2, ROWS_TOP - 10, 9, 1, 0xFF8A6040u);
    if (sTop + VISIBLE_ROWS < n)
        draw_triangle((kPage.x0 + kPage.x1) / 2, ROWS_TOP + VISIBLE_ROWS * ROW_PITCH - 2, 9, -1, 0xFF8A6040u);
    /* What the keys do here, when it's not plain. */
    if (sOnRows && row_at(sSection, sRow).kind == ROW_SETTING && row_at(sSection, sRow).which == SET_UPDATES &&
        update_state() == UPDATE_NEWER) {
        draw_text("Select: open its download page", (kPage.x0 + kPage.x1) / 2, 768, 26, ALIGN_CENTRE, WORDS_SOFT, 0, 0,
                  0, 0);
    }
    if (sOnRows && row_at(sSection, sRow).kind == ROW_BINDING) {
        draw_text(sCapture != CAPTURE_NONE ? "Esc: cancel" : input_controller_last() ? "Select: rebind    Y: clear"
                                                                                   : "Select: rebind    Delete: clear",
                  (kPage.x0 + kPage.x1) / 2, 768, 26, ALIGN_CENTRE, WORDS_SOFT, 0, 0, 0, 0);
    }
}

static unsigned sDraws; /* times the image was drawn again */

unsigned options_draws(void) { return sDraws; }

const uint32_t* options_draw(int w, int h) {
    static unsigned drawn = ~0u;
    static int drawnW, drawnH, drawnWhat, drawnUpdate, cw;
    int what = sOpen ? 2 : hle_pause_main_page() ? 1 : hle_file_select_shown() ? 3 : 0;
    if (sOpen && !shown(sFrom)) { /* what it was opened from went (the scene changed) */
        set_open(0);
        what = 0;
    }
    if (!what || w <= 0 || h <= 0 || !hle_font_metrics(&sCellW, &sCellH, &sBaseline))
        return NULL;
    if (!hle_pause_image("PauseTopBg", NULL, &cw, &cw)) return NULL; /* the Pad's pictures aren't read yet */
    /* (The update check's answer comes in by itself.) */
    if (what == drawnWhat && drawn == sChanges && w == drawnW && h == drawnH && drawnUpdate == update_state() && sPx)
        return sPx;
    if (w != sW || h != sH || !sPx) {
        free(sPx);
        sPx = (uint32_t*)malloc((size_t)w * h * 4);
        if (!sPx) return NULL;
        sW = w;
        sH = h;
    }
    sDraws++;
    drawn = sChanges;
    drawnW = w;
    drawnH = h;
    drawnWhat = what;
    drawnUpdate = update_state();
    sScale = (float)w / REF_W < (float)h / REF_H ? (float)w / REF_W : (float)h / REF_H;
    sOffX = (w - REF_W * sScale) / 2;
    sOffY = (h - REF_H * sScale) / 2;
    memset(sPx, 0, (size_t)w * h * 4);
    if (what == 1) {
        draw_tab("Options", 1);
        return sPx;
    }
    if (what == 3) {
        draw_file_button();
        return sPx;
    }
    {
        /* The pages, drawn once for this size. */
        static uint32_t* pages;
        static int pagesW, pagesH, pagesFrom;
        if (!pages || pagesW != w || pagesH != h || pagesFrom != sFrom) {
            pagesFrom = sFrom;
            free(pages);
            draw_pages();
            pages = (uint32_t*)malloc((size_t)w * h * 4);
            if (pages) memcpy(pages, sPx, (size_t)w * h * 4);
            pagesW = w;
            pagesH = h;
        } else {
            memcpy(sPx, pages, (size_t)w * h * 4);
        }
    }
    draw_book();
    return sPx;
}

/* ------------------------------------------------------------------------ */
/* A second screen's picture (vk_backend.c): the Pad open, its title on the
 * left page and the minimap on the right one's paper, on a photo; over
 * the dark the pause menu lays over the game. Drawn into an image of its
 * own, with the Options' drawing (sPx and its place) put back after. */

#define PAGES_BEHIND 0xFF10141Au
#define PHOTO 0xFFFBF7EEu
#define PHOTO_EDGE 14.0f

/* The friends list (hle_friends.c), on the left page: a row each, its
 * icon and name, dark and unnamed until befriended; scrolled by touch. */
int hle_friends_rows(void);
const char* hle_friends_name(int i);
int hle_friends_is_friend(int i);
const uint32_t* hle_friends_icon(int i, int* w, int* h);
unsigned hle_friends_changes(void);

static const Rect kList = {LX(-264), LY(144), LX(-28), LY(-176)}; /* the rows and their bar */
#define LIST_BAR 16.0f
#define LIST_PITCH 62.0f
#define LIST_ROW 54.0f
#define STRIPS 40         /* rows kept drawn */
#define NEW_STRIPS 3      /* rows drawn afresh a frame, at most */
#define UNKNOWN 0xFF6E5A46u /* a friend not made yet: its icon's place */

static struct {
    uint32_t* px;
    int row;
    unsigned used;
} sStrips[STRIPS];
static int sStripW, sStripH;
static unsigned sStripClock;
static float sListScale = 1, sListOffX, sListOffY; /* the pages' picture's place, for touches */
static float sScroll, sVelocity, sDragY, sLastY, sDragScroll;
static int sDragging;
static unsigned sMoves;

static float list_limit(int rows) {
    float limit = rows * LIST_PITCH - (LIST_PITCH - LIST_ROW) - (kList.y1 - kList.y0);
    return limit > 0 ? limit : 0;
}

static void forget_strips(void) {
    int i;
    for (i = 0; i < STRIPS; i++) sStrips[i].row = -1;
}

/* Row i drawn alone into a strip as wide as the rows, clear about it. */
static void draw_strip(uint32_t* px, int i) {
    uint32_t* keepPx = sPx;
    int keepW = sW, keepH = sH, w, h, friend_ = hle_friends_is_friend(i);
    float keepX = sOffX, keepY = sOffY;
    const uint32_t* icon = hle_friends_icon(i, &w, &h);
    Rect bar = {kList.x0, 0, kList.x1 - LIST_BAR - 8, LIST_ROW};
    memset(px, 0, (size_t)sStripW * sStripH * 4);
    sPx = px;
    sW = sStripW;
    sH = sStripH;
    sOffX -= floorf(X(kList.x0));
    sOffY = 0;
    Rect frame = {bar.x0 + 6, 3, bar.x0 + 6 + LIST_ROW - 6, LIST_ROW - 3};
    Rect in = {frame.x0 + 4, frame.y0 + 4, frame.x1 - 4, frame.y1 - 4};
    draw_box(bar, 14, friend_ ? ROW_TAN : 0xFFDCCAB4u);
    /* The icon in its frame, as the Pad's list has it (CharaIconFrame);
     * a question mark there until it's a friend. */
    draw_box(frame, 8, friend_ ? 0xFFFFFFFFu : 0xFFF4ECE0u);
    if (friend_ && icon) {
        Rect src = {0, 0, (float)w, (float)h};
        draw_image(icon, w, h, src, in, 0xFFFFFF, 0);
    } else {
        draw_box(in, 5, UNKNOWN);
        draw_text("?", (in.x0 + in.x1) / 2, (in.y0 + in.y1) / 2 + 2, 34, ALIGN_CENTRE, 0xFFF4ECE0u, 0, 0, 1, 0);
    }
    draw_text(friend_ ? hle_friends_name(i) : "?????", frame.x1 + 16, LIST_ROW / 2 + 2, 34, ALIGN_LEFT,
              friend_ ? WORDS : WORDS_SOFT, 0, 0, friend_, bar.x1 - frame.x1 - 28);
    sPx = keepPx;
    sW = keepW;
    sH = keepH;
    sOffX = keepX;
    sOffY = keepY;
}

/* Row i's strip, drawn if it isn't kept (and fresh allows: else NULL). */
static const uint32_t* strip(int i, int* fresh) {
    int k, oldest = 0;
    for (k = 0; k < STRIPS; k++) {
        if (sStrips[k].row == i && sStrips[k].px) {
            sStrips[k].used = ++sStripClock;
            return sStrips[k].px;
        }
        if (sStrips[k].used < sStrips[oldest].used) oldest = k;
    }
    if (*fresh <= 0) return NULL;
    if (!sStrips[oldest].px && !(sStrips[oldest].px = (uint32_t*)malloc((size_t)sStripW * sStripH * 4))) return NULL;
    --*fresh;
    draw_strip(sStrips[oldest].px, i);
    sStrips[oldest].row = i;
    sStrips[oldest].used = ++sStripClock;
    return sStrips[oldest].px;
}

/* A strip laid on the picture with its top at y, inside y0 to y1. */
static void lay(const uint32_t* s, int x, int y, int y0, int y1) {
    int j, i;
    for (j = 0; j < sStripH; j++) {
        uint32_t* d;
        const uint32_t* p = s + (size_t)j * sStripW;
        if (y + j < y0 || y + j >= y1 || y + j >= sH) continue;
        d = sPx + (size_t)(y + j) * sW + x;
        for (i = 0; i < sStripW && x + i < sW; i++) {
            uint32_t c = p[i];
            if (c >> 24 == 255) d[i] = c;
            else if (c >> 24) blend(&d[i], c, 1.0f);
        }
    }
}

/* A touch on the second screen (0 down, 1 up, 2 moved), at its pixels. */
void pad_pages_touch(int action, float x, float y) {
    x = (x - sListOffX) / sListScale;
    y = (y - sListOffY) / sListScale;
    if (action == 0) {
        if (x < kList.x0 - 40 || x >= kList.x1 + 40 || y < kList.y0 - 40 || y >= kList.y1 + 40) return;
        sDragging = 1;
        sDragY = sLastY = y;
        sDragScroll = sScroll;
        sVelocity = 0;
    } else if (sDragging) {
        float scroll = sDragScroll - (y - sDragY);
        sVelocity = sVelocity * 0.5f + (sLastY - y) * 0.5f;
        sLastY = y;
        if (scroll != sScroll) {
            sScroll = scroll;
            sMoves++;
        }
        if (action == 1) sDragging = 0;
    }
}

/* changed: 1 if all of it changed, 2 if only the part changed[1..4]
 * (x, y, w, h) did, else 0. */
const uint32_t* pad_pages_draw(int w, int h, double map_aspect, int* changed, int* map) {
    static uint32_t *px, *pages;
    static int drawnW, drawnH, drawnRows = -1, drawnFriends = -1;
    static unsigned drawnChanges = ~0u, drawnMoves = ~0u;
    static int at[4];
    Rect m;
    uint32_t* keepPx = sPx;
    int keepW = sW, keepH = sH, i, cw, rows, all = 0, fresh = NEW_STRIPS, friends = 0, lx, ly0, ly1;
    float keepScale = sScale, keepX = sOffX, keepY = sOffY, limit, top;
    *changed = 0;
    if (w <= 0 || h <= 0 || !hle_font_metrics(&sCellW, &sCellH, &sBaseline) || !hle_pause_image("PauseTopBg", NULL, &cw, &cw))
        return NULL;
    rows = hle_friends_rows();
    /* Thrown, it slows. */
    if (!sDragging && (sVelocity > 0.5f || sVelocity < -0.5f)) {
        sScroll += sVelocity;
        sVelocity *= 0.92f;
        sMoves++;
    }
    limit = list_limit(rows);
    if (sScroll < 0 || sScroll > limit) {
        sScroll = sScroll < 0 ? 0 : limit;
        sVelocity = 0;
    }
    if (px && w == drawnW && h == drawnH && rows == drawnRows && drawnChanges == hle_friends_changes() &&
        drawnMoves == sMoves)
        goto done;
    if (!px || w != drawnW || h != drawnH) {
        free(px);
        free(pages);
        px = (uint32_t*)malloc((size_t)w * h * 4);
        pages = (uint32_t*)malloc((size_t)w * h * 4);
        if (!px || !pages) {
            free(px);
            free(pages);
            px = pages = NULL;
            return NULL;
        }
        for (i = 0; i < STRIPS; i++) {
            free(sStrips[i].px);
            sStrips[i].px = NULL;
        }
        forget_strips();
        drawnW = w;
        drawnH = h;
        drawnFriends = -1;
        /* The book (layout x -335 to 335, y -212 to 212) as large as fits. */
        sScale = 0.97f * ((float)w / LW(670) < (float)h / LH(424) ? (float)w / LW(670) : (float)h / LH(424));
        sOffX = w / 2.0f - LX(0) * sScale;
        sOffY = h / 2.0f - LY(0) * sScale;
        sListScale = sScale;
        sListOffX = sOffX;
        sListOffY = sOffY;
        sStripW = (int)ceilf(X(kList.x1)) - (int)floorf(X(kList.x0)) + 1;
        sStripH = (int)ceilf(LIST_ROW * sScale) + 2;
        sPx = pages;
        sW = w;
        sH = h;
        for (i = 0; i < w * h; i++) pages[i] = PAGES_BEHIND;
        draw_book_pages();
        draw_pane("PauseTopBg", -16, 0, 294, 361, 3, 0, -1, 1, 1, 1, PLAIN); /* the left page's paper (PageBgL) */
        draw_caption("Friends", -276, 186, 0.9f, 1);
        draw_caption("Map", 37, 166, 0.95f, 0);
        {
            /* The photo, at the map's shape, in the paper below the caption. */
            Rect in = {LX(22), LY(140), LX(288), LY(-168)};
            float pw = in.x1 - in.x0 - 2 * PHOTO_EDGE, ph = in.y1 - in.y0 - 2 * PHOTO_EDGE;
            if (pw > ph * map_aspect) pw = (float)(ph * map_aspect);
            else ph = (float)(pw / map_aspect);
            m.x0 = (in.x0 + in.x1 - pw) / 2;
            m.y0 = (in.y0 + in.y1 - ph) / 2;
            m.x1 = m.x0 + pw;
            m.y1 = m.y0 + ph;
            {
                Rect shadow = {m.x0 - PHOTO_EDGE + 6, m.y0 - PHOTO_EDGE + 8, m.x1 + PHOTO_EDGE + 6, m.y1 + PHOTO_EDGE + 8};
                Rect border = {m.x0 - PHOTO_EDGE, m.y0 - PHOTO_EDGE, m.x1 + PHOTO_EDGE, m.y1 + PHOTO_EDGE};
                draw_box(shadow, 6, 0x50A48D62u);
                draw_box(border, 4, PHOTO); /* the map drawn on it as on paper (vk_backend.c's kMapColor) */
            }
        }
        at[0] = (int)X(m.x0);
        at[1] = (int)Y(m.y0);
        at[2] = (int)(X(m.x1) - X(m.x0));
        at[3] = (int)(Y(m.y1) - Y(m.y0));
        all = 1;
    } else {
        sScale = sListScale;
        sOffX = sListOffX;
        sOffY = sListOffY;
        sW = w;
        sH = h;
    }
    if (drawnChanges != hle_friends_changes() || rows != drawnRows) {
        forget_strips();
        for (i = 0; i < rows; i++) friends += hle_friends_is_friend(i);
        if (friends != drawnFriends) all = 1; /* the count, by the caption */
        drawnFriends = friends;
    }
    drawnRows = rows;
    drawnChanges = hle_friends_changes();
    drawnMoves = sMoves;
    sPx = px;
    lx = (int)floorf(X(kList.x0));
    ly0 = (int)floorf(Y(kList.y0));
    ly1 = (int)ceilf(Y(kList.y1));
    if (all) {
        memcpy(px, pages, (size_t)w * h * 4);
        *changed = 1;
    } else {
        int y, cw2 = sStripW < w - lx ? sStripW : w - lx;
        for (y = ly0 < 0 ? 0 : ly0; y < ly1 && y < h; y++)
            memcpy(px + (size_t)y * w + lx, pages + (size_t)y * w + lx, (size_t)cw2 * 4);
        *changed = 2;
        changed[1] = lx;
        changed[2] = ly0 < 0 ? 0 : ly0;
        changed[3] = cw2;
        changed[4] = (ly1 < h ? ly1 : h) - changed[2];
    }
    if (all && rows) {
        char text[16];
        snprintf(text, sizeof(text), "%d/%d", drawnFriends, rows);
        draw_text(text, kList.x1, LY(160), 28, ALIGN_RIGHT, WORDS, 0, 0, 1, 0);
    }
    /* The rows: at most a few drawn afresh a frame, the rest next frame. */
    top = kList.y0 - sScroll;
    {
        int first = (int)(sScroll / LIST_PITCH), last = (int)((sScroll + kList.y1 - kList.y0) / LIST_PITCH) + 1;
        for (i = first < 0 ? 0 : first; i <= last && i < rows; i++) {
            const uint32_t* s = strip(i, &fresh);
            if (s) lay(s, lx, (int)floorf(Y(top + i * LIST_PITCH)), ly0, ly1);
            else sMoves++; /* (drawn next frame) */
        }
    }
    /* The scroll bar. */
    if (limit > 0) {
        float x1 = kList.x1, x0 = x1 - LIST_BAR, y0 = kList.y0, y1 = kList.y1;
        float size = (y1 - y0) * (y1 - y0) / (rows * LIST_PITCH), thumbAt;
        Rect track = {x0 + 5, y0, x1 - 5, y1};
        if (size < 60) size = 60;
        thumbAt = y0 + (y1 - y0 - size) * sScroll / limit;
        {
            Rect thumb = {x0, thumbAt, x1, thumbAt + size};
            draw_box(track, 3, 0x40645032u);
            draw_box(thumb, LIST_BAR / 2, BUTTON_ORANGE);
        }
    }
    sPx = keepPx;
    sW = keepW;
    sH = keepH;
    sScale = keepScale;
    sOffX = keepX;
    sOffY = keepY;
done:
    memcpy(map, at, sizeof(at));
    return px;
}

/* ------------------------------------------------------------------------ */
/* Input */

/* Whether keyboard key vk is bound to act. */
static int key_is(int vk, int act) {
    int k;
    for (k = 0; k < BIND_SLOTS; k++) {
        if (g_settings.key[act][k] == vk) return 1;
    }
    return 0;
}

static int pad_is(uint32_t pressed, int act) {
    int k;
    for (k = 0; k < BIND_SLOTS; k++) {
        if (g_settings.pad[act][k] && (pressed & g_settings.pad[act][k])) return 1;
    }
    return 0;
}

static int in(Rect r, float x, float y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

/* A click at window pixel (mx, my); returns 1 if it was on something. */
static int click(int mx, int my) {
    float x, y;
    int i;
    if (sScale <= 0) return 0;
    x = (mx - sOffX) / sScale;
    y = (my - sOffY) / sScale;
    if (sFrom == FROM_PAD && in(kTab, x, y)) {
        set_open(0);
        return 1;
    }
    for (i = 0; i < sections(); i++) {
        if (in(kButton[i], x, y)) {
            sSection = i;
            sOnRows = 0;
            sRow = sTop = sSlot = 0;
            changed();
            return 1;
        }
    }
    for (i = 0; i < VISIBLE_ROWS && sTop + i < row_count(sSection); i++) {
        Rect r = row_rect(i);
        Row row;
        if (!in(r, x, y)) continue;
        row = row_at(sSection, sTop + i);
        sOnRows = 1;
        if (sRow != sTop + i || row.kind == ROW_BINDING) {
            sRow = sTop + i;
            if (row.kind == ROW_BINDING) {
                sSlot = x >= (slot_x(0) + slot_x(1)) / 2;
                sCapture = sSection == SEC_CONTROLLER ? CAPTURE_PAD : CAPTURE_KEY;
            }
            changed();
        } else {
            select_();
        }
        return 1;
    }
    return 0;
}

#ifdef _WIN32
/* The key a WM_KEYDOWN means, with left and right modifiers told apart. */
static int key_of(WPARAM wp, LPARAM lp) {
    int extended = (int)((lp >> 24) & 1);
    if (wp == VK_SHIFT) return (int)MapVirtualKeyA((UINT)((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX);
    if (wp == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (wp == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    return (int)wp;
}

int options_window_message(unsigned msg, uintptr_t wp, intptr_t lp) {
    int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, repeat = down && (lp & (1 << 30)), vk = 0;
    if (down) vk = key_of(wp, lp);
    if (msg == WM_LBUTTONDOWN) vk = VK_LBUTTON;
    if (msg == WM_RBUTTONDOWN) vk = VK_RBUTTON;
    if (msg == WM_MBUTTONDOWN) vk = VK_MBUTTON;
    if (msg == WM_XBUTTONDOWN) vk = HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    if (!sOpen) {
        /* Right, on the Pad's main page, opens the tab; on the save file
         * screen, its button (-) or a click on it. */
        if (down && !repeat && (vk == VK_RIGHT || key_is(vk, ACT_RIGHT)) && hle_pause_main_page()) {
            open_from(FROM_PAD);
            return 1;
        }
        if (!hle_file_select_shown() || repeat) return 0;
        if ((down && key_is(vk, ACT_MINUS)) ||
            (msg == WM_LBUTTONDOWN && sScale > 0 &&
             in(kFileButton, ((short)LOWORD(lp) - sOffX) / sScale, ((short)HIWORD(lp) - sOffY) / sScale))) {
            open_from(FROM_FILES);
            return 1;
        }
        return 0;
    }
    if (sCapture != CAPTURE_NONE) {
        if (repeat) return 1;
        if (vk == VK_ESCAPE || vk == VK_BACK) {
            sCapture = CAPTURE_NONE;
            changed();
        } else if (vk && sCapture == CAPTURE_KEY) {
            bind(vk, 0);
        }
        return down || vk;
    }
    if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2 && vk != VK_CANCEL && click((short)LOWORD(lp), (short)HIWORD(lp)))
        return 1;
    if (!vk) return 0;
    if (vk == VK_UP || key_is(vk, ACT_UP)) up_down(-1);
    else if (vk == VK_DOWN || key_is(vk, ACT_DOWN)) up_down(1);
    else if (vk == VK_LEFT || key_is(vk, ACT_LEFT)) left_right(-1);
    else if (vk == VK_RIGHT || key_is(vk, ACT_RIGHT)) left_right(1);
    else if (repeat) return 1;
    else if (vk == VK_RETURN || key_is(vk, ACT_2)) select_();
    else if (vk == VK_ESCAPE || vk == VK_BACK || key_is(vk, ACT_1)) back();
    else if (vk == VK_DELETE) clear();
    return 1;
}
#else
/* A tap at window pixel (x, y): on the tab or the save file screen's
 * button, it opens; open, it works as a click. Returns 1 if it was on
 * something. */
int options_touch(int x, int y) {
    float rx, ry;
    if (sScale <= 0) return sOpen;
    rx = (x - sOffX) / sScale;
    ry = (y - sOffY) / sScale;
    if (sOpen) {
        /* A touch off the book (the footer's Back among them) closes it:
         * a phone may have no button to. */
        if (!click(x, y) && !in(kLeftPage, rx, ry) && !in(kRightPage, rx, ry)) set_open(0);
        return 1;
    }
    if (hle_pause_main_page() && in(kTab, rx, ry)) {
        open_from(FROM_PAD);
        return 1;
    }
    if (hle_file_select_shown() && in(kFileButton, rx, ry)) {
        open_from(FROM_FILES);
        return 1;
    }
    return 0;
}
#endif

void options_controller(uint32_t raw, uint32_t nav) {
    static uint32_t lastRaw, lastNav;
    uint32_t pressed = raw & ~lastRaw, navPressed = nav & ~lastNav;
    lastRaw = raw;
    lastNav = nav;
    if (!sOpen) {
        if ((navPressed & XPAD_RIGHT) && hle_pause_main_page()) open_from(FROM_PAD);
        else if (pad_is(pressed, ACT_MINUS) && hle_file_select_shown()) open_from(FROM_FILES);
        return;
    }
    if (sCapture == CAPTURE_PAD) {
        if (pressed) bind(0, pressed & (0u - pressed)); /* the lowest one */
        return;
    }
    if (sCapture != CAPTURE_NONE) return;
    if (navPressed & XPAD_UP) up_down(-1);
    if (navPressed & XPAD_DOWN) up_down(1);
    if (navPressed & XPAD_LEFT) left_right(-1);
    if (navPressed & XPAD_RIGHT) left_right(1);
    if ((pressed & XPAD_A) || pad_is(pressed, ACT_2)) select_();
    else if ((pressed & XPAD_B) || pad_is(pressed, ACT_1)) back();
    else if (pressed & XPAD_Y) clear();
}

/* Wii Remote buttons held (HOST_PAD_* bits), from a script or the touch
 * controls, each with its own last and swallow. */
static int remote_input(uint32_t held, uint32_t* last, int* swallow) {
    uint32_t p = held & ~*last;
    *last = held;
    if (!held) *swallow = 0;
    if (!sOpen) {
        if ((p & HOST_PAD_SCREEN_RIGHT) && hle_pause_main_page()) open_from(FROM_PAD);
        else if ((p & HOST_PAD_MINUS) && hle_file_select_shown()) open_from(FROM_FILES);
        if (sOpen) *swallow = 1;
        return *swallow; /* what the tab took stays its until let go */
    }
    *swallow = 1;
    if (p & HOST_PAD_SCREEN_UP) up_down(-1);
    if (p & HOST_PAD_SCREEN_DOWN) up_down(1);
    if (p & HOST_PAD_SCREEN_LEFT) left_right(-1);
    if (p & HOST_PAD_SCREEN_RIGHT) left_right(1);
    if (p & HOST_PAD_2) select_();
    if (p & HOST_PAD_1) back();
    if (p & HOST_PAD_MINUS) clear();
    return 1;
}

int options_script(uint32_t held) {
    static uint32_t last;
    static int swallow;
    return remote_input(held, &last, &swallow);
}

int options_touch_buttons(uint32_t held) {
    static uint32_t last;
    static int swallow;
    return remote_input(held, &last, &swallow);
}
