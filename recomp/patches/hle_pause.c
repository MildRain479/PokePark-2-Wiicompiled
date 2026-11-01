/* The PokéPark Pad (the pause menu in the park) and the save file screen,
 * for the Options tab drawn over them (options_win32.c): whether the Pad
 * is open on its main page or the save file screen is up, and their
 * pictures, to draw the tab in.
 *
 * The Pad is a menu script (MenuScriptPauseMenu); its object's state is 5
 * while it's open, and the script sets a flag while its buttons can be
 * chosen, which it clears on going into one of them (Pikachu's Stats, the
 * Friend List, saving). Left and right do nothing there.
 *
 * The save file screen (Game 1, Game 2, Arcade) is a menu script too, and
 * where its cursor is couldn't be found; it is up while its Arcade plank is
 * drawn (its texture, MaAtMenu, loaded for drawing: GXLoadTexObj).
 *
 * The pictures are textures of Pause.brres, which stays loaded, and of the
 * start menu's Main.brres: found as each file is set up (ResTex::Init,
 * hle_prompts.c), and decoded when the menu first asks for them. */
#include <stdlib.h>
#include <string.h>

#include "hle.h"

static uint32_t sPause;

/* MenuScriptPauseMenu::MenuScriptPauseMenu() */
void f_80122C60(ppc_t* c) {
    uint32_t self = c->r[3];
    f_80122C60_orig(c);
    sPause = self;
}

int hle_pause_main_page(void) {
    return sPause && LD32(sPause + 0x328) == 5 && (LD32(sPause + 0x32C) & 1);
}

static uint32_t sArcadeImage;
static uint64_t sArcadeDrawn; /* when, on the time base */

/* void GXLoadTexObjPreLoaded(GXTexObj* obj, GXTexRegion* region, GXTexMapID id):
 * each time a texture is drawn. The object (often a copy) holds the
 * image's physical address over 32 in its fourth word. */
void f_80222E00(ppc_t* c) {
    if (sArcadeImage && (LD32(c->r[3] + 0xC) & 0xFFFFFFu) == (sArcadeImage & 0x3FFFFFFFu) >> 5)
        sArcadeDrawn = ppc_timebase();
    f_80222E00_orig(c);
}

int hle_file_select_shown(void) { return sArcadeDrawn && ppc_timebase() - sArcadeDrawn < PPC_TB_HZ / 8; }

/* ------------------------------------------------------------------------ */
/* Pictures */

static const char* const kWanted[] = {
    "PmPocketbookL01", "PmPocketbookL01A", "PmPocketbookR", "PmPocketbookRA", "PmMenuButton", "PmButtonPocket",
    "PmReaf01",        "PmReaf01A",        "PmArrowheadMat", "PmArrowheadMatAlp", "MaAtMenu",
};
#define WANTED ((int)(sizeof(kWanted) / sizeof(kWanted[0])))

static struct {
    uint32_t tex;
    int w, h;
    uint32_t* argb; /* decoded, once asked for */
} sTex[WANTED];

static int name_is(uint32_t at, const char* s) {
    for (; *s; s++, at++) {
        if (LD8(at) != (uint8_t)*s) return 0;
    }
    return LD8(at) == 0;
}

void hle_pause_texture(uint32_t tex) {
    int i;
    if (LD32(tex) != 0x54455830) return; /* TEX0 */
    for (i = 0; i < WANTED; i++) {
        if (!name_is(tex + LD32(tex + 0x14), kWanted[i])) continue;
        if (sTex[i].tex != tex) {
            free(sTex[i].argb);
            sTex[i].argb = NULL;
            sTex[i].tex = tex;
        }
        if (!strcmp(kWanted[i], "MaAtMenu")) sArcadeImage = tex + LD32(tex + 0x10);
        return;
    }
}

static uint32_t rgb5a3(uint16_t v) {
    if (v & 0x8000)
        return 0xFF000000u | (uint32_t)((v >> 10 & 31) * 255 / 31) << 16 | (uint32_t)((v >> 5 & 31) * 255 / 31) << 8 |
               (uint32_t)((v & 31) * 255 / 31);
    return (uint32_t)((v >> 12 & 7) * 255 / 7) << 24 | (uint32_t)((v >> 8 & 15) * 17) << 16 |
           (uint32_t)((v >> 4 & 15) * 17) << 8 | (uint32_t)((v & 15) * 17);
}

static uint32_t rgb565(uint16_t v) {
    return 0xFF000000u | (uint32_t)((v >> 11) * 255 / 31) << 16 | (uint32_t)((v >> 5 & 63) * 255 / 63) << 8 |
           (uint32_t)((v & 31) * 255 / 31);
}

/* The PLT0 of a texture's name, in its file; 0 if none. */
static uint32_t palette_of(uint32_t tex) {
    uint32_t file = tex + LD32(tex + 0xC), name = tex + LD32(tex + 0x14), pos, size;
    if (LD32(file) != 0x62726573) return 0; /* bres */
    size = LD32(file + 8);
    for (pos = file; pos + 0x20 <= file + size; pos += 4) {
        if (LD32(pos) != 0x504C5430) continue; /* PLT0 */
        uint32_t n = pos + LD32(pos + 0x14), k;
        for (k = 0; LD8(n + k) == LD8(name + k) && LD8(n + k); k++) {}
        if (LD8(n + k) == LD8(name + k)) return pos;
    }
    return 0;
}

