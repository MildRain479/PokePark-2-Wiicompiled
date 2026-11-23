/* Music, sound effects and voices volumes (the Audio settings).
 *
 * The game's sound is CRI ADX2 (Sound/nile_sound.acf, the cues in .acb
 * files). Its ACF has categories with volumes of their own (PK_VOICE 0,
 * BGM 1, PC_VOICE 2, PK_ACT 3, PC_ACT 4, ENV 5, ALWAYS_SE 6, AT_SE 7,
 * JGL 8), but most cues are in none: the title's music and most of the
 * parks' have no category command. So the volumes are set on the players
 * instead. Each cue is sorted when its ACB loads: by its category if it has
 * one (BGM and JGL music, PK_VOICE and PC_VOICE voices, the rest effects),
 * else by its ACB's name (BGM or STAFF_ROLL in it music, VOICE voices, the
 * rest effects). A player takes its cue's group when its cue is set, and
 * the volumes the game sets on it are scaled by that group's setting; when
 * a setting changes, every player's volume is set again (the game itself
 * changes a playing sound's volume this way, without anything after).
 *
 * The CRI functions (no symbols; found by what they do and the game's
 * calls of them):
 *   fn_80393A64, fn_80393C60  criAtomExAcb_LoadAcbData (r3 the ACB's data,
 *                             r4 its size; returns the ACB's handle). The
 *                             data is no file's read, so the ACB is named
 *                             by its size: the disc's .acb of that size.
 *   fn_80399324  criAtomExPlayer_SetCueId (r3 player, r4 ACB, r5 cue id)
 *   fn_8039A6D4  criAtomExPlayer_ResetParameters (r3 player)
 *   fn_8039A730  criAtomExPlayer_SetVolume (r3 player, f1 volume)
 *
 * RECOMP_SOUND_LOG=1 prints the ACBs as they are sorted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"

enum { MUSIC, EFFECTS, VOICES, GROUPS, NO_GROUP = -1 };
static float sPercent[GROUPS] = {100, 100, 100};
static volatile int sDirty;

static int log_on(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("RECOMP_SOUND_LOG");
        on = e && *e && *e != '0';
    }
    return on;
}

/* ---- The ACB's tables: CRI @UTF, in guest memory. ---- */

typedef struct {
    uint32_t b, rows, strs, data, cols;
    int ncol, rowlen, nrows;
} Utf;

static int utf_open(Utf* u, uint32_t at) {
    if (!at || LD32(at) != 0x40555446u) return 0; /* "@UTF" */
    u->b = at + 8;
    u->rows = u->b + LD32(u->b);
    u->strs = u->b + LD32(u->b + 4);
    u->data = u->b + LD32(u->b + 8);
    u->ncol = LD16(u->b + 16);
    u->rowlen = LD16(u->b + 18);
    u->nrows = (int)LD32(u->b + 20);
    u->cols = u->b + 24;
    return u->ncol > 0 && u->ncol < 256 && u->nrows >= 0 && u->nrows < 65536;
}

static int guest_str_is(uint32_t a, const char* s) {
    for (;; a++, s++) {
        uint8_t ch = LD8(a);
        if (ch != (uint8_t)*s) return 0;
        if (!ch) return 1;
    }
}

static int type_size(int t) {
    switch (t & 0xF) {
    case 0: case 1: return 1;
    case 2: case 3: return 2;
    case 4: case 5: case 8: case 0xA: return 4;
    case 6: case 7: case 0xB: return 8;
    default: return -1;
    }
}

/* A column's value in a row: a number, or for data its address and size. */
static int utf_get(const Utf* u, int row, const char* name, uint32_t* v, uint32_t* len) {
    uint32_t p = u->cols, at = u->rows + (uint32_t)(row * u->rowlen);
    int i;
    if (row < 0 || row >= u->nrows) return 0;
    for (i = 0; i < u->ncol; i++) {
        int fl = LD8(p), n = type_size(fl), store = fl & 0xF0;
        uint32_t nm = u->strs + LD32(p + 1), where = 0;
        if (n < 0) return 0;
        p += 5;
        if (store == 0x30) where = p, p += (uint32_t)n;
        else if (store == 0x50) where = at, at += (uint32_t)n;
        if (where && guest_str_is(nm, name)) {
            switch (fl & 0xF) {
            case 0: case 1: *v = LD8(where); break;
            case 2: case 3: *v = LD16(where); break;
            case 0xB:
                *v = u->data + LD32(where);
                if (len) *len = LD32(where + 4);
                break;
            default: *v = LD32(where); break;
            }
            return 1;
        }
    }
    return 0;
}

static int utf_table(const Utf* top, const char* name, Utf* out) {
    uint32_t at, len = 0;
    return utf_get(top, 0, name, &at, &len) && len && utf_open(out, at);
}

/* ---- Sorting an ACB's cues. ---- */

typedef struct {
    Utf syn, seq, trk, cmd;
} Acb;

enum { HAS_MUSIC = 1, HAS_EFFECTS = 2, HAS_VOICES = 4 };

