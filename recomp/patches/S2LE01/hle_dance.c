/* Dance Inferno (attraction 2): a key for each move.
 *
 * On the Wii any shake across the remote danced whatever move came up:
 * the game judges only when the remote is shaken, not which way (shaken
 * along its length it counts nothing). Here each move has its own key,
 * shown on its icon along the bottom, and a shake comes only from the key
 * of the move due: pressed in time on the move's icon, then held, it
 * dances the rest of that move's beats (the diamonds on its bar) in time.
 *
 * The dance (its object's vtable 0x80463280) has two parts to its song,
 * each with its own frame (fn_801B8170, then fn_801B8850), position in
 * ticks (+0x1F4C, then +0x1F50; a tick a frame) and chart (+0x1F04, then
 * +0x1F08; At01StageData.rlb's, a part of it for each difficulty), and
 * the ticks to a slot of the chart at +0x1F54 (half a beat: 30, then 24). The chart's rows are a word a slot; the player's cues, the
 * icons along the bottom, are the row at +0xBCC: a move's icon, then along
 * its bar 0x1F for each further beat of it (a diamond) and 0 between, and
 * 0xFF for none.
 *
 * The icons are pictures of the HUD's archive (Archive/MenuAttraction.dac,
 * arc/timg/At01UIDanceIcon00-07.tpl, CI8 160x160), which stays in memory
 * whole while the dance is on: the key bound to each move is drawn on its
 * corner, in the icon's own colours, and again when the bindings or the
 * device change (hle_prompts.c draws the button prompts alike). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

#define DANCE_VTABLE 0x80463280u
#define DANCE_SLOT_TICKS 0x1F54
#define CHART_CUES 0xBCC
#define CHART_SLOTS 151 /* each row's */

#define CUE_BEAT 0x1F /* a further beat of the move before */
#define CUE_HELD 0    /* the move before goes on */
#define CUE_NONE 0xFF

/* The moves (their cues), their icons and keys. */
static const struct {
    uint32_t cue;
    int icon;
    uint32_t key;
} kMoves[] = {
    {0x10, 0, HOST_PAD_SCREEN_LEFT},  /* arms pumped up and down */
    {0x12, 2, HOST_PAD_SCREEN_UP},    /* arms swung overhead */
    {0x16, 1, HOST_PAD_SCREEN_DOWN},  /* hips swung */
    {0x14, 7, HOST_PAD_SCREEN_RIGHT}, /* a spin */
    {0x13, 3, HOST_PAD_SHAKE2},       /* a kick */
    {0x20, 4, HOST_PAD_2},            /* the poses */
    {0x21, 5, HOST_PAD_2},
    {0x22, 6, HOST_PAD_2},
};
#define MOVES ((int)(sizeof(kMoves) / sizeof(kMoves[0])))
#define DANCE_KEYS                                                                                                   \
    (HOST_PAD_SCREEN_LEFT | HOST_PAD_SCREEN_UP | HOST_PAD_SCREEN_DOWN | HOST_PAD_SCREEN_RIGHT | HOST_PAD_SHAKE |      \
     HOST_PAD_SHAKE2 | HOST_PAD_2)

static const struct {
    uint32_t tick, chart;
} kParts[2] = {{0x1F4C, 0x1F04}, {0x1F50, 0x1F08}};

static uint32_t sDance;
static int sPart; /* kParts' */
static uint64_t sDanceSeen;

static int move_of(uint32_t cue) {
    int i;
    for (i = 0; i < MOVES; i++) {
        if (kMoves[i].cue == cue) return i;
    }
    return -1;
}

/* The song now: 0 if the dance isn't on. */
static int song(int32_t* tick, int32_t* slotTicks, uint32_t* cues) {
    uint32_t d = sDance, chart;
    if (!d || ppc_timebase() - sDanceSeen > PPC_TB_HZ / 4 || LD32(d) != DANCE_VTABLE) return 0;
    *tick = (int32_t)LD32(d + kParts[sPart].tick);
    *slotTicks = (int32_t)LD32(d + DANCE_SLOT_TICKS);
    chart = LD32(d + kParts[sPart].chart);
    if (*slotTicks <= 0 || *slotTicks > 1000 || *tick < -1000 || *tick > 1000000 || chart < 0x80000000u) return 0;
    *cues = chart + CHART_CUES;
    return 1;
}

