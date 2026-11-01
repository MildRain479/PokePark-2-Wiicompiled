/* The backend interface (gx.h) over a device (gx_device.h): the frames
 * the game draws, shown as they are at 60 fps, and above it recorded and
 * drawn again between the game's; textures freed once nothing draws
 * them; and the frame rate, in the window title.
 *
 * Frames between the game's
 *
 * Above 60 fps the game still draws 60 frames a second (hle_game.c), and
 * the frames between are drawn here: the newest of the game's frames is
 * recorded (its draws with their state and XF state, clears, EFB copies)
 * and drawn again, each draw's matrices blended from those of the same
 * draw in the frame before, by how far through the frame interval the
 * moment shown is. Shown frames run one game frame behind.
 *
 * A draw is recognized across frames by its key (gx_fifo.c: its display
 * lists and their draws) and, among draws with the same key, by position
 * once the camera's motion is taken out
 * (match_draws). Blended are the position, normal and texture matrices,
 * the post-transform matrices, the projection and the lights' position
 * and direction: everything that moves with the camera or the model. A
 * matrix whose rotation or translation changed too much to be the same
 * object moving (draws matched wrongly, a cut) is shown as it is. Draws
 * with no match, and everything else (vertices, textures), are the
 * newest frame's.
 *
 * Environment:
 *   RECOMP_GPU=vulkan|opengl   draw with this, over the Renderer setting
 *   RECOMP_FPS=1               the frame rate and time per call, each second, on stderr
 *   RECOMP_IGNORE_REFRESH=1    frames above the display's refresh rate are drawn too (for measuring) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gx_device.h"
#include "host_settings.h"

/* Entry points mark the thread as inside the graphics driver, so the
 * sampling profiler (boot.c) doesn't hold it suspended there. */
volatile long g_in_gl;

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

static const GXDevice* sDev;
static int sRecording;            /* frames between the game's are drawn */
static unsigned sGameFrames;      /* the game's frames, for the fps report */
static unsigned sPresentsDropped; /* frames left out as the display wasn't ready, for the fps report */

/* ------------------------------------------------------------------------ */
/* Showing a frame */

/* Frames per second and the longest frame of the last second, in the
 * window title and, with RECOMP_FPS=1, on stderr. */
static void report(void) {
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
        /* The share of the second the game thread was not waiting for an
         * event: how much headroom is left. */
        extern uint64_t g_wait_ticks;
        static uint64_t lastWait;
        static unsigned lastGameFrames, lastDropped;
        double secs = (double)(now.QuadPart - since.QuadPart) / freq.QuadPart;
        double busy = 100.0 * (1.0 - (double)(g_wait_ticks - lastWait) / PPC_TB_HZ / secs);
        lastWait = g_wait_ticks;
        snprintf(title, sizeof(title), "PokePark (recompiled) - %.2f fps, slowest frame %.1f ms, busy %.0f%%", fps,
                 worst, busy);
        win_set_title(title);
        if (e && *e == '1')
            fprintf(stderr, "fps: %.2f, slowest frame %.1f ms, busy %.0f%%, game %u, left out %u\n", fps, worst, busy,
                    sGameFrames - lastGameFrames, sPresentsDropped - lastDropped);
        lastGameFrames = sGameFrames;
        lastDropped = sPresentsDropped;
        if (e && *e == '1') print_gl_stats();
        since = now;
        frames = 0;
        worst = 0;
    }
}

/* Presents the EFB rectangle the game copied to the XFB. Nothing slow
 * belongs in here: presenting happens in the GX flush at a retrace, before
 * the retrace interrupt, and a few milliseconds more every frame (reading
 * a controller that isn't plugged in, for one) can leave the game showing
 * every fourth retrace (15 fps). */
static void show(int x, int y, int w, int h) {
    /* Above 60 fps a frame is never waited for: in a window the
     * compositor takes frames at its own rate (with more than one
     * display, maybe a slower one's), and once its queue is full a
     * present waits for it, and the game, drawing these frames while it
     * waits, would slow down. Then the frame is left out. */
    if (sDev->present(x, y, w, h, sRecording)) sPresentsDropped++;
    report();
    win_pump();
    /* Between frames, so a change doesn't lose the one being drawn. */
    win_set_fullscreen(g_settings.fullscreen);
    sDev->apply_settings();
}

/* ------------------------------------------------------------------------ */
/* Textures */

int gxb_texture_generation(void) { return g_settings.aniso; }

/* Textures are deleted after the frame is presented: a draw earlier in the
 * frame may still refer to one, and a device may hand a deleted handle
 * straight to the next texture made. Freed while a frame was recorded,
 * they are still drawn by its replays until the next frame is complete. */
#define MAX_PENDING_FREES 1024
static uint32_t sPendingFree[MAX_PENDING_FREES], sFreeLater[MAX_PENDING_FREES];
static int sPendingFreeCount, sFreeLaterCount;