static int category_bits(uint32_t id) {
    switch (id) {
    case 1: case 8: return HAS_MUSIC;
    case 0: case 2: return HAS_VOICES;
    default: return HAS_EFFECTS;
    }
}

static int synth_bits(const Acb* a, uint32_t i, int depth);

/* A command list: category (0x0041, the id its last two bytes) and the
 * synths a track's events play (0x07D0, a type 2 reference). */
static int command_bits(const Acb* a, uint32_t index, int depth) {
    uint32_t at, len = 0, p;
    int bits = 0;
    if (index == 0xFFFF || !utf_get(&a->cmd, (int)index, "Command", &at, &len)) return 0;
    for (p = 0; p + 3 <= len;) {
        uint32_t code = LD16(at + p), n = LD8(at + p + 2), arg = at + p + 3;
        if (p + 3 + n > len) break;
        if (code == 0x0041 && n >= 2) bits |= category_bits(LD16(arg + n - 2));
        if (code == 0x07D0 && n >= 4 && LD16(arg) == 2) bits |= synth_bits(a, LD16(arg + 2), depth + 1);
        p += 3 + n;
    }
    return bits;
}

static int synth_bits(const Acb* a, uint32_t i, int depth) {
    uint32_t v, items, len = 0, k;
    int bits = 0;
    if (depth > 8) return 0;
    if (utf_get(&a->syn, (int)i, "CommandIndex", &v, NULL)) bits |= command_bits(a, v, depth);
    if (utf_get(&a->syn, (int)i, "ReferenceItems", &items, &len)) {
        for (k = 0; k + 4 <= len; k += 4) {
            if (LD16(items + k) == 3) bits |= synth_bits(a, LD16(items + k + 2), depth + 1);
        }
    }
    return bits;
}

static int sequence_bits(const Acb* a, uint32_t i) {
    uint32_t v, tracks, len = 0, n = 0, k;
    int bits = 0;
    if (utf_get(&a->seq, (int)i, "CommandIndex", &v, NULL)) bits |= command_bits(a, v, 0);
    utf_get(&a->seq, (int)i, "NumTracks", &n, NULL);
    if (utf_get(&a->seq, (int)i, "TrackIndex", &tracks, &len)) {
        for (k = 0; k < n && 2 * k + 2 <= len; k++) {
            int t = LD16(tracks + 2 * k);
            if (utf_get(&a->trk, t, "CommandIndex", &v, NULL)) bits |= command_bits(a, v, 0);
            if (utf_get(&a->trk, t, "EventIndex", &v, NULL)) bits |= command_bits(a, v, 0);
        }
    }
    return bits;
}

static int group_of_bits(int bits, int fallback) {
    return bits & HAS_MUSIC ? MUSIC : bits & HAS_VOICES ? VOICES : bits ? EFFECTS : fallback;
}

static int group_of_name(const char* name) {
    if (strstr(name, "VOICE")) return VOICES;
    if (strstr(name, "BGM") || strstr(name, "STAFF_ROLL")) return MUSIC;
    return EFFECTS;
}

#define ACBS 160
static struct {
    uint32_t handle;
    int count, fallback;
    uint16_t* ids;
    int8_t* groups;
} sAcb[ACBS];
static int sAcbNext;

static void sort_acb(uint32_t handle, uint32_t data, uint32_t size) {
    Utf top, cue;
    Acb a;
    const char* name = hle_dvd_file_sized(".acb", size);
    int slot, i;
    for (slot = 0; slot < ACBS && sAcb[slot].handle != handle; slot++) {}
    if (slot == ACBS) slot = sAcbNext++ % ACBS;
    free(sAcb[slot].ids);
    free(sAcb[slot].groups);
    memset(&sAcb[slot], 0, sizeof(sAcb[slot]));
    sAcb[slot].handle = handle;
    sAcb[slot].fallback = group_of_name(name);
    memset(&a, 0, sizeof(a));
    if (!utf_open(&top, data) || !utf_table(&top, "CueTable", &cue)) return;
    utf_table(&top, "SynthTable", &a.syn);
    utf_table(&top, "SequenceTable", &a.seq);
    utf_table(&top, "TrackTable", &a.trk);
    utf_table(&top, "CommandTable", &a.cmd);
    sAcb[slot].ids = (uint16_t*)malloc(cue.nrows * sizeof(uint16_t) + 1);
    sAcb[slot].groups = (int8_t*)malloc(cue.nrows + 1);
    for (i = 0; i < cue.nrows; i++) {
        uint32_t id = 0, type = 0, index = 0;
        int bits = 0;
        utf_get(&cue, i, "CueId", &id, NULL);
        utf_get(&cue, i, "ReferenceType", &type, NULL);
        utf_get(&cue, i, "ReferenceIndex", &index, NULL);
        if (type == 2) bits = synth_bits(&a, index, 0);
        else if (type == 3) bits = sequence_bits(&a, index);
        sAcb[slot].ids[i] = (uint16_t)id;
        sAcb[slot].groups[i] = (int8_t)group_of_bits(bits, sAcb[slot].fallback);
    }
    sAcb[slot].count = cue.nrows;
    if (log_on()) {
        int n[GROUPS] = {0};
        for (i = 0; i < cue.nrows; i++) n[sAcb[slot].groups[i]]++;
        fprintf(stderr, "sound: %s (%08X): %d cues, music %d, effects %d, voices %d\n", *name ? name : "?", handle,
                cue.nrows, n[MUSIC], n[EFFECTS], n[VOICES]);
    }
}