static uint32_t cue_at(uint32_t cues, int slot) { return slot >= 0 && slot < CHART_SLOTS ? LD32(cues + 4 * slot) : CUE_NONE; }

/* The move a cue is of: a beat's, the move it follows. */
static int move_at(uint32_t cues, int slot) {
    for (; slot >= 0; slot--) {
        uint32_t c = cue_at(cues, slot);
        if (c != CUE_BEAT && c != CUE_HELD) return move_of(c);
    }
    return -1;
}

static int is_cue(uint32_t c) { return c == CUE_BEAT || move_of(c) >= 0; }

/* A press is for the cue nearest it, within a slot. */
static int nearest_cue(uint32_t cues, int32_t tick, int32_t slotTicks) {
    int s = tick / slotTicks, best = -1, i;
    int32_t bestD = slotTicks + 1;
    for (i = s - 1; i <= s + 2; i++) {
        int32_t dt = tick - i * slotTicks;
        if (dt < 0) dt = -dt;
        if (is_cue(cue_at(cues, i)) && dt < bestD) {
            bestD = dt;
            best = i;
        }
    }
    return best;
}

/* For testing (RECOMP_DBG_DANCE=auto): the keys each move asks for, held
 * from just before it comes up to its last beat, let go between moves. */
static uint32_t auto_keys(uint32_t cues, int32_t tick, int32_t slotTicks) {
    static int lastSlot = -1;
    int s = (tick + 2) / slotTicks, m;
    uint32_t c = cue_at(cues, s);
    if (c == CUE_NONE || (m = move_at(cues, s)) < 0) return 0;
    if (c != CUE_BEAT && c != CUE_HELD && s != lastSlot) {
        lastSlot = s;
        return 0; /* let go, to press it again */
    }
    return kMoves[m].key;
}

static int debug_dance(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("RECOMP_DBG_DANCE");
        v = !e ? 0 : !strcmp(e, "auto") ? 2 : 1;
    }
    return v;
}

/* The shake for the keys held: +1 or -1 while one lasts, else 0
 * (hle_motion.c, for Dance Inferno). */
int hle_dance_swing(uint32_t held, uint32_t report) {
    static uint32_t last, swingEnd, holding;
    static int sign, holdMove = -1, done = -1, part = -1;
    static int32_t lastTick;
    uint32_t pressed, cues;
    int32_t tick, slotTicks;
    int swing = 0, playing = song(&tick, &slotTicks, &cues);
    if (playing && (sPart != part || tick < lastTick)) {
        /* The song's next part, or again from the start. */
        part = sPart;
        done = -1;
        holding = 0;
    }
    if (playing) lastTick = tick;
    if (playing && debug_dance() == 2) held = auto_keys(cues, tick, slotTicks);
    pressed = held & ~last & DANCE_KEYS;
    last = held;
    if (!playing) {
        /* Not playing (or not found): a press is a shake, as before. */
        holding = 0;
        swing = pressed != 0;
    } else {
        if (pressed) {
            int s = nearest_cue(cues, tick, slotTicks), m = s >= 0 ? move_at(cues, s) : -1;
            holding = 0;
            if (m >= 0 && (pressed & kMoves[m].key)) {
                swing = 1;
                done = s;
                /* The poses are a press each. */
                if (kMoves[m].key != HOST_PAD_2) {
                    holding = kMoves[m].key;
                    holdMove = m;
                }
            }
        }
        if (!(held & holding) || cue_at(cues, (tick + slotTicks / 2) / slotTicks) == CUE_NONE) holding = 0; /* the bar's end */
        if (holding && !swing) {
            /* Its next beat, as it comes: the slot whose time this report reaches. */
            int s = (tick + 1) / slotTicks;
            int32_t at = s * slotTicks;
            if (s > done && tick + 1 >= at && is_cue(cue_at(cues, s))) {
                if (move_at(cues, s) == holdMove) {
                    swing = 1;
                    done = s;
                } else {
                    holding = 0; /* another move: its own key */
                }
            }
        }
    }
    if (swing && debug_dance() && playing)
        fprintf(stderr, "dance: shake at tick %d slot %d cue %02X%s\n", (int)tick, (int)(tick / slotTicks),
                cue_at(cues, (tick + slotTicks / 2) / slotTicks), pressed ? " (pressed)" : " (held)");
    if (swing) {
        sign = sign > 0 ? -1 : 1;
        swingEnd = report + 6; /* 30 ms, as hle_motion.c's swings */
    }
    return (int32_t)(swingEnd - report) > 0 ? sign : 0;
}