static void free_textures(uint32_t* list, int* count) {
    if (*count) {
        sDev->texture_delete(list, *count);
        *count = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Recording */

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
static int sOutputRate = 60, sHaveFrame;
static uint64_t sCurAt, sNextOut, sFramePeriod; /* time base */
static double sLastT = -1;

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

/* How far apart two draws are: the sum of the differences of their
 * position matrices' translations (the first 10 matrices of XF memory),
 * or at least limit. */
static float matrix_distance(const uint32_t* a, const uint32_t* b, float limit) {
    float d = 0;
    int i;
    for (i = 3; i < 120 && d < limit; i += 4) {
        float x, y;
        if (a[i] == b[i]) continue;
        memcpy(&x, &a[i], 4);
        memcpy(&y, &b[i], 4);
        if (x == x && y == y) d += fabsf(y - x);
    }
    return d;
}

/* A draw's position matrix (MATINDEX_A's), 3x4, in its XF state. */
static const uint32_t* pos_matrix(const uint32_t* xf) {
    return xf + 4 * (xf[GX_XF_MEM_WORDS + 0x18] & 0x3F) % 0x100;
}

static int perspective(const uint32_t* xf) { return xf[GX_XF_MEM_WORDS + 0x26] == 0; }

/* 3x4 matrices (rows of rotation and scale, then translation), as floats. */
typedef struct { float m[3][4]; } Mtx34;

static int load_mtx(Mtx34* out, const uint32_t* w) {
    int i;
    for (i = 0; i < 12; i++) {
        float f;
        memcpy(&f, &w[i], 4);
        if (!(f == f) || fabsf(f) > 1e7f) return 0;
        out->m[i / 4][i % 4] = f;
    }
    return 1;
}

/* out = a * b, as affine transforms. */
static void mtx_mul(Mtx34* out, const Mtx34* a, const Mtx34* b) {
    Mtx34 r;
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + (j == 3 ? a->m[i][3] : 0);
        }
    }
    *out = r;
}

static int mtx_inverse(Mtx34* out, const Mtx34* a) {
    const float(*m)[4] = a->m;
    float c[3][3], det;
    int i, j;
    c[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    c[0][1] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
    c[0][2] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
    c[1][0] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    c[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
    c[1][2] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
    c[2][0] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    c[2][1] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
    c[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    det = m[0][0] * c[0][0] + m[0][1] * c[1][0] + m[0][2] * c[2][0];
    if (fabsf(det) < 1e-12f) return 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) out->m[i][j] = c[i][j] / det;
        out->m[i][3] = -(out->m[i][0] * m[0][3] + out->m[i][1] * m[1][3] + out->m[i][2] * m[2][3]);
    }
    return 1;
}

static int mtx_close(const Mtx34* a, const Mtx34* b) {
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (fabsf(a->m[i][j] - b->m[i][j]) > 0.02f) return 0;
        }
        if (fabsf(a->m[i][3] - b->m[i][3]) > 2.0f + 0.02f * fabsf(b->m[i][3])) return 0;
    }
    return 1;
}

/* How the camera moved from sPrev to sCur: what takes a still object's
 * position matrix in the one to its matrix in the other (the view matrix
 * of sCur times the inverse of sPrev's). Found from draws of the 3D scene
 * with a key only one draw has, the transform most of them agree on (the
 * ground, trees and the like stand still; Pokémon don't). Identity if no
 * clear answer. */
static Mtx34 sCamera;
static int sCameraValid;

static void camera_motion(const int* uniqueMatch) {
    enum { SAMPLES = 48 };
    Mtx34 d[SAMPLES];
    int n = 0, i, k, best = -1, bestVotes = 0, step;
    int total = 0;
    for (i = 0; i < sCur->count; i++) total += uniqueMatch[i] >= 0;
    step = total / SAMPLES + 1;
    for (i = 0, k = 0; i < sCur->count && n < SAMPLES; i++) {
        const uint32_t *cur, *prev;
        Mtx34 c, p, pi;
        if (uniqueMatch[i] < 0 || k++ % step) continue;
        cur = sCur->xf + sCur->ops[i].xf;
        prev = sPrev->xf + sPrev->ops[uniqueMatch[i]].xf;
        if (!perspective(cur) || !perspective(prev)) continue;
        if (!load_mtx(&c, pos_matrix(cur)) || !load_mtx(&p, pos_matrix(prev)) || !mtx_inverse(&pi, &p)) continue;
        mtx_mul(&d[n++], &c, &pi);
    }
    for (i = 0; i < n; i++) {
        int votes = 0;
        for (k = 0; k < n; k++) votes += mtx_close(&d[i], &d[k]);
        if (votes > bestVotes) {
            bestVotes = votes;
            best = i;
        }
    }
    sCameraValid = best >= 0 && bestVotes >= 3 && bestVotes * 3 >= n;
    if (sCameraValid) sCamera = d[best];
}

