/* Minigame instructions for the PC's controls. Each minigame's lobby
 * explains how to play (Lobby.bmg, the same in every lobby), and most of
 * it is about the Wii Remote: holding it upright, shaking it, tilting
 * it, pointing it. Here those lines are replaced, as the game looks them
 * up, by ones that say what to press (hle_motion.c gives the remote's
 * motion for it), on the keyboard or the controller, whichever was used
 * last. Buttons are shown with the game's own button tags, which
 * hle_prompts.c draws as the keys or controller buttons bound to them.
 * The movement and shake keys are named instead: the d-pad's glyph, four
 * keys in a cross, is too small to read in a sentence, and the shake keys
 * have none. The pictures beside the text still show a Wii Remote.
 *
 * The new text is kept in guest memory taken from the top of the arena
 * at boot, and written again whenever the bindings or the device change.
 * A message is a big-endian UTF-16 string; a tag is 0x1A, its length in
 * bytes and group (one byte each), then its data: group 5 is a button,
 * shown as its glyph (U+E040 + the button: 2 A, 3 B, 7 1, 8 2). */
#include <stdio.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

void (*g_host_button_name)(uint32_t button, char* out, int n);

#define TEXT_SIZE 0x4000
static uint32_t sBase;

/* Called at boot, after the FST has taken the top of MEM1's arena. */
void hle_text_boot_setup(void) {
    uint32_t arena_hi = LD32(0x80000034);
    sBase = (arena_hi - TEXT_SIZE) & ~31u;
    ST32(0x80000034, sBase); /* arena hi */
    ST32(0x80003110, sBase); /* MEM1 arena end */
}

/* In the new text: [A] [B] [1] [2] are those buttons' glyphs; [up]
 * [down] [left] [right] the movement keys' names, and [shake] the two
 * shake keys'. Each replaces the message at index whose text begins with
 * was (Lobby.bmg's messages, checked so that no other file's are
 * replaced): key on the keyboard, pad (if different) on the controller. */
static const struct {
    int index;
    const char *was, *key, *pad;
} kText[] = {
    /* Absol's Hurdle Bounce */
    {20, "Shake the Wii Remote", "Press [shake] by turns\nto start running. The faster you\npress them, the faster you'll go!"},
    {23, "Once you've cleared the final hurdle,",
     "Once you've cleared the final hurdle,\npress [shake] by turns as quickly\nas you can to sprint to the finish line!"},
    /* Rayquaza's Balloon Panic */
    {26, "When you begin, tilt", "When you begin, steer with [left] and [right].\nBe careful not to fall!",
     "When you begin, steer with the left stick.\nBe careful not to fall!"},
    {31, "Once you reach Rayquaza's head,", "Once you reach Rayquaza's head,\nyou'll see a Pointer.\nMove it with the mouse.",
     "Once you reach Rayquaza's head,\nyou'll see a Pointer.\nMove it with the right stick."},
    /* Venusaur's Vine Swing */
    {35, "Hold the Wii Remote vertically", "When the time starts, press [shake]\nby turns to swing on the vine!"},
    {37, "Once you are swinging well,", "Once you are swinging well,\nit's time to jump!\nPress [B]."},
    /* Tangrowth's Swing-Along */
    {41, "Hold the Wii Remote vertically", "When the time starts, press [shake]\nby turns to swing on the rope!"},
    {42, "Swing in time with the rope", "Swing in time with the rope\nand jump when ready.\nPress [B] to jump."},
    /* Dusknoir's Speed Slam */
    {46, "Shake the Wii Remote", "Press [shake] by turns\nto start running. The faster you\npress them, the faster you'll go!"},
    {48, "You'll slow down when you",
     "You'll slow down when you\nsmash into blocks. Keep pressing\n[shake] by turns to keep your speed up!"},
    /* Gyarados' Aqua Dash */
    {52, "Tilt the Wii Remote", "Press [left] or [right]\nwhen you want to make a turn.",
     "Push the left stick left or right\nwhen you want to make a turn."},
    /* Pelipper's Circle Circuit */
    {59, "Tilt your Wii Remote left or right", "Press [left] or [right] if you want\nto move to the left or right.",
     "Push the left stick left or right if\nyou want to move in those directions."},
    {60, "Tilt your Wii Remote backward", "Press [up] to fly higher\nor [down] to fly lower.",
     "Push the left stick up to fly higher\nor down to fly lower."},
    /* Empoleon's Snow Slide */
    {66, "Tilt the Wii Remote left or right", "Press [left] or [right]\nwhen you want to make a turn.",
     "Push the left stick left or right\nwhen you want to make a turn."},
    /* Rhyperior's Bumper Burn */
    {80, "Tilt the Wii Remote as shown", "Use [up], [left], [down] and [right]\nto move around when you begin.\nAvoid falling off!",
     "Use the left stick to move around\nwhen you begin.\nAvoid falling off!"},
    /* Blaziken's Boulder Bash */
    {88, "Hold the Wii Remote vertically.", "When the game begins, boulders will\nstart to fly toward you!"},
    {89, "Bash boulders by swinging",
     "Bash boulders by swinging with [1] or [2].\nThe better your swing is timed,\nthe more points you'll score!"},
    {90, "You won't be able to move",
     "You won't be able to move for a while if a\nboulder hits you! Press [shake] by turns\nto recover as quickly as you can!"},
    /* Rotom's Spooky Shoot-'em-Up */
    {95, "Point the Wii Remote at the screen.",
     "Move the Pointer with the mouse.\nTake it past the side of the screen\nto turn that way.",
     "Move the Pointer with the right stick.\nTake it past the side of the screen\nto turn that way."},
    {96, "Push ", "Press [A] to shoot!\nKeep Ghost types away and score points."},
    /* Salamence's Air Race */
    {103, "Tilt the Wii Remote left or right", "Press [left] or [right]\nwhen you want to make a turn.",
     "Push the left stick left or right\nwhen you want to make a turn."},
    {104, "Tilt your Wii Remote backward", "Press [up] to fly higher\nor [down] to fly lower.",
     "Push the left stick up to fly higher\nor down to fly lower."},
    /* Bulbasaur's Daring Dash */
    {111, "Shake the Wii Remote", "Press [shake] by turns\nto start running. The faster you\npress them, the faster you'll go!"},
};
#define TEXTS ((int)(sizeof(kText) / sizeof(kText[0])))

