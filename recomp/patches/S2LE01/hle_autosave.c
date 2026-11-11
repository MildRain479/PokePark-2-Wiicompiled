/* Autosave (the Gameplay setting): the game saved, as the PokéPark Pad's
 * Save does, whenever the player makes a friend or enters another area.
 *
 * The Pad's save (fn_80185A20, then fn_80185A80 each frame) holds the
 * game busy (fn_8002EAB0 on fn_800779E0's object, 1 to hold and 0 to let
 * go), brings the play time up to date (fn_802161D0) and starts writing
 * the save data, GD<slot> and its copy, to the slot the game was loaded
 * from (fn_802165A0; the game data's +0x1D4, from fn_80074C60; +0x1D8 is
 * 3 while it's under way); fn_802166A0 says whether that's still busy (1)
 * or done (0, the writes released) and fn_80216750 whether it went well.
 *
 * A friend made is a Pokémon's friendship points reaching 100 (as
 * hle_friends.c reads them); an area entered is the save data's area and
 * zone (+0x14A6, +0x14A7 of the game data's +0x48, where a loaded game
 * starts) changing. Both are watched from the minimap's drawing
 * (hle_minimap.c), so in the park; after a gap in that (a load, a menu,
 * a minigame) what's there is taken as it is. The save waits until no
 * event script (a story scene, a talk, a favour) has run for half a
 * second: the script interface's (0x804B04F0) +0x248, which the
 * EventManager (fn_8004E2A0) checks before it starts one, is nonzero
 * while one runs. */
#include <time.h>

#include "hle.h"

#define DATA 0x48u
#define SLOT 0x1D4u   /* the save slot the game was loaded from */
#define STATE 0x1D8u  /* 3: a save under way */
#define SAVING 3u
#define SLOTS 3u
#define AREA 0x14A6u  /* in the data: area, then zone */
#define POKEMON 198
#define POINTS_ENTRY 0x80413100u
#define POINTS_AT 0x28u
#define FRIEND_POINTS 100
#define SCRIPTS 0x804B04F0u /* the event script interface */
#define SCRIPTS_RUNNING 0x248u
#define QUIET_FRAMES 30 /* minimap draws with no script running */

static int sOn;     /* the setting */
static int sWanted; /* something to save */
static int sOurs;   /* a save of ours under way */
static int sQuiet;  /* minimap draws since a script last ran */
static int sPrimed;
static uint32_t sArea, sSlot;
static int sFriends;
static time_t sLast;

void hle_set_autosave(int on) {
    sOn = on;
    if (!on) sWanted = 0; /* turned off while one waited for a scene to end */
}

static int script_running(void) {
    uint32_t esi = LD32(SCRIPTS);
    return esi && LD32(esi + SCRIPTS_RUNNING) != 0;
}

static int friends(uint32_t data) {
    int i, n = 0;
    for (i = 4; i < POKEMON; i++) { /* the partners left out */
        if (LD16(data + LD32(POINTS_ENTRY + i * 4) * 8 + POINTS_AT) >= FRIEND_POINTS) n++;
    }
    return n;
}

/* Calls fn with r3 and r4, below the hooked call's frame. */
static uint32_t call(ppc_t* c, void (*fn)(ppc_t*), uint32_t r3, uint32_t r4) {
    c->r[3] = r3;
    c->r[4] = r4;
    fn(c);
    return c->r[3];
}

static void hold(ppc_t* c, int on) { call(c, f_8002EAB0, call(c, f_800779E0, 0, 0), on ? 1 : 0); }

/* Each minimap draw, on the game's thread. */
void hle_autosave_update(ppc_t* c) {
    ppc_t save = *c;
    uint32_t gm, data, area, slot;
    int n;
    time_t now = time(NULL);
    c->r[1] -= 0x200;
    gm = call(c, f_80074C60, 0, 0);
    data = LD32(gm + DATA);
    if (!data) goto out;
    area = LD16(data + AREA);
    slot = LD32(gm + SLOT);
    n = friends(data);
    if (!sPrimed || slot != sSlot || now - sLast > 2) {
        sPrimed = 1; /* as it is */
    } else {
        if (area != sArea) {
            if (sOn) HLE_LOG("autosave: area %u zone %u entered", area >> 8, area & 0xFF);
            sWanted |= sOn;
        }
        if (n > sFriends) {
            if (sOn) HLE_LOG("autosave: a friend made");
            sWanted |= sOn;
        }
    }
    sArea = area;
    sSlot = slot;
    sFriends = n;
    sLast = now;
    if (script_running()) sQuiet = 0;
    else if (sQuiet < QUIET_FRAMES) sQuiet++;
    if (sOurs) {
        if (!call(c, f_802166A0, gm, 0)) {
            int ok = call(c, f_80216750, gm, 0) == 1;
            hold(c, 0);
            sOurs = 0;
            HLE_LOG("autosave: %s", ok ? "saved" : "failed");
        }
    } else if (sWanted && sQuiet >= QUIET_FRAMES && LD32(gm + STATE) == 0 && slot < SLOTS) {
        sWanted = 0;
        hold(c, 1);
        call(c, f_802161D0, gm, 0);
        call(c, f_802165A0, gm, 0);
        sOurs = LD32(gm + STATE) == SAVING;
        if (!sOurs) hold(c, 0);
        HLE_LOG("autosave: saving to slot %u", slot);
    }
out:
    *c = save;
}