static float wordf(uint32_t w);

/* How far apart a draw of sPrev, moved as the camera moved, and one of
 * sCur are: their position matrices' translations. */
static float camera_distance(const uint32_t* prev, const uint32_t* cur) {
    const uint32_t *p = pos_matrix(prev), *c = pos_matrix(cur);
    float t[3], d = 0;
    int i;
    t[0] = wordf(p[3]);
    t[1] = wordf(p[7]);
    t[2] = wordf(p[11]);
    for (i = 0; i < 3; i++) {
        const float* r = sCamera.m[i];
        d += fabsf(r[0] * t[0] + r[1] * t[1] + r[2] * t[2] + r[3] - wordf(c[4 * i + 3]));
    }
    return d == d ? d : 1e30f;
}

static float draw_distance(const uint32_t* prev, const uint32_t* cur, float limit) {
    if (sCameraValid && perspective(prev) && perspective(cur)) return camera_distance(prev, cur);
    return matrix_distance(prev, cur, limit);
}

/* Pairs each draw of sCur with the same draw of sPrev: of the first few
 * not yet taken with the same key, the one nearest, once moved as the
 * camera moved. (Draws of one key can change order between frames: the
 * save files screen's panels and the prism piece slots on them do, 28 of
 * those, and taken in order they swapped places. Nearest without the
 * camera's motion, a row of crates matched each crate with its neighbour
 * whenever the camera moved further in a frame than they stand apart.) */
