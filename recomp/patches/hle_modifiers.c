/* Gameplay options, from the settings (the Options tab's Gameplay section):
 * how fast dialogue is written out, and modifiers to the berries Pikachu
 * collects and what the game charges for them. */
#include "hle.h"

static int sTextSpeed = 1;      /* steps of the dialogue writer per frame; 0: a page at once */
static int sBerryPercent = 100; /* berries collected, percent of the game's */
static int sPricePercent = 100; /* berries spent, percent of the game's */

void hle_set_text_speed(int steps) { sTextSpeed = steps; }
void hle_set_berry_rate(int percent) { sBerryPercent = percent; }
void hle_set_prices(int percent) { sPricePercent = percent; }

/* Dialogue: a message window's writer (the class with vtable 0x8032E678)
 * takes a step each frame while it's writing (state, +0x0C, 1): a count
 * of frames to wait (+0x38) is counted down, and when it runs out the
 * next character or tag is taken, and the wait set again for the one
 * after. It returns the window's next state: 1 while still writing, 2 or
 * 4 at a pause for the player (a page's end, a choice), 5 at the end.
 * Faster dialogue takes more steps a frame while it's still writing; the
 * pauses for the player are left alone. */
#define WRITER_STATE 0x0C
#define WRITING 1

/* int fn_80109630(Writer*): the writer's step. */
void f_80109630(ppc_t* c) {
    uint32_t writer = c->r[3];
    int steps = sTextSpeed > 0 ? sTextSpeed : 4000; /* a page at once: until it stops */
    f_80109630_orig(c);
    while (--steps > 0 && c->r[3] == WRITING && LD32(writer + WRITER_STATE) == WRITING) {
        c->r[3] = writer;
        f_80109630_orig(c);
    }
}

/* Berries: OtherDataSingleMode (the save's story data) keeps them, at
 * most 9999, and virt0x58 adds to them: a number (spent: less than 0),
 * and whether it counts as collected (the save also keeps a total of
 * those). Collected berries are multiplied, spent ones scaled. */

/* void OtherDataSingleMode::virt0x58(this, s32 berries, BOOL collected) */
void f_801279B0(ppc_t* c) {
    int32_t berries = (int32_t)c->r[4];
    if (berries > 0 && c->r[5] && sBerryPercent != 100) berries = (int32_t)((int64_t)berries * sBerryPercent / 100);
    else if (berries < 0 && sPricePercent != 100) berries = (int32_t)((int64_t)berries * sPricePercent / 100);
    if (berries != (int32_t)c->r[4]) HLE_LOG("berries: %d becomes %d", (int32_t)c->r[4], berries);
    c->r[4] = (uint32_t)berries;
    f_801279B0_orig(c);
}
