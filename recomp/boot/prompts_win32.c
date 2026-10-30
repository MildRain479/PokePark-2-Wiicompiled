/* Button prompt icons (hle_prompts.c): which icon shows a Wii Remote
 * button, from what the player has bound to it on the device used last,
 * and the icon itself, a PNG from the prompts folder beside the program
 * (recomp/assets/prompts), decoded by Windows (WIC) and scaled to the
 * glyph it replaces. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_input.h"
#include "host_settings.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

/* ------------------------------------------------------------------------ */
/* Which icon */

static const char* key_icon(int vk) {
    static char name[32];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '1' && vk <= '9')) {
        snprintf(name, sizeof(name), "key_%c", (char)(vk >= 'A' ? vk - 'A' + 'a' : vk));
        return name;
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        snprintf(name, sizeof(name), "key_f%d", vk - VK_F1 + 1);
        return name;
    }
    switch (vk) {
    case VK_SPACE: return "key_space";
    case VK_RETURN: return "key_enter";
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return "key_ctrl";
    case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT: return "key_shift";
    case VK_TAB: return "key_tab";
    case VK_ESCAPE: return "key_esc";
    case VK_DELETE: return "key_delete";
    case VK_CAPITAL: return "key_capslock";
    case VK_OEM_MINUS: return "key_minus";
    case VK_OEM_PLUS: return "key_equal";
    case VK_ADD: return "key_plus";
    case VK_OEM_2: return "key_slash";
    case VK_OEM_PERIOD: return "key_period";
    case VK_OEM_COMMA: return "key_comma";
    case VK_LBUTTON: return "mouse_left";
    case VK_RBUTTON: return "mouse_right";
    case VK_MBUTTON: return "mouse_middle";
    }
    return NULL;
}

static const char* pad_icon(uint32_t b) {
    switch (b) {
    case XPAD_A: return "pad_a";
    case XPAD_B: return "pad_b";
    case XPAD_X: return "pad_x";
    case XPAD_Y: return "pad_y";
    case XPAD_LB: return "pad_lb";
    case XPAD_RB: return "pad_rb";
    case XPAD_LT: return "pad_lt";
    case XPAD_RT: return "pad_rt";
    case XPAD_START: return "pad_start";
    case XPAD_BACK: return "pad_back";
    case XPAD_LTHUMB: return "pad_lstick";
    case XPAD_RTHUMB: return "pad_rstick";
    }
    return NULL;
}

/* The icon for a Wii Remote button (HOST_PAD_*; HOST_PAD_SCREEN_UP for
 * the d-pad as a whole), or NULL to keep the game's glyph. */
static const char* icon_for(uint32_t button) {
    static const uint32_t kBit[ACT_COUNT] = {
        HOST_PAD_SCREEN_UP, HOST_PAD_SCREEN_DOWN, HOST_PAD_SCREEN_LEFT, HOST_PAD_SCREEN_RIGHT, HOST_PAD_2, HOST_PAD_1,
        HOST_PAD_A, HOST_PAD_B, HOST_PAD_PLUS, HOST_PAD_MINUS, HOST_PAD_HOME, HOST_PAD_SHAKE, HOST_PAD_SHAKE2,
    };
    int a, k, pad = input_controller_last();
    /* The d-pad: the left stick moves too; four keys have no one icon. */
    if (button == HOST_PAD_SCREEN_UP) return pad ? "pad_lstick" : NULL;
    for (a = 0; a < ACT_COUNT && kBit[a] != button; a++) {}
    if (a == ACT_COUNT) return NULL;
    for (k = 0; k < BIND_SLOTS; k++) {
        const char* n = pad ? pad_icon(g_settings.pad[a][k]) : key_icon(g_settings.key[a][k]);
        if (n) return n;
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* The icons themselves */

typedef struct {
    char name[32];
    int w, h, ok;
    uint8_t* alpha; /* w x h, top row first */
} Icon;

static Icon sIcons[96];
static int sIconCount;

static int decode_png(const char* path, Icon* icon) {
    static IWICImagingFactory* factory;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    WCHAR wpath[MAX_PATH];
    UINT w = 0, h = 0;
    uint8_t* bgra;
    int ok = 0, i;
    if (!factory) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                    (void**)&factory)))
            return 0;
    }
    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
    if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(factory, wpath, NULL, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &conv)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppBGRA,
                                                 WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(IWICFormatConverter_GetSize(conv, &w, &h)) && w && h && w <= 1024 && h <= 1024) {
        bgra = (uint8_t*)malloc((size_t)w * h * 4);
        if (bgra && SUCCEEDED(IWICFormatConverter_CopyPixels(conv, NULL, w * 4, w * h * 4, bgra))) {
            icon->alpha = (uint8_t*)malloc((size_t)w * h);
            for (i = 0; i < (int)(w * h); i++) icon->alpha[i] = bgra[4 * i + 3];
            icon->w = (int)w;
            icon->h = (int)h;
            ok = 1;
        }
        free(bgra);
    }
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    return ok;
}

static Icon* load_icon(const char* name) {
    char path[MAX_PATH], *slash;
    int i;
    for (i = 0; i < sIconCount; i++) {
        if (!strcmp(sIcons[i].name, name)) return sIcons[i].ok ? &sIcons[i] : NULL;
    }
    if (sIconCount == (int)(sizeof(sIcons) / sizeof(sIcons[0]))) return NULL;
    Icon* icon = &sIcons[sIconCount++];
    snprintf(icon->name, sizeof(icon->name), "%s", name);
    GetModuleFileNameA(NULL, path, sizeof(path) - 64);
    slash = strrchr(path, '\\');
    snprintf(slash ? slash + 1 : path, 64, "prompts\\%s.png", name);
    icon->ok = decode_png(path, icon);
    return icon->ok ? icon : NULL;
}

/* The icon for button, as coverage (0-255) in a w x h box, top row first:
 * as large as fits at its own shape, centred, scaled down by averaging.
 * Returns 0 to keep the game's glyph. */
static int prompt_icon(uint32_t button, uint8_t* out, int w, int h) {
    const char* name = icon_for(button);
    Icon* icon = name ? load_icon(name) : NULL;
    int x, y, dw, dh, ox, oy;
    if (!icon) return 0;
    dw = w;
    dh = icon->h * w / icon->w;
    if (dh > h) {
        dh = h;
        dw = icon->w * h / icon->h;
    }
    ox = (w - dw) / 2;
    oy = (h - dh) / 2;
    memset(out, 0, (size_t)w * h);
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            int x0 = x * icon->w / dw, x1 = (x + 1) * icon->w / dw, y0 = y * icon->h / dh, y1 = (y + 1) * icon->h / dh;
            int sx, sy, sum = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            for (sy = y0; sy < y1; sy++) {
                for (sx = x0; sx < x1; sx++) {
                    sum += icon->alpha[sy * icon->w + sx];
                    n++;
                }
            }
            out[(oy + y) * w + ox + x] = (uint8_t)(sum / n);
        }
    }
    return 1;
}

/* Changes whenever the icons to show may have: the bindings were saved, or
 * the other device was used. */
static uint32_t prompt_generation(void) { return settings_generation() * 2 + (uint32_t)input_controller_last(); }

void prompts_init(void) {
    g_host_prompt_icon = prompt_icon;
    g_host_prompt_generation = prompt_generation;
}
