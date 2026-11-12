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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
/* The performance counter, from the monotonic clock in nanoseconds. */
typedef long long LONGLONG;
typedef struct {
    LONGLONG QuadPart;
} LARGE_INTEGER;
static void QueryPerformanceCounter(LARGE_INTEGER* t) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t->QuadPart = (LONGLONG)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static void QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000; }
#endif

#include "game.h"
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
        snprintf(title, sizeof(title), GAME_NAME " (recompiled) - %.2f fps, slowest frame %.1f ms, busy %.0f%%", fps,
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

int gxb_texture_generation(void) { return g_settings.aniso | g_settings.texture_pack << 8; }

/* Textures are deleted after the frame is presented: a draw earlier in the
 * frame may still refer to one, and a device may hand a deleted handle
 * straight to the next texture made. Freed while a frame was recorded,
 * they are still drawn by its replays until two more frames are complete
 * (it is replayed as the frame before, render_frame). */
#define MAX_PENDING_FREES 1024
static uint32_t sPendingFree[MAX_PENDING_FREES], sFreeLater[2][MAX_PENDING_FREES];
static int sPendingFreeCount, sFreeLaterCount[2];

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
    int a[8];         /* the other ops' arguments; OP_DRAW: first vertex, vertices, first index, count,
                       * and the row of the matrix that places it (pos_row) */
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
/* sPrev can be drawn: the device has its vertices as the frame before's
 * (each of the two handed over once, at its end: not so after
 * gxb_copy_sync). */
static int sPrevDrawable, sFrameDataCalls, sPrevDataCalls;
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

/* The matrix index of each vertex of the arena, as written (gxb_arena_write):
 * the arena itself is on the GPU. */
static uint8_t* sArenaMtx;
static int sArenaMtxCap;

/* The row of XF memory where the matrix that places a draw starts: MATINDEX_A's,
 * or with a matrix per vertex (a skinned model's), its first vertex's. Of a
 * model drawn piece by piece, MATINDEX_A is whatever was left there: nuts of a
 * row, alike, were paired across holes by it and blended half way, a nut
 * showing between two. */
static int pos_row(const GXDrawState* st, const GXVertex* v, int vertices, const uint32_t* index, int first_index,
                   int count) {
    if ((st->mtx_per_vertex & 1) && count > 0) {
        uint32_t i = index[first_index];
        if (v) return (int)i < vertices ? v[i].mtx[0] : 0;
        if ((int)i < sArenaMtxCap) return sArenaMtx[i];
    }
    return (int)(st->xf[GX_XF_MEM_WORDS + 0x18] & 0x3F);
}

/* Whether the draw lays a copy the frame made of its own picture where its
 * vertices' texture coordinates put it: a layer of the picture, a blur's
 * (the cave's: Pikachu alone, blurred and laid back over him). The copy
 * is of the frame as drawn, in between too; the draw moved half way, it
 * laid it beside: a second Pikachu, the layer's edge a band at the side.
 * A copy projected from the vertices' positions (a shadow, the floor's)
 * follows them, and is blended with them. */
static int lays_copy(const GXDrawState* st) {
    int s, i;
    for (s = 0; s < st->num_stages; s++) {
        const GXTevStage* g = &st->stage[s];
        uint32_t tg;
        if (!g->tex_enable || g->texmap > 7 || g->texcoord > 7) continue;
        tg = st->xf[GX_XF_MEM_WORDS + 0x40 + g->texcoord];
        if (((tg >> 7) & 0x1F) == 0) continue; /* from the position */
        for (i = sRec->count - 2; i >= 0; i--) {
            if (sRec->ops[i].op == OP_COPY && st->texture[g->texmap] == sDev->copy_texture(sRec->ops[i].a[0])) return 1;
        }
    }
    return 0;
}

static void rec_draw(const GXDrawState* st, int first_vertex, int vertices, int first_index, int count, int row) {
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
    r->a[4] = row;
    r->a[5] = lays_copy(st);
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

/* A draw's position matrix (pos_row's), 3x4, in its XF state. */
static const uint32_t* pos_matrix(const Frame* f, const RecOp* r) {
    return f->xf + r->xf + 4 * (r->a[4] & 0x3F) % 0x100;
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
        if (!load_mtx(&c, pos_matrix(sCur, &sCur->ops[i])) || !load_mtx(&p, pos_matrix(sPrev, &sPrev->ops[uniqueMatch[i]])) ||
            !mtx_inverse(&pi, &p))
            continue;
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
static float camera_distance(const RecOp* prev, const RecOp* cur) {
    const uint32_t *p = pos_matrix(sPrev, prev), *c = pos_matrix(sCur, cur);
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

/* How far apart two draws' position matrices' rotation and scale are (the
 * one of sPrev turned as the camera turned). */
static float shape_distance(const RecOp* prev, const RecOp* cur, int camera) {
    const uint32_t *p = pos_matrix(sPrev, prev), *c = pos_matrix(sCur, cur);
    float d = 0;
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            float was = wordf(p[4 * i + j]);
            if (camera) {
                const float* r = sCamera.m[i];
                was = r[0] * wordf(p[j]) + r[1] * wordf(p[4 + j]) + r[2] * wordf(p[8 + j]);
            }
            d += fabsf(wordf(c[4 * i + j]) - was);
        }
    }
    return d == d ? d : 0;
}

/* Where they are, and of draws in one place, how alike they are: the
 * Attraction Cleared banner and the fading copies of it that trail it,
 * bigger, are one picture (one key) with one middle; paired by where
 * alone, the banner took a copy's size as often as not, its own between
 * them, and jumped from size to size. */
#define SHAPE_WEIGHT 1e-3f

static float draw_distance(const RecOp* prev, const RecOp* cur, float limit) {
    const uint32_t *p = sPrev->xf + prev->xf, *c = sCur->xf + cur->xf;
    int camera = sCameraValid && perspective(p) && perspective(c);
    float d = camera ? camera_distance(prev, cur) : matrix_distance(p, c, limit);
    return d < limit ? d + SHAPE_WEIGHT * shape_distance(prev, cur, camera) : d;
}

/* Whether a match can be one object moving. Of many alike, one going as
 * another comes up elsewhere (the targets of a shooting game) pair up as
 * the nearest: blended, a copy slides across between them. In a game frame
 * nothing crosses this much of its distance from the eye. */
#define FARTHEST_STEP 0.1f

static int same_object(const RecOp* prev, const RecOp* cur) {
    const uint32_t *c = pos_matrix(sCur, cur), *p = pos_matrix(sPrev, prev);
    float x = wordf(c[3]), y = wordf(c[7]), z = wordf(c[11]), d;
    if (prev->a[4] != cur->a[4]) return 0; /* placed by another of its matrices */
    if (prev->a[5] || cur->a[5]) return 0; /* lays a copy of its picture (lays_copy) */
    if (!perspective(sPrev->xf + prev->xf) || !perspective(sCur->xf + cur->xf)) return 1;
    /* (With too few draws to tell the camera's motion by, as it is here.) */
    d = sCameraValid ? camera_distance(prev, cur)
                     : fabsf(x - wordf(p[3])) + fabsf(y - wordf(p[7])) + fabsf(z - wordf(p[11]));
    return d <= FARTHEST_STEP * sqrtf(x * x + y * y + z * z);
}

/* Pairs each draw of sCur with the same draw of sPrev: of the first few
 * not yet taken with the same key, the one nearest, once moved as the
 * camera moved. (Draws of one key can change order between frames: the
 * save files screen's panels and the prism piece slots on them do, 28 of
 * those, and taken in order they swapped places. Nearest without the
 * camera's motion, a row of crates matched each crate with its neighbour
 * whenever the camera moved further in a frame than they stand apart.) */
#define CROSSED 0.5f /* another draw nearer than this much of the match's distance */

/* Takes back a match when another draw of the same key in sCur is plainly
 * nearer the draw of sPrev it was paired with: that one is the same object,
 * this one is new or a piece left over. Taken first come, the leftovers
 * paired with what remained, a neighbour: a nut's shell opening in a row of
 * holes alike (two halves and the nut instead of one shell) put a shell
 * and the nut half way to the next hole for a frame. */
static void unmatch_crossed(void) {
    enum { CANDIDATES = 48, SLOTS = 8192 };
    static uint64_t keys[SLOTS];
    static uint8_t used[SLOTS];
    static int head[SLOTS], *next, nextCap;
    int i;
    memset(used, 0, sizeof(used));
    if (sCur->count > nextCap) {
        nextCap = sCur->count * 2;
        next = (int*)realloc(next, (size_t)nextCap * sizeof(int));
    }
    for (i = sCur->count - 1; i >= 0; i--) {
        uint64_t k = sCur->ops[i].st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        if (sCur->ops[i].op != OP_DRAW) continue;
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
        const RecOp* p;
        float d;
        int j, n;
        if (r->op != OP_DRAW || r->match < 0) continue;
        while (keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (next[head[slot]] < 0) continue; /* the only one */
        p = &sPrev->ops[r->match];
        d = draw_distance(p, r, 1e30f);
        if (d <= 0) continue;
        for (n = 0, j = head[slot]; j >= 0 && n < CANDIDATES; j = next[j], n++) {
            if (j != i && draw_distance(p, &sCur->ops[j], d) < d * CROSSED) {
                r->match = -1;
                break;
            }
        }
    }
}

/* A draw of sCur and one of sPrev with its key, and how far apart. */
typedef struct Pair {
    float d;
    int cur, prev;
} Pair;

static int pair_order(const void* a, const void* b) {
    float x = ((const Pair*)a)->d, y = ((const Pair*)b)->d;
    return x < y ? -1 : x > y;
}

/* Pairs of a key up to which they're all weighed, nearest taken first. */
#define ALL_PAIRS 4096

static void match_draws(void) {
    enum { CANDIDATES = 48 };
    enum { SLOTS = 8192 };
    static uint64_t keys[SLOTS];
    static uint8_t used[SLOTS];
    static int head[SLOTS], curHead[SLOTS], curTail[SLOTS], *next, *curNext, *unique;
    static int nextCap, uniqueCap;
    static uint8_t* taken;
    static Pair pairs[ALL_PAIRS];
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
    /* Of a key a few draws in each frame have (nuts in a row of holes):
     * every pair weighed, the nearest taken first. Each draw in turn taking
     * the nearest left, a nut jumping out of its hole further than the
     * holes stand apart took its neighbour's, and the neighbour its place:
     * blended, a copy of each half way between. */
    taken = (uint8_t*)realloc(taken, (size_t)sPrev->count + 1);
    memset(taken, 0, (size_t)sPrev->count + 1);
    curNext = (int*)realloc(curNext, (size_t)uniqueCap * sizeof(int));
    for (i = 0; i < SLOTS; i++) curHead[i] = -1;
    for (i = 0; i < sCur->count; i++) {
        uint64_t k = sCur->ops[i].st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        sCur->ops[i].match = -1;
        if (sCur->ops[i].op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (!used[slot] || head[slot] < 0) continue;
        curNext[i] = -1;
        if (curHead[slot] < 0) curHead[slot] = i;
        else curNext[curTail[slot]] = i;
        curTail[slot] = i;
    }
    for (i = 0; i < SLOTS; i++) {
        int nc = 0, np = 0, c, p, n = 0, j;
        if (curHead[i] < 0) continue;
        for (c = curHead[i]; c >= 0; c = curNext[c]) nc++;
        for (p = head[i]; p >= 0; p = next[p]) np++;
        if ((nc == 1 && np == 1) || nc * np > ALL_PAIRS) continue; /* below */
        for (c = curHead[i]; c >= 0; c = curNext[c]) {
            for (p = head[i]; p >= 0; p = next[p]) {
                pairs[n].d = draw_distance(&sPrev->ops[p], &sCur->ops[c], 1e30f);
                pairs[n].cur = c;
                pairs[n].prev = p;
                n++;
            }
        }
        qsort(pairs, (size_t)n, sizeof(Pair), pair_order);
        for (j = 0; j < n; j++) {
            RecOp* r = &sCur->ops[pairs[j].cur];
            if (r->match >= 0 || taken[pairs[j].prev]) continue;
            if (!same_object(&sPrev->ops[pairs[j].prev], r)) continue;
            r->match = pairs[j].prev;
            taken[pairs[j].prev] = 1;
        }
        curHead[i] = -2; /* done */
    }
    for (i = 0; i < sCur->count; i++) {
        RecOp* r = &sCur->ops[i];
        uint64_t k = r->st.key;
        unsigned slot = (unsigned)(k ^ (k >> 29)) & (SLOTS - 1);
        if (r->op != OP_DRAW) continue;
        while (used[slot] && keys[slot] != k) slot = (slot + 1) & (SLOTS - 1);
        if (used[slot] && head[slot] >= 0 && curHead[slot] != -2) {
            int* link = &head[slot];
            int* best = link;
            float bestD = next[*link] < 0 ? 0 : draw_distance(&sPrev->ops[*link], r, 1e30f);
            int n;
            for (n = 1, link = &next[*link]; n < CANDIDATES && *link >= 0 && bestD > 0; n++, link = &next[*link]) {
                float d = draw_distance(&sPrev->ops[*link], r, bestD);
                if (d < bestD) {
                    bestD = d;
                    best = link;
                }
            }
            if (same_object(&sPrev->ops[*best], r)) r->match = *best;
            *best = next[*best];
        }
    }
    unmatch_crossed();
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

/* out[from..to) blended t of the way from a to b, where they differ (a word
 * the same in both may not be a float). out is a or b there already. With
 * no branch, it vectorizes: these were 4% of the Thor's game thread at
 * 120 fps. */
static void lerp_words(uint32_t* out, const uint32_t* a, const uint32_t* b, int from, int to, float t) {
    int i;
    for (i = from; i < to; i++) {
        uint32_t l = fword(wordf(a[i]) + (wordf(b[i]) - wordf(a[i])) * t);
        out[i] = a[i] == b[i] ? a[i] : l;
    }
}

/* Matrix m (3x4, 12 words) of out blended from prev's to cur's, if it is
 * the same object's moving a little; out has own's there. */
static void blend_matrix(uint32_t* out, const uint32_t* prev, const uint32_t* cur, int m, float t) {
    const uint32_t *a = prev + m, *b = cur + m;
    float scale = 0, rot = 0, dt = 0, tlen = 0;
    int nan = 0, j;
    if (memcmp(a, b, 48) == 0) return;
    for (j = 0; j < 12; j++) {
        float x = wordf(a[j]), y = wordf(b[j]);
        nan |= !(x == x) | !(y == y);
        if ((j & 3) == 3) {
            dt += (y - x) * (y - x);
            tlen += y * y;
        } else {
            scale = fmaxf(scale, fabsf(y));
            rot = fmaxf(rot, fabsf(y - x));
        }
    }
    if (nan) return; /* not floats */
    /* The same object moving turns and moves a little in a frame;
     * another object (a wrong match, a cut) differs wholesale. */
    if (rot > 0.3f * scale || sqrtf(dt) > 0.5f * sqrtf(tlen) + 200.0f) return;
    lerp_words(out, prev, cur, m, m + 12, t);
}

static void lerp_clipped(uint32_t* out, const uint32_t* prev, const uint32_t* cur, int from, int to, int lo, int hi,
                         float t) {
    if (from < lo) from = lo;
    if (to > hi) to = hi;
    if (from < to) lerp_words(out, prev, cur, from, to, t);
}

/* Words lo..hi of the XF state of a draw blended from prev (the frame
 * before) to cur, the rest as own (one of the two) has it, the draw's own
 * frame's; with the matrices and the projection those words are part of
 * whole. Each word of the result depends on those of its matrix (or
 * projection) alone, so a draw's is the draw before's but where either
 * frame's state changed between the two. */
static void blend_range(uint32_t* out, const uint32_t* prev, const uint32_t* cur, const uint32_t* own, float t, int lo,
                        int hi) {
    const int proj = GX_XF_MEM_WORDS + 0x20;
    int m, j;
    if (lo >= hi) return;
    memcpy(out + lo, own + lo, (size_t)(hi - lo) * 4);
    /* Position and texture matrices, 3x4, at multiples of 12 words. */
    for (m = lo < 0x100 ? lo / 12 * 12 : 0x100; m + 12 <= 0x100 && m < hi; m += 12) {
        memcpy(out + m, own + m, 48);
        blend_matrix(out, prev, cur, m, t);
    }
    lerp_clipped(out, prev, cur, 0x400, 0x460, lo, hi, t); /* normal matrices */
    lerp_clipped(out, prev, cur, 0x500, 0x600, lo, hi, t); /* post-transform matrices */
    for (j = 0; j < 8; j++) lerp_clipped(out, prev, cur, 0x600 + 16 * j + 10, 0x600 + 16 * j + 16, lo, hi, t); /* lights */
    if (lo < proj + 7 && hi > proj) { /* the projection, with its kind (word 6) */
        memcpy(out + proj, own + proj, 6 * 4);
        if (prev[proj + 6] == cur[proj + 6]) lerp_words(out, prev, cur, proj, proj + 6, t);
    }
}

/* blend_range over the 32-word chunks set in chunks (GXDrawState's
 * xf_dirty_chunks), a run of them at a time. */
static void blend_chunks(uint32_t* out, const uint32_t* prev, const uint32_t* cur, const uint32_t* own, float t,
                         uint64_t chunks) {
    int lo = 0, hi;
    while (lo < 64 && (chunks >> lo)) {
        if (!((chunks >> lo) & 1)) {
            lo++;
            continue;
        }
        for (hi = lo; hi < 64 && ((chunks >> hi) & 1); hi++) {}
        blend_range(out, prev, cur, own, t, 32 * lo, 32 * hi < GX_XF_WORDS ? 32 * hi : GX_XF_WORDS);
        lo = hi;
    }
}

/* Draws placed alike are drawn alike. A model drawn twice in a frame, as
 * the cave's sky is (once in the scene, then again on black for the glow,
 * tested against the scene's depth for where it shows), is two draws, and
 * one may be matched with the frame before and the other not: blended,
 * one moved and the other didn't, a hair apart, the depth test failed in
 * bands, and the glow over the sky went dark and striped for the frame.
 * Each draw whose position matrix and projection are as an earlier one's
 * in the frame has its blended ones. */
#define PLACES 1024
typedef struct Place {
    unsigned gen;
    uint32_t own[19], drawn[19];
} Place;
static Place sPlaces[PLACES];
static unsigned sPlaceGen;

static void place_words(uint32_t* out, const uint32_t* xf, int m) {
    memcpy(out, xf + m, 12 * 4);
    memcpy(out + 12, xf + GX_XF_MEM_WORDS + 0x20, 7 * 4);
}

/* drawn, or a copy in placed with the earlier draw's placing; NULL if as it is. */
static const uint32_t* same_place(const GXDrawState* st, const uint32_t* own, const uint32_t* drawn, uint32_t* placed) {
    uint32_t key[19], have[19], h = 2166136261u;
    int m = (int)(own[GX_XF_MEM_WORDS + 0x18] & 63) * 4, i, slot;
    Place* p;
    if (st->mtx_per_vertex & 1 || m + 12 > 0x100) return NULL;
    place_words(key, own, m);
    for (i = 0; i < 19; i++) h = (h ^ key[i]) * 16777619u;
    for (slot = (int)(h & (PLACES - 1));; slot = (slot + 1) & (PLACES - 1)) {
        p = &sPlaces[slot];
        if (p->gen != sPlaceGen) break;
        if (memcmp(p->own, key, sizeof(key)) == 0) {
            place_words(have, drawn, m);
            if (memcmp(have, p->drawn, sizeof(have)) == 0) return NULL;
            if (drawn != placed) memcpy(placed, drawn, GX_XF_WORDS * 4);
            memcpy(placed + m, p->drawn, 12 * 4);
            memcpy(placed + GX_XF_MEM_WORDS + 0x20, p->drawn + 12, 7 * 4);
            return placed;
        }
    }
    p->gen = sPlaceGen;
    memcpy(p->own, key, sizeof(key));
    place_words(p->drawn, drawn, m);
    return NULL;
}

/* For each draw of sPrev, the draw of sCur matched with it; -1 for none. */
static int* sNext;
static int sNextCap;

/* Draws sCur or, the first half of the way, sPrev, their matrices t of the
 * way from sPrev's to sCur's. Each frame's own draws: drawn as sCur, a nut
 * opening in its shell (two halves and the nut in place of the shell) put
 * the nut, new, where it is in sCur, and the pieces that match sPrev's
 * where they were: the nut out of its shell, beside a shell still shut. */
static void render_frame(float t) {
    static uint32_t xf[GX_XF_WORDS];
    int before = t < 0.5f && sPrevDrawable;
    const Frame* f = before ? sPrev : sCur;
    const Frame* g = before ? sCur : sPrev; /* the other frame, blended with */
    if (++sPlaceGen == 0) {
        memset(sPlaces, 0, sizeof(sPlaces));
        sPlaceGen = 1;
    }
    /* The last draw blended into xf, of f and of g; -1 for none. Two
     * draws in a row, of both frames, differ only in their XF dirty
     * chunks (the decoder's words changed since its draw before), so
     * only those are blended again: blending all of each draw's state,
     * two 7 KB snapshots, was 9% of the Thor's game thread at 120 fps
     * (now 2%). */
    int last = -1, lastOther = -1;
    int i;
    for (i = 0; i < f->count; i++) {
        const RecOp* r = &f->ops[i];
        switch (r->op) {
        case OP_DRAW: {
            const uint32_t* own = f->xf + r->xf;
            const uint32_t* drawn = own;
            int o = before ? sNext[i] : t < 1.0f ? r->match : -1;
            if (o >= 0) {
                const uint32_t* other = g->xf + g->ops[o].xf;
                const uint32_t* prev = before ? own : other;
                const uint32_t* cur = before ? other : own;
                int k = lastOther + 1;
                while (k < o && g->ops[k].op != OP_DRAW) k++;
                if (last < 0 || (o != lastOther && k != o) || o < lastOther) {
                    blend_range(xf, prev, cur, own, t, 0, GX_XF_WORDS);
                } else {
                    blend_chunks(xf, prev, cur, own, t, r->st.xf_dirty_chunks | (o != lastOther ? g->ops[o].st.xf_dirty_chunks : 0));
                }
                last = i;
                lastOther = o;
                drawn = xf;
            } else {
                last = -1;
            }
            {
                static uint32_t placed[GX_XF_WORDS];
                const uint32_t* moved = same_place(&r->st, own, drawn, drawn == xf ? xf : placed);
                if (moved) {
                    drawn = moved;
                    last = -1; /* xf isn't the blend the next draw would build on */
                }
            }
            if (drawn != own && r->st.bounds_ok &&
                (r->st.bounds_mtx[0] > r->st.bounds_mtx[1] ||
                 memcmp(drawn + 4 * r->st.bounds_mtx[0], own + 4 * r->st.bounds_mtx[0],
                        (size_t)(4 * (r->st.bounds_mtx[1] - r->st.bounds_mtx[0]) + 12) * 4) != 0 ||
                 memcmp(drawn + GX_XF_MEM_WORDS + 0x20, own + GX_XF_MEM_WORDS + 0x20, 7 * 4) != 0)) {
                /* Its bounds are where its own frame put it, and it moved:
                 * its position matrices or the projection. */
                GXDrawState st = r->st;
                st.bounds_ok = 0;
                sDev->draw_recorded(&st, drawn, r->a[0], r->a[2], r->a[3], before);
            } else {
                sDev->draw_recorded(&r->st, drawn, r->a[0], r->a[2], r->a[3], before);
            }
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
    int i;
    sPrev = sCur;
    sCur = sRec;
    sRec = f;
    sRec->count = sRec->xf_words = 0;
    match_draws();
    if (sPrev->count > sNextCap) {
        sNextCap = sPrev->count * 2;
        sNext = (int*)realloc(sNext, (size_t)sNextCap * sizeof(int));
    }
    for (i = 0; i < sPrev->count; i++) sNext[i] = -1;
    for (i = 0; i < sCur->count; i++) {
        if (sCur->ops[i].op == OP_DRAW && sCur->ops[i].match >= 0) sNext[sCur->ops[i].match] = i;
    }
    sPrevDrawable = sHaveFrame && sFrameDataCalls == 1 && sPrevDataCalls == 1;
    sPrevDataCalls = sFrameDataCalls;
    sFrameDataCalls = 0;
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
    free_textures(sFreeLater[1], &sFreeLaterCount[1]);
    memcpy(sFreeLater[1], sFreeLater[0], (size_t)sFreeLaterCount[0] * sizeof(uint32_t));
    sFreeLaterCount[1] = sFreeLaterCount[0];
    memcpy(sFreeLater[0], sPendingFree, (size_t)sPendingFreeCount * sizeof(uint32_t));
    sFreeLaterCount[0] = sPendingFreeCount;
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
#ifdef __ANDROID__
    /* Vulkan alone: there is no OpenGL ES device. */
    (void)v;
    sDev = &g_vk_device;
    if (!sDev->init()) {
        fprintf(stderr, "gpu: no Vulkan 1.3 with what the game needs\n");
        exit(1);
    }
#else
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
#endif
    g_host_frame_due = frame_due;
    g_host_frame_render = frame_render;
}

/* ------------------------------------------------------------------------ */
/* The canvas */

int hle_widescreen(void); /* hle_nand.c */

void gx_canvas(int* mx, int* my) {
    /* The picture is 16:9 in the game's 16:9 mode; its 4:3 mode has none. */
    double pa = win_pixel_aspect(), want;
    *mx = *my = 0;
    if (!hle_widescreen()) return;
    if (g_settings.aspect == ASPECT_16_10) want = 16.0 / 10.0;
    else if (g_settings.aspect == ASPECT_21_9) want = 64.0 / 27.0; /* 2560 x 1080 */
    else return;
    if (GX_PICTURE_H * want > EFB_W * pa) *mx = (int)floor((GX_PICTURE_H * want / pa - EFB_W) / 2 + 0.5);
    else *my = (int)floor((EFB_W * pa / want - GX_PICTURE_H) / 2 + 0.5);
}

int gx_canvas_whole(float x, float y, float w, float h) {
    if (w < 0) {
        x += w;
        w = -w;
    }
    if (h < 0) {
        y += h;
        h = -h;
    }
    return x <= 1 && y <= 1 && x + w >= EFB_W - 1 && y + h >= GX_PICTURE_H - 1;
}

void gx_canvas_draw(const GXDrawState* st, int mx, int my, float vp[4], int sc[4], float k[2]) {
    vp[0] = st->viewport[0] + mx;
    vp[1] = st->viewport[1] + my;
    vp[2] = st->viewport[2];
    vp[3] = st->viewport[3];
    k[0] = k[1] = 1;
    if (st->canvas) {
        /* The viewport grown about its middle by the canvas's size over
         * the picture's; a 3D draw's clip space shrunk as much, so what
         * was in the picture stays where it was. */
        float gx = (float)(EFB_W + 2 * mx) / EFB_W, gy = (float)(GX_PICTURE_H + 2 * my) / GX_PICTURE_H;
        vp[0] -= vp[2] * (gx - 1) / 2;
        vp[2] *= gx;
        vp[1] -= vp[3] * (gy - 1) / 2;
        vp[3] *= gy;
        if (st->canvas == 1) {
            k[0] = 1 / gx;
            k[1] = 1 / gy;
        }
    }
    if ((mx || my) && gx_canvas_whole((float)st->scissor[0], (float)st->scissor[1],
                                      (float)(st->scissor[2] - st->scissor[0]), (float)(st->scissor[3] - st->scissor[1]))) {
        if (st->canvas) {
            sc[0] = sc[1] = 0;
            sc[2] = st->scissor[2] + 2 * mx;
            sc[3] = st->scissor[3] + 2 * my;
        } else {
            /* What stays in the picture is cut to it: what's drawn past
             * its edges (a Wii shows none of it) would show on the
             * margins. */
            sc[0] = mx;
            sc[1] = my;
            sc[2] = EFB_W + mx;
            sc[3] = GX_PICTURE_H + my;
        }
    } else {
        sc[0] = st->scissor[0] + mx;
        sc[1] = st->scissor[1] + my;
        sc[2] = st->scissor[2] + mx;
        sc[3] = st->scissor[3] + my;
    }
    {
        int dy = gx_minimap_dy(st);
        vp[1] += dy;
        sc[1] += dy;
        sc[3] += dy;
    }
}

int gx_minimap_dy(const GXDrawState* st) {
    return st->screen == 1 && input_touch_shown() ? 24 - (int)kGxMinimapRect[1] : 0;
}

void gx_canvas_rect(int mx, int my, int* x, int* y, int* w, int* h) {
    if ((mx || my) && gx_canvas_whole((float)*x, (float)*y, (float)*w, (float)*h)) {
        *w += *x + 2 * mx;
        *h += *y + 2 * my;
        *x = *y = 0;
    } else {
        *x += mx;
        *y += my;
    }
}

int gx_wanted_scale(int h) {
    const char* v = getenv("RECOMP_GX_SCALE");
    int s = v && atoi(v) > 0 ? atoi(v) : g_settings.scale;
    if (!s) {
        s = (int)ceil(h / 456.0 - 0.25);
#ifdef __ANDROID__
        /* A phone's screen is small enough that more doesn't show, and
         * its GPU and battery pay for every pixel. */
        if (s > 2) s = 2;
#endif
    }
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

int display_scale(void) { return sDev->scale(); }
int display_msaa(void) { return sDev->msaa(); }
int display_renderer(void) { return sDev == &g_vk_device ? RENDERER_VULKAN : RENDERER_OPENGL; }

int gxb_recording(void) {
    /* Only when more than 60 frames a second would be shown: on a 60 Hz
     * display (an Android device's refresh rate capped in its settings)
     * the setting's 120 is 60, drawn as at 60. */
    int want = output_rate() > 60.5;
    if (sRecording && !want && sHaveFrame) {
        /* Stopping: show the newest frame as it is. */
        GL_ENTER();
        render_frame(1.0f);
        free_textures(sFreeLater[0], &sFreeLaterCount[0]);
        free_textures(sFreeLater[1], &sFreeLaterCount[1]);
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
    sFrameDataCalls++;
    GL_ENTER();
    sDev->frame_data(v, vertices, index, count);
    GL_LEAVE(GLK_DRAW);
}

int gxb_arena(void) { return sDev->arena_alloc != NULL; }

int gxb_arena_alloc(int n) {
    int r;
    GL_ENTER();
    r = sDev->arena_alloc(n);
    GL_LEAVE(GLK_DRAW);
    return r;
}

void gxb_arena_write(int first, const GXVertex* v, int n) {
    int i;
    if (first + n > sArenaMtxCap) {
        int cap = (first + n) * 2;
        uint8_t* grown = (uint8_t*)realloc(sArenaMtx, (size_t)cap);
        if (grown) {
            memset(grown + sArenaMtxCap, 0, (size_t)(cap - sArenaMtxCap));
            sArenaMtx = grown;
            sArenaMtxCap = cap;
        }
    }
    for (i = 0; i < n && first + i < sArenaMtxCap; i++) sArenaMtx[first + i] = v[i].mtx[0];
    GL_ENTER();
    sDev->arena_write(first, v, n);
    GL_LEAVE(GLK_DRAW);
}

void gxb_arena_free(int first, int n) {
    GL_ENTER();
    sDev->arena_free(first, n);
    GL_LEAVE(GLK_DRAW);
}

uint32_t gxb_texture(uint64_t key, int pix, int width, int height, int levels, const uint8_t* pixels, uint32_t mode0) {
    uint32_t r;
    GL_ENTER();
    (void)key;
    r = sDev->texture(pix, width, height, levels, pixels, mode0);
    GL_LEAVE(GLK_TEXTURE);
    return r;
}

void gxb_texture_update(uint32_t handle, int pix, int width, int height, int levels, const uint8_t* pixels) {
    GL_ENTER();
    sDev->texture_update(handle, pix, width, height, levels, pixels);
    GL_LEAVE(GLK_TEXTURE_UPDATE);
}

unsigned gxb_texture_pixels(void) { return sDev->texture_pixels ? sDev->texture_pixels() : 0; }

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
        rec_draw(st, first_vertex, vertices, first_index, count, pos_row(st, v, first_vertex + vertices, index, first_index, count));
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

int gx_register_copy(uint32_t addr, uint32_t handle, int width, int height, int fmt);
void gx_copy_write_back(uint32_t addr, int fmt, int intensity, int w, int h, int stride, const uint8_t* rgba, int force);

/* Copies to put in guest memory too (gx_register_copy says which), once
 * the frame after has been drawn: read back from the GPU, at the EFB's own
 * size. Made again in that frame, it's an effect starting (PokePark 1's
 * blur's eight copies held a frame up by 45 ms), so it's dropped. */
typedef struct WriteBack {
    uint32_t addr;
    int target, w, h, fmt, intensity, stride;
    int waited;
    int mx, my; /* the canvas's margins, if the copy was of the whole picture: then it has them too */
} WriteBack;
static WriteBack sWriteBacks[8];
static int sWriteBackCount;
/* Every copy's latest, for gxb_copy_sync. */
static WriteBack sCopiesMade[32];
static int sCopiesMadeCount;

static void write_back(const WriteBack* b, int force) {
    int tw, th, x, y;
    uint8_t* full = sDev->copy_read ? sDev->copy_read(b->target, &tw, &th) : NULL;
    uint8_t* px = full ? (uint8_t*)malloc((size_t)b->w * b->h * 4) : NULL;
    if (px) {
        /* Each EFB pixel: the average of those it was drawn as, in the
         * picture's part of a copy grown to the canvas (photos stay the
         * picture's shape). */
        int ox = b->mx * tw / (b->w + 2 * b->mx), oy = b->my * th / (b->h + 2 * b->my);
        int pw = tw - 2 * ox, ph = th - 2 * oy;
        for (y = 0; y < b->h; y++) {
            int y0 = oy + y * ph / b->h, y1 = oy + (y + 1) * ph / b->h;
            for (x = 0; x < b->w; x++) {
                int x0 = ox + x * pw / b->w, x1 = ox + (x + 1) * pw / b->w, sx, sy, k, n = 0;
                unsigned sum[4] = {0, 0, 0, 0};
                if (y1 <= y0) y1 = y0 + 1;
                if (x1 <= x0) x1 = x0 + 1;
                for (sy = y0; sy < y1 && sy < th; sy++) {
                    for (sx = x0; sx < x1 && sx < tw; sx++, n++) {
                        for (k = 0; k < 4; k++) sum[k] += full[((size_t)sy * tw + sx) * 4 + k];
                    }
                }
                for (k = 0; k < 4; k++) px[((size_t)y * b->w + x) * 4 + k] = (uint8_t)(n ? (sum[k] + n / 2) / n : 0);
            }
        }
        gx_copy_write_back(b->addr, b->fmt, b->intensity, b->w, b->h, b->stride, px, force);
    }
    free(px);
    free(full);
}

static void write_back_copies(void) {
    int i, kept = 0;
    for (i = 0; i < sWriteBackCount; i++) {
        if (sWriteBacks[i].waited) {
            write_back(&sWriteBacks[i], 0);
        } else {
            sWriteBacks[i].waited = 1;
            sWriteBacks[kept++] = sWriteBacks[i];
        }
    }
    sWriteBackCount = kept;
}

/* g_gx_copy_sync: the copy holding addr, made every frame and so never
 * written back (the game's photos: its JPEG encoder reads the screen's
 * copy), put in memory now, as the frames submitted so far left it. */
void gxb_copy_sync(uint32_t addr) {
    const WriteBack* in = NULL;
    int i;
    for (i = 0; i < sCopiesMadeCount; i++) {
        const WriteBack* b = &sCopiesMade[i];
        /* Its rows of blocks: 8 lines for 4 bits a pixel, else 4. */
        uint32_t bytes = (uint32_t)(b->stride * ((b->h + 7) / 8 * 8)) / ((b->fmt & 15) == 0 ? 8 : 4);
        if (b->addr == addr) {
            in = b;
            break;
        }
        if (!in && addr - b->addr < bytes) in = b;
    }
    if (!in) return;
    if (sRecording) {
        /* The copy is drawn with the frame, not yet ended: what's been
         * recorded of it is drawn now. Nothing more of the frame before is
         * shown until this one ends (the device has this one's vertices). */
        gx_frame_data_so_far();
        GL_ENTER();
        for (i = 0; i < sRec->count; i++) {
            const RecOp* r = &sRec->ops[i];
            if (r->op == OP_DRAW) sDev->draw_recorded(&r->st, sRec->xf + r->xf, r->a[0], r->a[2], r->a[3], 0);
            else if (r->op == OP_CLEAR) sDev->clear(r->a[0], r->a[1], r->a[2], (uint32_t)r->a[3], (uint32_t)r->a[4]);
            else if (r->op == OP_COPY) sDev->copy(r->a[0], r->a[1], r->a[2], r->a[3], r->a[4], r->a[5], r->a[6], r->a[7]);
        }
        GL_LEAVE(GLK_DRAW);
        sHaveFrame = 0;
    }
    GL_ENTER();
    write_back(in, 1);
    GL_LEAVE(GLK_COPY);
}

void gxb_copy_to_texture(uint32_t addr, int x, int y, int w, int h, int fmt, int intensity, int efb_alpha, int stride) {
    WriteBack made;
    int t, i;
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
    made.addr = addr;
    made.waited = 0;
    made.target = t;
    made.w = w;
    made.h = h;
    made.fmt = fmt;
    made.intensity = intensity;
    made.stride = stride;
    made.mx = made.my = 0;
    if (gx_canvas_whole((float)x, (float)y, (float)w, (float)h)) gx_canvas(&made.mx, &made.my);
    for (i = 0; i < sCopiesMadeCount && sCopiesMade[i].addr != addr; i++) {}
    if (i == sCopiesMadeCount) {
        if (sCopiesMadeCount < 32) sCopiesMadeCount++;
        else i = (int)(addr >> 5) & 31;
    }
    sCopiesMade[i] = made;
    if (!gx_register_copy(addr, sDev->copy_texture(t), w, h, fmt)) {
        for (i = 0; i < sWriteBackCount; i++) {
            if (sWriteBacks[i].addr == addr && sWriteBacks[i].waited) {
                sWriteBacks[i] = sWriteBacks[--sWriteBackCount];
                break;
            }
        }
        return;
    }
    for (i = 0; i < sWriteBackCount && sWriteBacks[i].addr != addr; i++) {}
    if (i == sWriteBackCount) {
        if (sWriteBackCount == 8) return;
        sWriteBackCount++;
    }
    sWriteBacks[i] = made;
}

void gxb_present(int x, int y, int cw, int ch) {
    sGameFrames++;
#ifdef __ANDROID__
    {
        void android_frame_done(void); /* window_android.c: the CPU's clocks kept up */
        android_frame_done();
    }
#endif
    if (sRecording) {
        RecOp* r = rec_op(OP_PRESENT);
        r->a[0] = x;
        r->a[1] = y;
        r->a[2] = cw;
        r->a[3] = ch;
        GL_ENTER();
        frame_end();
        if (sWriteBackCount) write_back_copies();
        GL_LEAVE(GLK_PRESENT);
        return;
    }
    GL_ENTER();
    show(x, y, cw, ch);
    free_textures(sPendingFree, &sPendingFreeCount);
    if (sWriteBackCount) write_back_copies();
    GL_LEAVE(GLK_PRESENT);
}
