/* Gameplay options, from the settings: modifiers to the berries collected
 * and what the game charges for them, damage in battles, and how fast the
 * Pokémon being chased runs. As in the first game (PokePark 1's hle_modifiers.c).
 * How fast dialogue is written out is not done here yet. */
#include <stdio.h>
#include <stdlib.h>

#include "hle.h"

static int sBerryPercent = 100; /* berries collected, percent of the game's */
static int sTextSpeed = 1; /* writer steps a frame; 0 = a page at once */
static int sPricePercent = 100; /* berries spent, percent of the game's */

void hle_set_text_speed(int steps) { sTextSpeed = steps; }
void hle_set_berry_rate(int percent) { sBerryPercent = percent; }
void hle_set_prices(int percent) { sPricePercent = percent; }

/* Berries: the save's data (the manager's +0x48) keeps them, at most 99999,
 * and a dozen places in the game add to them or take from them directly.
 * So they are watched: each frame in the park, any change since the last
 * frame is scaled. */
#define SAVE_DATA 0x48
#define SAVE_BERRIES 0x1474
#define MAX_BERRIES 99999

void hle_modifiers_frame(ppc_t* c, int inPark) {
    static int have;
    static int32_t last;
    uint32_t berries;
    int32_t now, change, scaled;
    if (!inPark) {
        have = 0; /* a file loaded, or a minigame's prize: not ours to scale yet */
        return;
    }
    berries = LD32(hle_call(c, A_GlobalManager_Get, 0, 0) + SAVE_DATA) + SAVE_BERRIES;
    now = (int32_t)LD32(berries);
    if (!have) {
        have = 1;
        last = now;
        return;
    }
    change = now - last;
    if (!change) return;
    scaled = (int32_t)((int64_t)change * (change > 0 ? sBerryPercent : sPricePercent) / 100);
    HLE_LOG("berries: %d becomes %d", change, scaled);
    last += scaled;
    last = last < 0 ? 0 : last > MAX_BERRIES ? MAX_BERRIES : last;
    ST32(berries, (uint32_t)last);
}

/* Battles: every Pokémon's hit points are a LifeModuleImpl (+0x08 left,
 * +0x0C most; Pikachu has 2 against Mankey's 3, and a hit takes 1 or
 * 0.25), and damage is taken by its virt0x38. Pikachu's is the one its
 * ModuleAccessorPlayer holds (+0x60); damage to any other was dealt by
 * Pikachu. The player's accessor is learned from the movement update
 * below, which runs for Pikachu every frame. */
#define ACCESSOR_PLAYER_VT 0x8044A850 /* ModuleAccessorPlayer::__vt */
#define PLAYER_LIFE 0x60

static int sDamageDealt = 100; /* damage Pikachu deals, percent of the game's */
static int sDamageTaken = 100; /* damage Pikachu takes, percent of the game's */
static uint32_t sPlayer;       /* Pikachu's module accessor */

void hle_set_damage(int dealt, int taken) {
    sDamageDealt = dealt;
    sDamageTaken = taken;
}

/* void LifeModuleImpl::virt0x38(this, f32 damage) */
void F_LifeModuleImpl_virt0x4_0x38(ppc_t* c) {
    int percent = sPlayer && c->r[3] == LD32(sPlayer + PLAYER_LIFE) ? sDamageTaken : sDamageDealt;
    if (percent != 100) {
        double damage = c->f[1];
        c->f[1] = (double)(float)(damage * percent / 100);
        HLE_LOG("damage: %g becomes %g (%s)", damage, c->f[1], percent == sDamageTaken ? "taken" : "dealt");
    }
    F_LifeModuleImpl_virt0x4_0x38_orig(c);
}

/* Chase skill games: the Pokémon Pikachu chases is steered by its AI's
 * chase action (FieldAIActionChallengeChase), whose step is handed the Pokémon's module
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
void F_FieldAIActionChallengeChase_virt0x14(ppc_t* c) {
    chase_step(c->r[4]);
    F_FieldAIActionChallengeChase_virt0x14_orig(c);
}

/* void KineticEnergyControl::virt0x4_0xC(this, ModuleAccessor*): the
 * movement update. */
void F_KineticEnergyControl_virt0x4_0xC(ppc_t* c) {
    static const int kScaled[] = {0x28, 0x2C, 0x30, 0x38, 0x3C};
    uint32_t energy = c->r[3], accessor = c->r[4], saved[5];
    double k = sChaseSpeed / 100.0;
    int i;
    if (LD32(accessor) == ACCESSOR_PLAYER_VT) sPlayer = accessor;
    if (sChaseSpeed == 100 || accessor == sPlayer || !chased(accessor)) {
        F_KineticEnergyControl_virt0x4_0xC_orig(c);
        return;
    }
    for (i = 0; i < 5; i++) {
        saved[i] = LD32(energy + kScaled[i]);
        if (kScaled[i] != 0x30 || ld_f32(energy + 0x30) > 0) st_f32(energy + kScaled[i], ld_f32(energy + kScaled[i]) * k);
    }
    F_KineticEnergyControl_virt0x4_0xC_orig(c);
    for (i = 0; i < 5; i++) ST32(energy + kScaled[i], saved[i]);
}

/* Dialogue: MenuMessagePuncherImpl's step, the first game's writer's. See
 * PokePark 1's hle_modifiers.c: faster dialogue takes more steps a frame while
 * it's still writing, and the pauses for the player are left alone. */
#define WRITER_STATE 0x0C
#define WRITING 1

/* int fn_80148520(MenuMessagePuncherImpl*): the writer's step. */
void F_fn_80148520(ppc_t* c) {
    uint32_t writer = c->r[3];
    int steps = sTextSpeed > 0 ? sTextSpeed : 4000; /* a page at once: until it stops */
    F_fn_80148520_orig(c);
    while (--steps > 0 && c->r[3] == WRITING && LD32(writer + WRITER_STATE) == WRITING) {
        c->r[3] = writer;
        F_fn_80148520_orig(c);
    }
}