/* Decodes I4, I8, CI4, CI8 and CMPR, the formats the Pad uses. */
static uint32_t* decode(uint32_t tex, int* pw, int* ph) {
    int w = LD16(tex + 0x1C), h = LD16(tex + 0x1E), fmt = (int)LD32(tex + 0x20), bw, bh, tx, ty, x, y;
    uint32_t data = tex + LD32(tex + 0x10), plt = 0, pal = 0, *out;
    uint32_t colours[256];
    if (fmt == 8 || fmt == 9) {
        int n, i;
        if (!(plt = palette_of(tex))) return NULL;
        pal = plt + LD32(plt + 0x10);
        n = LD16(plt + 0x1C);
        for (i = 0; i < 256; i++) {
            uint16_t v = i < n ? LD16(pal + 2 * i) : 0;
            colours[i] = LD32(plt + 0x18) == 2 ? rgb5a3(v) : LD32(plt + 0x18) == 1 ? rgb565(v) : 0;
        }
    } else if (fmt != 0 && fmt != 1 && fmt != 14) {
        return NULL;
    }
    out = (uint32_t*)calloc((size_t)w * h, 4);
    if (!out) return NULL;
    bw = 8;
    bh = fmt == 1 || fmt == 9 ? 4 : 8;
    for (ty = 0; ty < h; ty += bh) {
        for (tx = 0; tx < w; tx += bw) {
            if (fmt == 14) { /* four 4x4 blocks */
                int sb, i;
                for (sb = 0; sb < 4; sb++, data += 8) {
                    uint16_t c0 = LD16(data), c1 = LD16(data + 2);
                    uint32_t bits = LD32(data + 4), c[4], a = rgb565(c0), b = rgb565(c1);
                    int k;
                    c[0] = a;
                    c[1] = b;
                    c[2] = c[3] = 0xFF000000u;
                    for (k = 0; k < 24; k += 8) {
                        int ca = (int)(a >> k & 255), cb = (int)(b >> k & 255);
                        c[2] |= (uint32_t)(c0 > c1 ? (2 * ca + cb) / 3 : (ca + cb) / 2) << k;
                        c[3] |= (uint32_t)((ca + 2 * cb) / 3) << k;
                    }
                    if (c0 <= c1) c[3] = 0;
                    for (i = 0; i < 16; i++) {
                        x = tx + (sb % 2) * 4 + i % 4;
                        y = ty + (sb / 2) * 4 + i / 4;
                        if (x < w && y < h) out[y * w + x] = c[bits >> (30 - 2 * i) & 3];
                    }
                }
                continue;
            }
            for (y = 0; y < bh; y++) {
                for (x = 0; x < bw; x++) {
                    int v;
                    if (fmt == 0 || fmt == 8) {
                        v = LD8(data + (uint32_t)(y * 8 + x) / 2);
                        v = x % 2 ? v & 15 : v >> 4;
                    } else {
                        v = LD8(data + (uint32_t)(y * 8 + x));
                    }
                    if (tx + x >= w || ty + y >= h) continue;
                    out[(ty + y) * w + tx + x] = fmt == 0   ? 0xFF000000u | (uint32_t)(v * 17) * 0x010101u
                                                 : fmt == 1 ? 0xFF000000u | (uint32_t)v * 0x010101u
                                                            : colours[v];
                }
            }
            data += 32;
        }
    }
    *pw = w;
    *ph = h;
    return out;
}

/* The Pad's model is set up only as the park starts, but its file is
 * loaded from the start (PrepareResidentRes): before that, its textures
 * are found in memory, in a file (bres) of their own. */
static void find_loaded(void) {
    static const uint32_t kRange[2][2] = {{0x80000000u, 0x81800000u}, {0x90000000u, 0x94000000u}};
    static uint64_t last;
    int r, i;
    if (last && ppc_timebase() - last < 2 * PPC_TB_HZ) return;
    last = ppc_timebase();
    for (r = 0; r < 2; r++) {
        uint32_t a;
        for (a = kRange[r][0]; a + 0x40 < kRange[r][1]; a += 4) {
            uint32_t file, name;
            if (LD32(a) != 0x54455830) continue; /* TEX0 */
            file = a + LD32(a + 0xC);
            name = a + LD32(a + 0x14);
            if (file < kRange[r][0] || file >= a || name <= a || name >= kRange[r][1] - 32 || LD32(file) != 0x62726573)
                continue;
            for (i = 0; i < WANTED; i++) {
                if (!sTex[i].tex && name_is(name, kWanted[i])) {
                    sTex[i].tex = a;
                    break;
                }
            }
        }
    }
}

static const uint32_t* image(const char* name, int* w, int* h) {
    int i;
    for (i = 0; i < WANTED && strcmp(kWanted[i], name); i++) {}
    if (i == WANTED) return NULL;
    if (!sTex[i].tex) find_loaded();
    if (!sTex[i].tex || LD32(sTex[i].tex) != 0x54455830) return NULL;
    if (!sTex[i].argb) sTex[i].argb = decode(sTex[i].tex, &sTex[i].w, &sTex[i].h);
    *w = sTex[i].w;
    *h = sTex[i].h;
    return sTex[i].argb;
}

const uint32_t* hle_pause_image(const char* name, const char* alpha, int* w, int* h) {
    static uint32_t* sMerged[WANTED];
    const uint32_t *c = image(name, w, h), *a;
    int aw, ah, i, k;
    if (!c || !alpha) return c;
    if (!(a = image(alpha, &aw, &ah)) || aw != *w || ah != *h) return c;
    /* The colour with the alpha texture's grey as its alpha, kept with the colour's slot. */
    for (i = 0; i < WANTED && strcmp(kWanted[i], name); i++) {}
    if (!sMerged[i]) {
        sMerged[i] = (uint32_t*)malloc((size_t)aw * ah * 4);
        if (!sMerged[i]) return c;
        for (k = 0; k < aw * ah; k++) sMerged[i][k] = (c[k] & 0xFFFFFFu) | (a[k] & 0xFFu) << 24;
    }
    return sMerged[i];
}