static void match_draws(void) {
    enum { CANDIDATES = 48 };
    enum { SLOTS = 8192 };
    static uint64_t keys[SLOTS];
    static uint8_t used[SLOTS];
    static int head[SLOTS], *next, *unique;
    static int nextCap, uniqueCap;
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
    /* Draws whose key only one draw of sPrev has, for the camera's motion. */
    if (sCur->count > uniqueCap) {
        uniqueCap = sCur->count * 2;
        unique = (int*)realloc(unique, (size_t)uniqueCap * sizeof(int));
    }
    for (i = 0; i < sCur->count; i++) {
        uint64_t k = sCur->ops[i].st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        unique[i] = -1;
        if (sCur->ops[i].op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (used[slot] && head[slot] >= 0 && next[head[slot]] < 0) unique[i] = head[slot];
    }
    camera_motion(unique);
    for (i = 0; i < sCur->count; i++) {
        RecOp* r = &sCur->ops[i];
        uint64_t k = r->st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        r->match = -1;
        if (r->op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (used[slot] && head[slot] >= 0) {
            const uint32_t* xf = sCur->xf + r->xf;
            int* link = &head[slot];
            int* best = link;
            float bestD = next[*link] < 0 ? 0 : draw_distance(sPrev->xf + sPrev->ops[*link].xf, xf, 1e30f);
            int n;
            for (n = 1, link = &next[*link]; n < CANDIDATES && *link >= 0 && bestD > 0; n++, link = &next[*link]) {
                float d = draw_distance(sPrev->xf + sPrev->ops[*link].xf, xf, bestD);
                if (d < bestD) {
                    bestD = d;
                    best = link;
                }
            }
            r->match = *best;
            *best = next[*best];
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
            sDev->draw_recorded(&r->st, cur, r->a[0], r->a[2], r->a[3]);
            break;
        }
        case OP_CLEAR: sDev->clear(r->a[0], r->a[1], r->a[2], (uint32_t)r->a[3], (uint32_t)r->a[4]); break;
        case OP_COPY: sDev->copy(r->a[0], r->a[1], r->a[2], r->a[3], r->a[4], r->a[5], r->a[6], r->a[7]); break;
        default: show(r->a[0], r->a[1], r->a[2], r->a[3]); break;
        }
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
    memcpy(sFreeLater, sPendingFree, (size_t)sPendingFreeCount * sizeof(uint32_t));
    sFreeLaterCount = sPendingFreeCount;
    sPendingFreeCount = 0;
}

/* ------------------------------------------------------------------------ */
/* Pacing */

static uint64_t frame_due(void) { return sRecording && sHaveFrame ? sNextOut : 0; }

/* Frames shown per second: the setting, or the display's refresh rate if
 * lower (more would never be seen). */
static double output_rate(void) {
    static int ignore = -1;
    double hz;
    if (ignore < 0) ignore = getenv("RECOMP_IGNORE_REFRESH") != NULL;
    hz = ignore ? 0 : win_display_hz();
    return hz >= 30 && hz < sOutputRate ? hz : (double)sOutputRate;
}

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

void gxb_set_output_rate(int fps) { sOutputRate = fps < 30 ? 30 : fps > 360 ? 360 : fps; }

/* ------------------------------------------------------------------------ */
/* Entry points */

void gxb_init(void) {
    const char* v = getenv("RECOMP_GPU");
    win_create();
    if (v && *v) sDev = strcmp(v, "vulkan") == 0 ? &g_vk_device : &g_gl_device;
    else sDev = g_settings.renderer == RENDERER_VULKAN ? &g_vk_device : &g_gl_device;
    if (!sDev->init()) {
        const GXDevice* other = sDev == &g_gl_device ? &g_vk_device : &g_gl_device;
        fprintf(stderr, "gpu: no %s, trying %s\n", sDev->name, other->name);
        sDev = other;
        if (!sDev->init()) {
            fprintf(stderr, "gpu: no %s either\n", sDev->name);
            exit(1);
        }
    }
    g_host_frame_due = frame_due;
    g_host_frame_render = frame_render;
}

int gx_wanted_scale(int h) {
    const char* v = getenv("RECOMP_GX_SCALE");
    int s = v && atoi(v) > 0 ? atoi(v) : g_settings.scale;
    if (!s) s = (int)ceil(h / 456.0 - 0.25);
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

int display_scale(void) { return sDev->scale(); }
int display_msaa(void) { return sDev->msaa(); }
int display_renderer(void) { return sDev == &g_vk_device ? RENDERER_VULKAN : RENDERER_OPENGL; }

int gxb_recording(void) {
    int want = sOutputRate > 60;
    if (sRecording && !want && sHaveFrame) {
        /* Stopping: show the newest frame as it is. */
        GL_ENTER();
        render_frame(1.0f);
        free_textures(sFreeLater, &sFreeLaterCount);
        GL_LEAVE(GLK_PRESENT);
    }
    if (want != sRecording) {
        sRecording = want;
        sHaveFrame = 0;
        sRec->count = sRec->xf_words = 0;
        sCur->count = sCur->xf_words = 0;
        sPrev->count = sPrev->xf_words = 0;
    }
    return sRecording;
}

void gxb_frame_data(const GXVertex* v, int vertices, const uint32_t* index, int count) {
    GL_ENTER();
    sDev->frame_data(v, vertices, index, count);
    GL_LEAVE(GLK_DRAW);
}

uint32_t gxb_texture(uint64_t key, int width, int height, int levels, const uint8_t* rgba, uint32_t mode0) {
    uint32_t r;
    GL_ENTER();
    (void)key;
    r = sDev->texture(width, height, levels, rgba, mode0);
    GL_LEAVE(GLK_TEXTURE);
    return r;
}

void gxb_texture_update(uint32_t handle, int width, int height, int levels, const uint8_t* rgba) {
    GL_ENTER();
    sDev->texture_update(handle, width, height, levels, rgba);
    GL_LEAVE(GLK_TEXTURE_UPDATE);
}

void gxb_texture_free(uint32_t handle) {
    if (sPendingFreeCount == MAX_PENDING_FREES) {
        GL_ENTER();
        free_textures(sPendingFree, &sPendingFreeCount);
        GL_LEAVE(GLK_TEXTURE);
    }
    sPendingFree[sPendingFreeCount++] = handle;
}

void gxb_draw_triangles(const GXDrawState* st, const GXVertex* v, int first_vertex, int vertices, const uint32_t* index,
                        int first_index, int count) {
    if (sRecording) {
        rec_draw(st, first_vertex, vertices, first_index, count);
        return;
    }
    GL_ENTER();
    sDev->draw(st, v, first_vertex, vertices, index, first_index, count);
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
    sDev->clear(color, alpha, depth, rgba, z);
    GL_LEAVE(GLK_CLEAR);
}

void gx_register_copy(uint32_t addr, uint32_t handle, int width, int height, int fmt);

void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha) {
    int t;
    GL_ENTER();
    t = sDev->copy_target(addr, w, h);
    if (t >= 0 && !sRecording) sDev->copy(t, x, y, w, h, fmt, intensity, efb_alpha);
    GL_LEAVE(GLK_COPY);
    if (t < 0) return;
    if (sRecording) {
        /* The texture is made now, for the draws that follow to name;
         * the copy into it is drawn with the frame. */
        RecOp* r = rec_op(OP_COPY);
        r->a[0] = t;
        r->a[1] = x;
        r->a[2] = y;
        r->a[3] = w;
        r->a[4] = h;
        r->a[5] = fmt;
        r->a[6] = intensity;
        r->a[7] = efb_alpha;
    }
    gx_register_copy(addr, sDev->copy_texture(t), w, h, fmt);
}

void gxb_present(int x, int y, int cw, int ch) {
    sGameFrames++;
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
    show(x, y, cw, ch);
    free_textures(sPendingFree, &sPendingFreeCount);
    GL_LEAVE(GLK_PRESENT);
}
