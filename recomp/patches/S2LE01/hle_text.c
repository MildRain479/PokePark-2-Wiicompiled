/* The instructions, for the PC's and the phone's controls. The attractions'
 * How to Play pages (AttractionMenuRes.dac, Attraction.msbt) and the
 * field's lessons (Message/US_English/Fd*.msbt: the partners' moves,
 * Shake Sprint, Iron Tail, the portals, Photos, moving about) tell the
 * player to shake, swing, tilt and point the Wii Remote. Here those
 * messages are replaced, as the game looks them up, by ones that say what
 * to press: on the keyboard and mouse, the controller, or the touch
 * controls, whichever was used last.
 *
 * In the attractions the shake keys swing the remote by turns (in Dance
 * Inferno each move has a key instead, drawn on its icon: hle_dance.c);
 * in the field, holding either one shakes it (hle_wpad.c). Buttons are the
 * game's own button tags, which hle_prompts.c draws as the keys bound to
 * them; the movement and shake keys are named instead, from the bindings.
 *
 * The messages go through LMS_GetTextByIndex (fn_803D7670: the message
 * data's TXT2 section, its text index'th); each is recognized by its label
 * (LBL1) and how it begins. A message is big-endian UTF-16; a tag is 0x0E,
 * its group, type and parameters' size (u16 each), then those: group 2 is
 * a button (0 the d-pad, 1 A, 2 B, 5 -, 6 1, 7 2), group 0 type 4 a page
 * break. The new text is kept in guest memory taken from the top of the
 * arena at boot, and written again whenever the bindings or the device
 * change. */
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

/* In the new text: [A] [B] [1] [2] [-] are those buttons' tags, [page] a
 * page break; [shake] names the two shake keys ("Q and E"), [either shake]
 * the same with "or", [move] the four movement keys, [up] [down] [left]
 * [right] one each. Each replaces the message labelled label whose text
 * begins with was: key on the keyboard, pad (if given) on a controller,
 * touch (if given; else pad) with the phone's touch controls. */