static int group_of_cue(uint32_t handle, uint32_t id) {
    int s, i;
    for (s = 0; s < ACBS; s++) {
        if (sAcb[s].handle != handle || !handle) continue;
        for (i = 0; i < sAcb[s].count; i++) {
            if (sAcb[s].ids[i] == id) return sAcb[s].groups[i];
        }
        return sAcb[s].fallback;
    }
    return NO_GROUP;
}

static void loaded(ppc_t* c, void (*orig)(ppc_t*)) {
    uint32_t data = c->r[3], size = c->r[4];
    orig(c);
    if (c->r[3]) sort_acb(c->r[3], data, size);
}

void f_80393A64(ppc_t* c) { loaded(c, f_80393A64_orig); }
void f_80393C60(ppc_t* c) { loaded(c, f_80393C60_orig); }

/* ---- The players. ---- */

#define PLAYERS 256
static struct {
    uint32_t player;
    int group;
    float wanted; /* what the game last set, before the setting */
} sPlayer[PLAYERS];
static int sPlayers;

static int player_slot(uint32_t player) {
    int i;
    for (i = 0; i < sPlayers; i++) {
        if (sPlayer[i].player == player) return i;
    }
    if (sPlayers == PLAYERS) return -1;
    sPlayer[sPlayers].player = player;
    sPlayer[sPlayers].group = NO_GROUP;
    sPlayer[sPlayers].wanted = 1.0f;
    return sPlayers++;
}

static float scaled(int slot) {
    int g = sPlayer[slot].group;
    return sPlayer[slot].wanted * (g == NO_GROUP ? 1.0f : sPercent[g] / 100.0f);
}

static void set_volume(ppc_t* c, uint32_t player, float volume) {
    ppc_t saved;
    memcpy(&saved, c, sizeof(saved));
    c->r[1] = (c->r[1] - 0x200) & ~7u;
    c->r[3] = player;
    c->f[1] = c->ps1[1] = volume;
    c->lr = 0;
    f_8039A730_orig(c);
    memcpy(c, &saved, sizeof(saved));
}

/* void criAtomExPlayer_SetVolume(CriAtomExPlayerHn player, CriFloat32 volume) */
void f_8039A730(ppc_t* c) {
    int slot = c->r[3] ? player_slot(c->r[3]) : -1;
    if (slot >= 0) {
        sPlayer[slot].wanted = (float)c->f[1];
        c->f[1] = c->ps1[1] = scaled(slot);
    }
    f_8039A730_orig(c);
}

/* void criAtomExPlayer_ResetParameters(CriAtomExPlayerHn player): the
 * volume back to 1, scaled. */
void f_8039A6D4(ppc_t* c) {
    uint32_t player = c->r[3];
    int slot = player ? player_slot(player) : -1;
    f_8039A6D4_orig(c);
    if (slot >= 0) {
        sPlayer[slot].wanted = 1.0f;
        if (sPlayer[slot].group != NO_GROUP && sPercent[sPlayer[slot].group] != 100) set_volume(c, player, scaled(slot));
    }
}

/* void criAtomExPlayer_SetCueId(CriAtomExPlayerHn player, CriAtomExAcbHn acb, CriAtomExCueId id) */
void f_80399324(ppc_t* c) {
    uint32_t player = c->r[3], acb = c->r[4], id = c->r[5];
    int slot = player ? player_slot(player) : -1;
    f_80399324_orig(c);
    if (slot >= 0) {
        int g = group_of_cue(acb, id), was = sPlayer[slot].group;
        float before = scaled(slot);
        sPlayer[slot].group = g;
        if (log_on()) fprintf(stderr, "sound: player %08X plays cue %u of %08X: group %d\n", player, id, acb, g);
        if (scaled(slot) != before || g != was) set_volume(c, player, scaled(slot));
    }
}

void hle_set_sound_volumes(int music, int effects, int voices) {
    sPercent[MUSIC] = (float)music;
    sPercent[EFFECTS] = (float)effects;
    sPercent[VOICES] = (float)voices;
    sDirty = 1;
}

/* Each frame (hle_game.c): after a change, every player's volume again. */
void hle_sound_frame(ppc_t* c) {
    int i;
    if (!sDirty) return;
    sDirty = 0;
    for (i = 0; i < sPlayers; i++) {
        if (sPlayer[i].group != NO_GROUP) set_volume(c, sPlayer[i].player, scaled(i));
    }
}