/* ------------------------------------------------------------------------ */
/* The keys on the icons */

#define ICONS 8
#define ICON_W 160
#define ICON_H 160
#define BADGE 58 /* the key's box, in the icon's lower right corner */
#define EDGE 3   /* the white edge around the key */

static uint32_t sArchive, sGeneration = 0xFFFFFFFFu;
static uint64_t sLastSearch;
static uint32_t sIcon[ICONS];       /* each icon's TPL, 0 if not found */
static uint8_t* sOriginal[ICONS];   /* its pixels as the game had them */

static int is_archive(uint32_t h) {
    return h && LD32(h) == 0x55AA382Du && LD32(h + 4) == 0x20;
}

static int name_at(uint32_t at, const char* s) {
    for (; *s; s++, at++) {
        if (LD8(at) != (uint8_t)*s) return 0;
    }
    return LD8(at) == 0;
}

/* The archive holding the icons: found by an icon's name, then the U8
 * header before it whose name table it is in. */
static uint32_t find_archive(void) {
    static const struct {
        uint32_t base, size;
    } kRam[] = {{0x90000000u, 0x04000000u}, {0x80000000u, 0x01800000u}};
    static const char kName[] = "At01UIDanceIcon00.tpl";
    int r;
    for (r = 0; r < 2; r++) {
        const uint8_t *start = MA(kRam[r].base), *p = start, *end = start + kRam[r].size - sizeof(kName);
        while (p < end && (p = (const uint8_t*)memchr(p, 'A', (size_t)(end - p))) != NULL) {
            if (!memcmp(p, kName, sizeof(kName))) {
                uint32_t at = kRam[r].base + (uint32_t)(p - start), h;
                for (h = (at & ~3u) - 4; h + 0x40000 > at && h > kRam[r].base; h -= 4) {
                    if (is_archive(h)) {
                        uint32_t n = LD32(h + 0x28), names = h + 0x20 + n * 12;
                        if (n > 1 && n < 10000 && at >= names && at < h + 0x20 + LD32(h + 8)) return h;
                    }
                }
            }
            p++;
        }
    }
    return 0;
}

/* The file of the archive at h named name, 0 if none. */
static uint32_t archive_file(uint32_t h, const char* name) {
    uint32_t n = LD32(h + 0x28), names = h + 0x20 + n * 12, i;
    for (i = 1; i < n; i++) {
        uint32_t node = h + 0x20 + i * 12;
        if (LD8(node) == 0 && name_at(names + (LD32(node) & 0xFFFFFF), name)) return h + LD32(node + 4);
    }
    return 0;
}

