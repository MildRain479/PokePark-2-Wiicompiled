/* The friends list a second screen shows (options.c's pad_pages_draw).
 *
 * The Pad numbers the Pokémon that can be befriended 0-197 (the partners
 * 0-3), its names' labels a table (0x80435C10, fn_8002E330: "NAME_POCHAMA"),
 * and they're found by slot: 258 of them, each one of those found in a zone
 * (0x804141F0, fn_800F87D0). Each one's friendship points are in the save:
 * a u16 at +0x28 of the 8-byte entry, in the array at +0x48 of the game's
 * data (fn_80074C60), that the table at 0x80413100 (fn_8002E3D0) gives it;
 * 100 or more is a friend, as the Pad counts its friends (fn_80182DA0).
 * They're read from the minimap's drawing (hle_minimap.c), on the game's
 * thread in the park.
 *
 * The list is every one of those found in a slot, once, in the Pad's
 * order (Piplup first), the partners left out. Characters (the models', another numbering)
 * have internal names, a table of char* (0x8044DA48), that name the icons;
 * a Pokémon's icon is the character's of its name, or failing that the one
 * whose name starts the most like it (Onond for ONONDO, PururillF for
 * PURURILL). Icons and English names are read from the disc
 * (hle_pad_art.c). */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "hle.h"

#define POKEMON 198
#define PARTNERS 4 /* Pikachu, Oshawott, Tepig, Snivy: 0-3 */
#define LABELS 0x80435C10u /* per Pokémon: its name's label */
#define SLOTS 258
#define SLOT_POKEMON 0x804141F0u
#define CHARACTERS 258
#define CHARACTER_NAMES 0x8044DA48u
#define POINTS_ENTRY 0x80413100u /* per Pokémon: its entry */
#define POINTS 0x48             /* the game data's array of entries */
#define POINTS_AT 0x28          /* in an 8-byte entry */
#define FRIEND_POINTS 100
#define ASK_EVERY 30 /* minimap draws between asking */

int hle_friend_has_icon(const char* name); /* hle_pad_art.c */
int hle_friend_name(const char* name, char* out, int size);
const uint32_t* hle_friend_icon(const char* name, int* w, int* h);

static struct {
    char name[24];    /* the icon's character's internal name */
    char english[24]; /* "" if none found */
    int friend_;
} sRows[POKEMON];
static int sCount = -1; /* rows; -1 until set up */
static int sRowOf[POKEMON];
static volatile unsigned sChanges;

static void read_name(uint32_t at, char* out, int size) {
    int n;
    for (n = 0; n < size - 1 && LD8(at + n) >= 32 && LD8(at + n) < 127; n++) out[n] = (char)LD8(at + n);
    out[n] = 0;
}

/* The character whose icon a Pokémon (its name's label, past NAME_) has,
 * into out: 0 if none. */
static int icon_of(const char* pokemon, char* out, int size) {
    char name[24];
    int c, best = 4; /* letters alike, at least */
    out[0] = 0;
    for (c = 0; c < CHARACTERS; c++) {
        int n;
        read_name(LD32(CHARACTER_NAMES + c * 4), name, sizeof(name));
        for (n = 0; name[n] && tolower((unsigned char)name[n]) == tolower((unsigned char)pokemon[n]); n++) {}
        if (!name[n] && !pokemon[n]) n = 99; /* the same */
        if (n > best && hle_friend_has_icon(name)) {
            best = n;
            snprintf(out, (size_t)size, "%s", name);
        }
    }
    return out[0] != 0;
}

/* The rows, once the icons and messages are read. */
static int set_up(void) {
    int i, count = 0;
    if (!hle_friend_has_icon("Pikachu")) return 0;
    int found[POKEMON] = {0};
    for (i = 0; i < SLOTS; i++) {
        uint32_t p = LD32(SLOT_POKEMON + i * 4);
        if (p < POKEMON) found[p] = 1;
    }
    for (i = 0; i < POKEMON; i++) sRowOf[i] = -1;
    for (i = PARTNERS; i < POKEMON; i++) {
        uint32_t p = (uint32_t)i;
        char label[32];
        if (!found[p]) continue;
        read_name(LD32(LABELS + p * 4), label, sizeof(label));
        if (strncmp(label, "NAME_", 5) || !icon_of(label + 5, sRows[count].name, sizeof(sRows[count].name))) continue;
        sRowOf[p] = count;
        if (!hle_friend_name(label + 5, sRows[count].english, sizeof(sRows[count].english))) sRows[count].english[0] = 0;
        sRows[count].friend_ = 0;
        count++;
    }
#ifdef __GNUC__
    __atomic_store_n(&sCount, count, __ATOMIC_RELEASE); /* the rows before their count */
#else
    sCount = count;
#endif
    sChanges++;
    return 1;
}

/* Read on the game's thread (the minimap's drawing): which rows are
 * friends. */
void hle_friends_update(ppc_t* c) {
    static int n;
    ppc_t save;
    uint32_t entries, i;
    int now[POKEMON], changed = 0;
    if (n++ % ASK_EVERY) return;
    if (sCount < 0 && !set_up()) return;
    save = *c;
    c->r[1] -= 0x200; /* below the hooked call's frame */
    f_80074C60(c);
    entries = LD32(c->r[3] + POINTS);
    *c = save;
    if (!entries) return;
    memset(now, 0, sizeof(now));
    for (i = PARTNERS; i < POKEMON; i++) {
        uint32_t at = entries + LD32(POINTS_ENTRY + i * 4) * 8 + POINTS_AT;
        if (sRowOf[i] >= 0 && LD16(at) >= FRIEND_POINTS) now[sRowOf[i]] = 1;
    }
    for (i = 0; i < (uint32_t)sCount; i++) {
        if (sRows[i].friend_ != now[i]) changed = 1;
        sRows[i].friend_ = now[i];
    }
    if (changed) sChanges++;
}

/* For options.c, on the presenting thread: the rows (0 until known), each
 * one's name (English, else internal), whether it's a friend, and its
 * icon; and a count that changes whenever any of that may have. */
int hle_friends_rows(void) {
#ifdef __GNUC__
    int n = __atomic_load_n(&sCount, __ATOMIC_ACQUIRE);
#else
    int n = sCount;
#endif
    return n < 0 ? 0 : n;
}
const char* hle_friends_name(int i) { return sRows[i].english[0] ? sRows[i].english : sRows[i].name; }
int hle_friends_is_friend(int i) { return sRows[i].friend_; }
const uint32_t* hle_friends_icon(int i, int* w, int* h) { return hle_friend_icon(sRows[i].name, w, h); }
unsigned hle_friends_changes(void) { return sChanges; }