static const struct {
    const char *label, *was, *key, *pad, *touch;
} kText[] = {
    /* Cake Contraption (alone, with others, and its multiplayer lobby's) */
    {"AT_30_00_020", "Point the Wii Remote", "Move the cursor on the screen\nwith the mouse to play.",
     "Move the cursor on the screen\nwith the right stick to play.", "Drag on the screen to move\nthe cursor to play."},
    {"AT_30_10_020", "Point the Wii Remote", "Move the cursor on the screen\nwith the mouse to play.",
     "Move the cursor on the screen\nwith the right stick to play.", "Drag on the screen to move\nthe cursor to play."},
    {"AT_30_04_020", "Point the Wii Remote", "Move the cursor on the screen\nwith the mouse to play.",
     "Move the cursor on the screen\nwith the right stick to play.", "Drag on the screen to move\nthe cursor to play."},
    {"AT_30_00_030", "Aim the pointer", "Aim the cursor at a cake ingredient,\nand press [A] to shoot it!"},
    {"AT_30_10_030", "Aim the pointer", "Aim the cursor at a cake ingredient,\nand press [A] to shoot it!"},
    {"AT_30_04_030", "Aim the pointer", "Aim the cursor at a cake ingredient,\nand press [A] to shoot it!"},
    /* Dance Inferno */
    {"AT_30_01_020", "Hold the Wii Remote",
     "Press the button on each move's\nicon as it comes up, and hold it\nto its bar's end, to dance\njust like Lopunny."},
    {"AT_30_11_020", "Hold the Wii Remote",
     "Press the button on each move's\nicon as it comes up, and hold it\nto its bar's end, to dance\njust like Lopunny."},
    /* Power Bash */
    {"AT_30_02_020", "When a Pok", "When a Pok\xe9mon appears,\npress [shake] by turns\nto swing at it!"},
    {"AT_30_12_020", "When a Pok", "When a Pok\xe9mon appears,\npress [shake] by turns\nto swing at it!"},
    {"AT_30_02_030", "Swing the Wii Remote", "Swing when the Pok\xe9mon is close to\nthe center to get a high score."},
    {"AT_30_12_030", "Swing the Wii Remote", "Swing when the Pok\xe9mon is close to\nthe center to get a high score."},
    /* Flight of Fancy */
    {"AT_30_03_020", "When you want to fly to the side", "When you want to fly to the side,\npress [left] or [right].",
     "When you want to fly to the side,\npush the left stick left or right.",
     "When you want to fly to the side,\npush the stick left or right."},
    {"AT_30_13_020", "When you want to fly to the side", "When you want to fly to the side,\npress [left] or [right].",
     "When you want to fly to the side,\npush the left stick left or right.",
     "When you want to fly to the side,\npush the stick left or right."},
    {"AT_30_03_030", "When you want to fly up or down", "When you want to fly up or down,\npress [up] or [down].",
     "When you want to fly up or down,\npush the left stick up or down.", "When you want to fly up or down,\npush the stick up or down."},
    {"AT_30_13_030", "When you want to fly up or down", "When you want to fly up or down,\npress [up] or [down].",
     "When you want to fly up or down,\npush the left stick up or down.", "When you want to fly up or down,\npush the stick up or down."},
    {"AT_30_03_040", "Press ", "Press [2] while steering\nto move faster."},
    {"AT_30_13_040", "Press ", "Press [2] while steering\nto move faster."},
    /* The partners' moves, as each joins */
    {"C1146_SYSTEM_020", "Oshawott uses Water-type moves.",
     "Oshawott uses Water-type moves.\nHold down [1], then release it\nto use Water Gun. Hold\n[either shake] to use Razor Shell."},
    {"C3080_SYSTEM_010", "Snivy is good at jumping.",
     "Snivy is good at jumping.\nHold [2] to jump higher.[page]Snivy uses Grass-type moves.\nHold [1] to charge, and release it\n"
     "to use Leaf Storm. Hold\n[either shake] to use Leaf Tornado."},
    {"C4020_SYSTEM_010", "Tepig's Tackle is very powerful!",
     "Tepig's Tackle is very powerful!\nTry Dashing into a rock,\nand you might be able to break it.[page]"
     "Tepig uses Fire-type moves.\nHold, then release [1]\nto use Flamethrower. Hold\n[either shake] to use Heat Crash."},
    {"C3090_SYSTEM_020", "You can move this elevator",
     "You can move this elevator\nby using Snivy's Leaf Tornado.[page]Hold [either shake]\non the elevator\nto use Leaf Tornado."},
    {"T_SYSTEM_040", "You can break these blocks",
     "You can break these blocks by using\nTepig's Tackle or Heat Crash.[page]When you're beside the block,\n"
     "you can smash it with a Tackle.\nWhen you're above the block, hold\n[either shake] to use Heat Crash."},
    /* Iron Tail */
    {"C1090_SYSTEM_010", "In a battle with a different",
     "In a battle with a different\ncondition for winning, that\ncondition is shown on screen.\nAchieve the goal!\n"
     "Hold [either shake]\nto use Iron Tail."},
    /* Shake Sprint */
    {"T_SYSTEM_00_040", "Shake the Wii Remote",
     "Hold [either shake] when playing\nChase, and you'll do a special\nkind of Dash called a Shake Sprint.[page]"
     "But be careful--it's hard to turn\nwhen you're going full speed!"},
    /* The portals */
    {"T_FURIFURI_00_030", "You'll have to shake",
     "You'll have to shake more\nto open the portal. Keep holding\n[either shake] next time!"},
    {"T_FURIFURI_00_031", "You'll have to shake",
     "You'll have to shake more\nto open the portal. Keep holding\n[either shake] next time!"},
    {"T_FURIFURI_00_032", "You'll have to shake",
     "You'll have to shake a little more\nto open the portal. Keep holding\n[either shake] next time!"},
    {"T_FURIFURI_00_070", "Shake the Wii Remote", "Hold [either shake] to charge!"},
    /* Photos */
    {"T_SYSTEM_00_020", "Press ",
     "Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] to look\naround with the mouse, and then\n"
     "press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!",
     "Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] to look\naround with the right stick, and\n"
     "then press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!",
     "Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] and drag\non the screen to look around, then\n"
     "press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!"},
    /* Moving about, the first lesson */
    {"C0500_SYSTEM_010", "Go up to Piplup", "Go up to Piplup, and\npress [2] to talk to him.\nMove with [move].",
     "Go up to Piplup, and\npress [2] to talk to him.\nMove with the left stick.",
     "Go up to Piplup, and\npress [2] to talk to him.\nMove with the stick."},
    /* The title screen's Options */
    {"TITLE_01_070", "Change the rumble setting", "Change the rumble setting\nof the controller."},
};
#define TEXTS ((int)(sizeof(kText) / sizeof(kText[0])))

enum { MODE_KEY, MODE_PAD, MODE_TOUCH };

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

static uint32_t put_tag(uint32_t at, uint32_t end, uint16_t group, uint16_t type) {
    at = put(at, end, 0x0E);
    at = put(at, end, group);
    at = put(at, end, type);
    return put(at, end, 0); /* no parameters */
}

static void button_name(uint32_t button, int mode, char* out, int n) {
    *out = 0;
    if (mode == MODE_TOUCH) { /* the touch controls' labels (input_android.c) */
        snprintf(out, n, "%s", button == HOST_PAD_SHAKE ? "Shake" : button == HOST_PAD_SHAKE2 ? "Shake 2" : "the stick");
        return;
    }
    if (g_host_button_name) g_host_button_name(button, out, n);
    if (!*out) snprintf(out, n, "nothing"); /* unbound */
}