static uint32_t ci8_byte(uint32_t image, int w, int x, int y) {
    return image + (uint32_t)(((y / 4) * ((w + 7) / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8);
}

typedef struct {
    uint32_t image;
    int colours;
    uint8_t rgba[256][4];
} Picture;

/* A place in a TPL: an offset in the file, or once the game has bound it
 * (TPLBind), the address itself; 0 if it is neither. */
static uint32_t tpl_at(uint32_t tpl, uint32_t v) {
    uint32_t a = v >= 0x80000000u ? v : tpl + v;
    return a >= tpl && a - tpl < 0x40000u ? a : 0;
}

/* An icon's image and palette (RGB5A3, RGB565 or IA8), if it is the CI8 it was. */
static int picture(uint32_t tpl, Picture* p) {
    uint32_t table, ih, ph, pal, fmt;
    int i;
    if (LD32(tpl) != 0x0020AF30u || LD32(tpl + 4) != 1) return 0;
    if (!(table = tpl_at(tpl, LD32(tpl + 8))) || !(ih = tpl_at(tpl, LD32(table))) || !(ph = tpl_at(tpl, LD32(table + 4))))
        return 0;
    if (LD16(ih) != ICON_H || LD16(ih + 2) != ICON_W || LD32(ih + 4) != 9) return 0;
    if (!(p->image = tpl_at(tpl, LD32(ih + 8))) || !(pal = tpl_at(tpl, LD32(ph + 8)))) return 0;
    p->colours = LD16(ph);
    fmt = LD32(ph + 4);
    if (p->colours > 256) p->colours = 256;
    for (i = 0; i < p->colours; i++) {
        uint32_t v = LD16(pal + 2 * i);
        uint8_t* c = p->rgba[i];
        if (fmt == 2 && (v & 0x8000)) {
            c[0] = (uint8_t)((v >> 10 & 31) * 255 / 31), c[1] = (uint8_t)((v >> 5 & 31) * 255 / 31);
            c[2] = (uint8_t)((v & 31) * 255 / 31), c[3] = 255;
        } else if (fmt == 2) {
            c[0] = (uint8_t)((v >> 8 & 15) * 17), c[1] = (uint8_t)((v >> 4 & 15) * 17), c[2] = (uint8_t)((v & 15) * 17);
            c[3] = (uint8_t)((v >> 12 & 7) * 255 / 7);
        } else if (fmt == 1) {
            c[0] = (uint8_t)((v >> 11 & 31) * 255 / 31), c[1] = (uint8_t)((v >> 5 & 63) * 255 / 63);
            c[2] = (uint8_t)((v & 31) * 255 / 31), c[3] = 255;
        } else {
            c[0] = c[1] = c[2] = (uint8_t)v, c[3] = (uint8_t)(v >> 8);
        }
    }
    return p->colours > 0;
}

/* The colour nearest r g b a, weighing its colour by how opaque it is. */
static int nearest(const Picture* b, int r, int g, int bl, int a) {
    int i, best = 0;
    long bestD = -1;
    for (i = 0; i < b->colours; i++) {
        const uint8_t* p = b->rgba[i];
        long da = (long)(p[3] - a) * 2, dr = p[0] - r, dg = p[1] - g, db = p[2] - bl;
        long d = da * da + (dr * dr + dg * dg + db * db) * (a + p[3]) / 510;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

/* The key for button in the corner of the picture: drawn in the darkest
 * colour the icon has (its outline's), inside it white, with a white edge
 * all round, so it reads over the icon and the bar alike. */
static void draw_key(const Picture* b, uint32_t button) {
    static uint8_t cov[BADGE * BADGE], inside[BADGE * BADGE];
    static int stack[BADGE * BADGE];
    const int s = BADGE - 2 * EDGE, x0 = ICON_W - BADGE, y0 = ICON_H - BADGE;
    int x, y, i, n = 0, dark = 0, darkness = -1;
    if (!g_host_prompt_icon || !g_host_prompt_icon(button | HOST_PROMPT_ALONE, cov, s, s)) return;
    for (i = 0; i < b->colours; i++) {
        const uint8_t* p = b->rgba[i];
        int v = p[3] == 255 ? 765 - p[0] - p[1] - p[2] : -1;
        if (v > darkness) {
            darkness = v;
            dark = i;
        }
    }
    /* Inside the key: what can't be reached from the box's edge without
     * crossing its outline. */
    memset(inside, 1, (size_t)s * s);
    for (i = 0; i < s * s; i++) {
        x = i % s;
        y = i / s;
        if ((x == 0 || y == 0 || x == s - 1 || y == s - 1) && cov[i] < 128) {
            inside[i] = 0;
            stack[n++] = i;
        }
    }
    while (n) {
        static const int kStep[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        int at = stack[--n], d;
        for (d = 0; d < 4; d++) {
            int nx = at % s + kStep[d][0], ny = at / s + kStep[d][1], j = ny * s + nx;
            if (nx < 0 || ny < 0 || nx >= s || ny >= s || !inside[j] || cov[j] >= 128) continue;
            inside[j] = 0;
            stack[n++] = j;
        }
    }
    for (y = 0; y < BADGE; y++) {
        for (x = 0; x < BADGE; x++) {
            int ix = x - EDGE, iy = y - EDGE, ink = 0, white = 0, dx, dy;
            const uint8_t* line = b->rgba[dark];
            if (ix >= 0 && iy >= 0 && ix < s && iy < s) {
                ink = cov[iy * s + ix];
                if (inside[iy * s + ix]) white = 255;
            }
            for (dy = -EDGE; dy <= EDGE; dy++) {
                for (dx = -EDGE; dx <= EDGE; dx++) {
                    int jx = ix + dx, jy = iy + dy, v;
                    if (dx * dx + dy * dy > EDGE * EDGE + 1 || jx < 0 || jy < 0 || jx >= s || jy >= s) continue;
                    v = inside[jy * s + jx] ? 255 : cov[jy * s + jx];
                    if (v > white) white = v;
                }
            }
            if ((ink > white ? ink : white) < 96) continue; /* the icon shows through */
            ST8(ci8_byte(b->image, ICON_W, x0 + x, y0 + y),
                (uint8_t)nearest(b, (255 * (255 - ink) + line[0] * ink) / 255, (255 * (255 - ink) + line[1] * ink) / 255,
                                 (255 * (255 - ink) + line[2] * ink) / 255, 255));
        }
    }
}

static void label_icons(void) {
    uint32_t gen = g_host_prompt_generation ? g_host_prompt_generation() : 0;
    int i, m;
    if (!is_archive(sArchive) || !sIcon[0] || LD32(sIcon[0]) != 0x0020AF30u) {
        /* (Searched for at most twice a second, while the dance is on.) */
        if (ppc_timebase() - sLastSearch < PPC_TB_HZ / 2) return;
        sLastSearch = ppc_timebase();
        sArchive = find_archive();
        for (i = 0; i < ICONS; i++) {
            char name[32];
            snprintf(name, sizeof(name), "At01UIDanceIcon%02d.tpl", i);
            sIcon[i] = sArchive ? archive_file(sArchive, name) : 0;
            free(sOriginal[i]);
            sOriginal[i] = NULL;
        }
        sGeneration = 0xFFFFFFFFu;
        if (debug_dance())
            fprintf(stderr, "dance: icons' archive %08X, icons %08X %08X .. %08X\n", sArchive, sIcon[0], sIcon[1], sIcon[7]);
        if (!sArchive) return;
    }
    if (gen == sGeneration) return;
    sGeneration = gen;
    for (m = 0; m < MOVES; m++) {
        static Picture p;
        int k = kMoves[m].icon;
        if (!sIcon[k] || !picture(sIcon[k], &p)) continue;
        if (!sOriginal[k]) {
            sOriginal[k] = (uint8_t*)malloc(ICON_W * ICON_H);
            if (!sOriginal[k]) continue;
            memcpy(sOriginal[k], MA(p.image), ICON_W * ICON_H);
        }
        /* (Noted as written for the texture cache, before writing.) */
        ppc_mem_host_write(p.image, ICON_W * ICON_H);
        memcpy(MA(p.image), sOriginal[k], ICON_W * ICON_H);
        draw_key(&p, kMoves[m].key);
    }
}

/* The dance, each frame of a part of the song (each moves it on a tick). */
static void dance_frame(ppc_t* c, int part) {
    uint32_t d = c->r[3];
    if (LD32(d) != DANCE_VTABLE) return;
    sDance = d;
    sPart = part;
    sDanceSeen = ppc_timebase();
    if (debug_dance() && LD32(d + kParts[part].tick) % 120 == 0)
        fprintf(stderr, "dance: part %d tick %d, %d a slot\n", part + 1, (int)LD32(d + kParts[part].tick),
                (int)LD32(d + DANCE_SLOT_TICKS));
    label_icons();
}

void F_DanceInferno_Frame(ppc_t* c) {
    dance_frame(c, 0);
    F_DanceInferno_Frame_orig(c);
}

void F_DanceInferno_Frame2(ppc_t* c) {
    dance_frame(c, 1);
    F_DanceInferno_Frame2_orig(c);
}