static uint32_t sAt[TEXTS]; /* where each is written */
static uint32_t sGeneration = 0xFFFFFFFFu;

static uint32_t put(uint32_t at, uint32_t end, uint16_t ch) {
    if (at + 2 <= end) ST16(at, ch);
    return at + 2;
}

static uint32_t put_str(uint32_t at, uint32_t end, const char* s) {
    for (; *s; s++) at = put(at, end, (uint8_t)*s);
    return at;
}

static uint32_t put_button(uint32_t at, uint32_t end, int glyph) {
    at = put(at, end, 0x1A);
    at = put(at, end, 0x0605); /* 6 bytes, group 5 */
    return put(at, end, (uint16_t)glyph);
}

static void button_name(uint32_t button, char* out, int n) {
    *out = 0;
    if (g_host_button_name) g_host_button_name(button, out, n);
}

/* Writes the new text, as the controls now stand. */
static void write_all(void) {
    static const struct { const char* name; int glyph; } kGlyph[] = {{"[A]", 2}, {"[B]", 3}, {"[1]", 7}, {"[2]", 8}};
    static const struct { const char* name; uint32_t button; } kName[] = {
        {"[up]", HOST_PAD_SCREEN_UP}, {"[down]", HOST_PAD_SCREEN_DOWN},
        {"[left]", HOST_PAD_SCREEN_LEFT}, {"[right]", HOST_PAD_SCREEN_RIGHT},
    };
    char shake[64], one[24], two[24];
    /* The generation's low bit: the controller was used last (prompts.c). */
    int pad = g_host_prompt_generation ? (int)(g_host_prompt_generation() & 1) : 0, i;
    uint32_t at = sBase, end = sBase + TEXT_SIZE;
    button_name(HOST_PAD_SHAKE, one, sizeof(one));
    button_name(HOST_PAD_SHAKE2, two, sizeof(two));
    if (*one && *two) snprintf(shake, sizeof(shake), "%s and %s", one, two);
    else snprintf(shake, sizeof(shake), "the shake buttons");
    for (i = 0; i < TEXTS; i++) {
        const char* s = pad && kText[i].pad ? kText[i].pad : kText[i].key;
        sAt[i] = at;
        while (*s) {
            size_t k, n = 0;
            for (k = 0; k < sizeof(kGlyph) / sizeof(kGlyph[0]); k++) {
                if (!strncmp(s, kGlyph[k].name, n = strlen(kGlyph[k].name))) break;
            }
            if (k < sizeof(kGlyph) / sizeof(kGlyph[0])) {
                at = put_button(at, end, kGlyph[k].glyph);
                s += n;
                continue;
            }
            for (k = 0; k < sizeof(kName) / sizeof(kName[0]); k++) {
                if (!strncmp(s, kName[k].name, n = strlen(kName[k].name))) break;
            }
            if (k < sizeof(kName) / sizeof(kName[0])) {
                char name[24];
                button_name(kName[k].button, name, sizeof(name));
                if (*name) at = put_str(at, end, name);
                else at = put_str(at, end, "nothing"); /* unbound */
                s += n;
            } else if (!strncmp(s, "[shake]", 7)) {
                at = put_str(at, end, shake);
                s += 7;
            } else {
                at = put(at, end, (uint8_t)*s++);
            }
        }
        at = put(at, end, 0);
        if (at > end) {
            HLE_WARN("text: no room for the minigame instructions");
            sAt[i] = 0;
            at = end;
        }
    }
}

/* Whether the string at s (UTF-16) begins with prefix. */
static int begins(uint32_t s, const char* prefix) {
    for (; *prefix; prefix++, s += 2) {
        if (LD16(s) != (uint8_t)*prefix) return 0;
    }
    return 1;
}

/* const wchar_t* MessageData::GetString(int index): message index of a
 * BMG file (the object holds its INF1 at +8, DAT1 at +0xC). */
void f_801081C0(ppc_t* c) {
    int index = (int)c->r[4], i;
    uint32_t gen;
    f_801081C0_orig(c);
    if (!sBase || !c->r[3]) return;
    for (i = 0; i < TEXTS; i++) {
        if (kText[i].index != index || !begins(c->r[3], kText[i].was)) continue;
        gen = g_host_prompt_generation ? g_host_prompt_generation() : 0;
        if (gen != sGeneration) {
            sGeneration = gen;
            write_all();
        }
        if (sAt[i]) c->r[3] = sAt[i];
        return;
    }
}
