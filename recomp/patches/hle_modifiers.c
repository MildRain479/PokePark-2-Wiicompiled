/* Gameplay options, from the settings (the Options tab's Gameplay section):
 * how fast dialogue is written out, and modifiers to the berries Pikachu
 * collects and what the game charges for them, damage in battles, and
 * how fast the Pokémon Pikachu chases runs. */
#include <stdlib.h>

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

/* Battles: every Pokémon's hit points are a LifeModuleImpl (+0x08 left,
 * +0x0C most; Pikachu has 2 against Mankey's 3, and a hit takes 1 or
 * 0.25), and damage is taken by its virt0x38. Pikachu's is the one its
 * ModuleAccessorPlayer holds (+0x60); damage to any other was dealt by
 * Pikachu. The player's accessor is learned from the movement update
 * below, which runs for Pikachu every frame. */
#define ACCESSOR_PLAYER_VT 0x80328178 /* ModuleAccessorPlayer::__vt */
#define PLAYER_LIFE 0x60

static int sDamageDealt = 100; /* damage Pikachu deals, percent of the game's */
static int sDamageTaken = 100; /* damage Pikachu takes, percent of the game's */
static uint32_t sPlayer;       /* Pikachu's module accessor */

void hle_set_damage(int dealt, int taken) {
    sDamageDealt = dealt;
    sDamageTaken = taken;
}

/* void LifeModuleImpl::virt0x38(this, f32 damage) */
void f_80095EE0(ppc_t* c) {
    int percent = sPlayer && c->r[3] == LD32(sPlayer + PLAYER_LIFE) ? sDamageTaken : sDamageDealt;
    if (percent != 100) {
        double damage = c->f[1];
        c->f[1] = (double)(float)(damage * percent / 100);
        HLE_LOG("damage: %g becomes %g (%s)", damage, c->f[1], percent == sDamageTaken ? "taken" : "dealt");
    }
    f_80095EE0_orig(c);
}

/* Chase skill games: the Pokémon Pikachu chases is steered by its AI's
 * chase action (FieldAIActionChallengeChase, and its Fly and
 * FlyVertical kinds), whose step is handed the Pokémon's module
 * accessor. It moves like any Pokémon: each frame its KineticEnergyControl
 * eases its velocity (+0x20, +0x24) toward the input times a target speed
 * (+0x28, +0x2C) at a rate (+0x38, +0x3C), capped at a top speed (+0x30,
 * unless -1). For a Pokémon being chased those are scaled for the update.
 *
 * RECOMP_CHASE_ALL=1 (testing): every Pokémon but Pikachu counts as being
 * chased. */
#define CHASED_SLOTS 4
#define CHASED_FRAMES 4 /* updates without a chase step before it no longer counts */

static int sChaseSpeed = 100; /* the chased Pokémon's speed, percent of the game's */
static struct { uint32_t accessor; int age; } sChased[CHASED_SLOTS];

void hle_set_chase_speed(int percent) { sChaseSpeed = percent; }

static void chase_step(uint32_t accessor) {
    int i, free = -1, oldest = 0;
    for (i = 0; i < CHASED_SLOTS; i++) {
        if (sChased[i].accessor == accessor) {
            sChased[i].age = 0;
            return;
        }
        if (!sChased[i].accessor && free < 0) free = i;
        if (sChased[i].age > sChased[oldest].age) oldest = i;
    }
    i = free >= 0 ? free : oldest;
    HLE_LOG("chase: %08X is being chased", accessor);
    sChased[i].accessor = accessor;
    sChased[i].age = 0;
}

static int chased(uint32_t accessor) {
    static int all = -1;
    int i;
    if (all < 0) {
        const char* e = getenv("RECOMP_CHASE_ALL");
        all = e && *e && *e != '0';
    }
    if (all) return accessor != sPlayer;
    for (i = 0; i < CHASED_SLOTS; i++) {
        if (sChased[i].accessor != accessor) continue;
        if (++sChased[i].age > CHASED_FRAMES) {
            sChased[i].accessor = 0;
            return 0;
        }
        return 1;
    }
    return 0;
}

/* The chase actions' steps: void virt0x14(this, ModuleAccessor*) */
void f_801441C0(ppc_t* c) {
    chase_step(c->r[4]);
    f_801441C0_orig(c);
}

void f_80147590(ppc_t* c) {
    chase_step(c->r[4]);
    f_80147590_orig(c);
}

void f_80149240(ppc_t* c) {
    chase_step(c->r[4]);
    f_80149240_orig(c);
}

/* void KineticEnergyControl::virt0x4_0xC(this, ModuleAccessor*): the
 * movement update. */
void f_8008FFA0(ppc_t* c) {
    static const int kScaled[] = {0x28, 0x2C, 0x30, 0x38, 0x3C};
    uint32_t energy = c->r[3], accessor = c->r[4], saved[5];
    double k = sChaseSpeed / 100.0;
    int i;
    if (LD32(accessor) == ACCESSOR_PLAYER_VT) sPlayer = accessor;
    if (sChaseSpeed == 100 || accessor == sPlayer || !chased(accessor)) {
        f_8008FFA0_orig(c);
        return;
    }
    for (i = 0; i < 5; i++) {
        saved[i] = LD32(energy + kScaled[i]);
        if (kScaled[i] != 0x30 || ld_f32(energy + 0x30) > 0) st_f32(energy + kScaled[i], ld_f32(energy + kScaled[i]) * k);
    }
    f_8008FFA0_orig(c);
    for (i = 0; i < 5; i++) ST32(energy + kScaled[i], saved[i]);
}