/* Writes the new text, as the controls now stand. */
static void write_all(void) {
    static const struct { const char* name; uint16_t group, type; } kTag[] = {
        {"[A]", 2, 1}, {"[B]", 2, 2}, {"[-]", 2, 5}, {"[1]", 2, 6}, {"[2]", 2, 7}, {"[page]", 0, 4},
    };
    static const struct { const char* name; uint32_t button; } kName[] = {
        {"[up]", HOST_PAD_SCREEN_UP}, {"[down]", HOST_PAD_SCREEN_DOWN},
        {"[left]", HOST_PAD_SCREEN_LEFT}, {"[right]", HOST_PAD_SCREEN_RIGHT},
    };
    char one[24], two[24], shake[64], either[64], move[112], k[4][24];
    /* The generation's low bit: the controller was used last (prompts.c). */
    int mode = g_host_prompt_generation && (g_host_prompt_generation() & 1) ? MODE_PAD : MODE_KEY, i, j;
    uint32_t at = sBase, end = sBase + TEXT_SIZE;
#ifdef __ANDROID__
    if (mode == MODE_KEY) mode = MODE_TOUCH; /* no keyboard: the touch controls */
#endif
    button_name(HOST_PAD_SHAKE, mode, one, sizeof(one));
    button_name(HOST_PAD_SHAKE2, mode, two, sizeof(two));
    snprintf(shake, sizeof(shake), "%s and %s", one, two);
    snprintf(either, sizeof(either), "%s or %s", one, two);
    for (j = 0; j < 4; j++) button_name(kName[j].button, mode, k[j], sizeof(k[j]));
    snprintf(move, sizeof(move), "%s, %s, %s and %s", k[0], k[2], k[1], k[3]);
    for (i = 0; i < TEXTS; i++) {
        const char* s = kText[i].key;
        if (mode != MODE_KEY && kText[i].pad) s = kText[i].pad;
        if (mode == MODE_TOUCH && kText[i].touch) s = kText[i].touch;
        sAt[i] = at;
        while (*s) {
            size_t t, n = 0;
            for (t = 0; t < sizeof(kTag) / sizeof(kTag[0]); t++) {
                if (!strncmp(s, kTag[t].name, n = strlen(kTag[t].name))) break;
            }
            if (t < sizeof(kTag) / sizeof(kTag[0])) {
                at = put_tag(at, end, kTag[t].group, kTag[t].type);
                s += n;
                continue;
            }
            for (t = 0; t < 4; t++) {
                if (!strncmp(s, kName[t].name, n = strlen(kName[t].name))) break;
            }
            if (t < 4) {
                at = put_str(at, end, k[t]);
                s += n;
            } else if (!strncmp(s, "[shake]", 7)) {
                at = put_str(at, end, shake);
                s += 7;
            } else if (!strncmp(s, "[either shake]", 14)) {
                at = put_str(at, end, either);
                s += 14;
            } else if (!strncmp(s, "[move]", 6)) {
                at = put_str(at, end, move);
                s += 6;
            } else {
                at = put(at, end, (uint8_t)*s++);
            }
        }
        at = put(at, end, 0);
        if (at > end) {
            HLE_WARN("text: no room for the instructions");
            sAt[i] = 0;
            at = end;
        }
    }
}

/* Whether the string at s (UTF-16) begins with prefix, after any tags. */
static int begins(uint32_t s, const char* prefix) {
    while (LD16(s) == 0x0E) s += 8 + LD16(s + 6);
    for (; *prefix; prefix++, s += 2) {
        if (LD16(s) != (uint8_t)*prefix) return 0;
    }
    return 1;
}

/* Whether text index of message data msg (LMS_InitMessage's: its blocks at
 * +0xC, 16 bytes each, data first and size at +8; LBL1's block at +0x10)
 * is labelled label. LBL1: a count of hash slots (8 bytes each), then
 * labels: a length byte, the name, the text index (u32). */
static int labelled(uint32_t msg, uint32_t index, const char* label) {
    int32_t block = (int32_t)LD32(msg + 0x10);
    uint32_t data, size, pos, len = (uint32_t)strlen(label), i;
    if (block < 0) return 0;
    data = LD32(LD32(msg + 0xC) + (uint32_t)block * 16);
    size = LD32(LD32(msg + 0xC) + (uint32_t)block * 16 + 8);
    for (pos = LD32(data) * 8 + 4; pos + 5 <= size; pos += LD8(data + pos) + 5) {
        uint32_t n = LD8(data + pos);
        if (LD32(data + pos + 1 + n) != index) continue;
        if (n != len) return 0;
        for (i = 0; i < n; i++) {
            if (LD8(data + pos + 1 + i) != (uint8_t)label[i]) return 0;
        }
        return 1;
    }
    return 0;
}

/* const u16* LMS_GetTextByIndex(MsgData* msg, int index) */
void f_803D7670(ppc_t* c) {
    uint32_t msg = c->r[3], index = c->r[4], gen;
    int i;
    f_803D7670_orig(c);
    if (!sBase || !c->r[3]) return;
    for (i = 0; i < TEXTS; i++) {
        if (!begins(c->r[3], kText[i].was) || !labelled(msg, index, kText[i].label)) continue;
        gen = g_host_prompt_generation ? g_host_prompt_generation() : 0;
        if (gen != sGeneration) {
            sGeneration = gen;
            write_all();
        }
        if (sAt[i]) c->r[3] = sAt[i];
        return;
    }
}
