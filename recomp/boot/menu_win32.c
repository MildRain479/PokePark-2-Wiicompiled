/* Settings (settings.ini) and the settings menu: display, mouse and
 * controls. Backspace, or Back+Start on a controller, opens and closes
 * it; the game keeps running behind it but gets no input. It is drawn
 * with GDI into an image that gl_backend.c lays over the picture: plain,
 * for testing (it can start any minigame). The same settings, but for
 * the debug ones, are in the Options tab of the pause menu
 * (options_win32.c).
 *
 *   Up/Down choose, Left/Right change, Enter rebinds (then press the key,
 *   mouse button or controller button; Esc cancels), Delete clears.
 *   On a controller: the d-pad or stick, A, X clears, B closes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_settings.h"

int hle_widescreen(void);
int display_scale(void); /* gl_backend.c: as applied */
int display_msaa(void);
void hle_set_frame_rate(int fps);   /* hle_game.c */
void gxb_set_output_rate(int fps);  /* gl_backend.c */
int gxb_display_hz(void);           /* gl_backend.c: the window's monitor's refresh rate */

/* The frame rates offered. Above 60 the game still draws 60 frames a
 * second and the ones between are interpolated (gl_backend.c). */
static const int kFrameRates[] = {30, 60, 120, 144, 165, 240};
#define FRAME_RATES ((int)(sizeof(kFrameRates) / sizeof(kFrameRates[0])))

static int valid_frame_rate(int fps) {
    int i;
    for (i = FRAME_RATES - 1; i > 0 && kFrameRates[i] > fps; i--) {}
    return kFrameRates[i];
}

void hle_set_fov(int percent); /* hle_camera.c */
void hle_request_minigame(int at); /* hle_game.c */
void hle_set_text_speed(int steps);  /* hle_modifiers.c */
void hle_set_berry_rate(int percent);
void hle_set_prices(int percent);

/* The gameplay settings' choices, in order. */
static const int kTextSpeeds[] = {1, 2, 3, 0};
static const int kBerries[] = {100, 150, 200, 300, 50};
static const int kPrices[] = {100, 50, 0, 200};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int choice(const int* list, int n, int v) {
    int i;
    for (i = 0; i < n && list[i] != v; i++) {}
    return i < n ? i : 0;
}

static void apply_gameplay(void) {
    hle_set_text_speed(g_settings.text_speed);
    hle_set_berry_rate(g_settings.berries);
    hle_set_prices(g_settings.prices);
}

/* The minigames, by At number, for the Debug section. */
static const struct {
    int at;
    const char* name;
} kMinigames[] = {
    {1, "Absol's Hurdle Bounce"},         {2, "Rayquaza's Balloon Panic"},   {3, "Venusaur's Vine Swing"},
    {4, "Tangrowth's Swing-Along"},       {5, "Dusknoir's Speed Slam"},      {6, "Gyarados' Aqua Dash"},
    {7, "Pelipper's Circle Circuit"},     {9, "Empoleon's Snow Slide"},      {10, "Bastiodon's Panel Crush"},
    {11, "Rhyperior's Bumper Burn"},      {12, "Blaziken's Boulder Bash"},   {13, "Rotom's Spooky Shoot-'em-Up"},
    {15, "Salamence's Air Race"},         {16, "Bulbasaur's Daring Dash"},
};
#define MINIGAMES ((int)(sizeof(kMinigames) / sizeof(kMinigames[0])))
static int sMinigame;
#define FOV_MIN 80
#define FOV_MAX 150

static void apply_frame_rate(void) {
    hle_set_frame_rate(g_settings.frame_rate);
    gxb_set_output_rate(g_settings.frame_rate);
}

HostSettings g_settings;

static const char* const kActionIni[ACT_COUNT] = {
    "up", "down", "left", "right", "two", "one", "a", "b", "plus", "minus", "home", "shake", "shake2",
};
static const char* const kActionLabel[ACT_COUNT] = {
    "Move up", "Move down", "Move left", "Move right", "2: jump, talk", "1: dash",
    "A: Thunderbolt", "B: recenter camera", "+", "-", "HOME", "Shake", "Shake 2 (minigames)",
};

void settings_default_controls(void) {
    static const int kKeys[ACT_COUNT][BIND_SLOTS] = {
        {'W', VK_UP}, {'S', VK_DOWN}, {'A', VK_LEFT}, {'D', VK_RIGHT},
        {VK_SPACE, 'X'}, {VK_LBUTTON, 'Z'}, {VK_RBUTTON, VK_RETURN}, {'V', VK_LCONTROL},
        {'P', 0}, {'M', 0}, {VK_HOME, 'H'}, {'Q', VK_LSHIFT}, {'E', 0},
    };
    static const uint32_t kPad[ACT_COUNT][BIND_SLOTS] = {
        {XPAD_UP, 0}, {XPAD_DOWN, 0}, {XPAD_LEFT, 0}, {XPAD_RIGHT, 0},
        {XPAD_A, 0}, {XPAD_X, 0}, {XPAD_B, 0}, {XPAD_Y, XPAD_RT},
        {XPAD_START, 0}, {XPAD_BACK, 0}, {XPAD_RTHUMB, 0}, {XPAD_LB, 0}, {XPAD_RB, 0},
    };
    memcpy(g_settings.key, kKeys, sizeof(kKeys));
    memcpy(g_settings.pad, kPad, sizeof(kPad));
}

static int valid_msaa(int n) { return n >= 8 ? 8 : n >= 4 ? 4 : n >= 2 ? 2 : 1; }
static int valid_aniso(int n) { return n >= 16 ? 16 : valid_msaa(n); }

/* ------------------------------------------------------------------------ */
/* Names */

void settings_key_name(int vk, char* out, size_t n) {
    static const struct { int vk; const char* name; } kNames[] = {
        {VK_LBUTTON, "Left click"}, {VK_RBUTTON, "Right click"}, {VK_MBUTTON, "Middle click"},
        {VK_XBUTTON1, "Mouse 4"}, {VK_XBUTTON2, "Mouse 5"},
        {VK_LCONTROL, "Left Ctrl"}, {VK_RCONTROL, "Right Ctrl"}, {VK_LSHIFT, "Left Shift"},
        {VK_RSHIFT, "Right Shift"}, {VK_LMENU, "Left Alt"}, {VK_RMENU, "Right Alt"},
    };
    /* Keys whose scan code needs the extended bit to be told apart. */
    static const int kExtended[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
                                    VK_INSERT, VK_DELETE, VK_DIVIDE, VK_NUMLOCK};
    size_t i;
    LONG lp;
    if (!vk) {
        snprintf(out, n, "-");
        return;
    }
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (kNames[i].vk == vk) {
            snprintf(out, n, "%s", kNames[i].name);
            return;
        }
    }
    lp = (LONG)(MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC) << 16);
    for (i = 0; i < sizeof(kExtended) / sizeof(kExtended[0]); i++) {
        if (kExtended[i] == vk) lp |= 1 << 24;
    }
    if (!GetKeyNameTextA(lp, out, (int)n)) snprintf(out, n, "Key %02X", vk);
}

const char* settings_pad_name(uint32_t b) {
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        {XPAD_UP, "D-pad up"}, {XPAD_DOWN, "D-pad down"}, {XPAD_LEFT, "D-pad left"}, {XPAD_RIGHT, "D-pad right"},
        {XPAD_START, "Start"}, {XPAD_BACK, "Back"}, {XPAD_LTHUMB, "L stick click"}, {XPAD_RTHUMB, "R stick click"},
        {XPAD_LB, "LB"}, {XPAD_RB, "RB"}, {XPAD_A, "A"}, {XPAD_B, "B"}, {XPAD_X, "X"}, {XPAD_Y, "Y"},
        {XPAD_LT, "LT"}, {XPAD_RT, "RT"},
    };
    size_t i;
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (kNames[i].bit == b) return kNames[i].name;
    }
    return "-";
}

void settings_binding_name(int act, int controller, char* out, size_t n) {
    int k;
    if (n) *out = 0;
    for (k = 0; k < BIND_SLOTS; k++) {
        if (controller && g_settings.pad[act][k]) {
            snprintf(out, n, "%s", settings_pad_name(g_settings.pad[act][k]));
            return;
        }
        if (!controller && g_settings.key[act][k]) {
            settings_key_name(g_settings.key[act][k], out, n);
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* settings.ini */

static char sPath[MAX_PATH];

static const char* settings_path(void) {
    if (!sPath[0]) {
        const char* e = getenv("RECOMP_SETTINGS");
        char* slash;
        if (e && *e) {
            snprintf(sPath, sizeof(sPath), "%s", e);
        } else {
            GetModuleFileNameA(NULL, sPath, sizeof(sPath) - 16);
            slash = strrchr(sPath, '\\');
            strcpy(slash ? slash + 1 : sPath, "settings.ini");
        }
    }
    return sPath;
}

static uint32_t sSaves;

uint32_t settings_generation(void) { return sSaves; }

void settings_save(void) {
    sSaves++;
    FILE* f = fopen(settings_path(), "w");
    int i;
    if (!f) return;
    fprintf(f, "; PokePark recomp settings, also changed in the settings menu (Backspace).\n\n");
    fprintf(f, "[display]\n");
    fprintf(f, "fullscreen = %d\n", g_settings.fullscreen);
    fprintf(f, "scale = %d ; internal resolution: 0 fits the window, or 1-%d\n", g_settings.scale, MAX_SCALE);
    fprintf(f, "msaa = %d ; anti-aliasing samples: 1 (off), 2, 4 or 8\n", g_settings.msaa);
    fprintf(f, "frame_rate = %d ; 30 (the game's own), 60, or 120, 144, 165, 240 (interpolated)\n",
            g_settings.frame_rate);
    fprintf(f, "fov = %d ; field of view in the park, percent of the game's: %d-%d\n", g_settings.fov, FOV_MIN,
            FOV_MAX);
    fprintf(f, "aniso = %d ; anisotropic texture filtering: 1 (off), 2, 4, 8 or 16\n\n", g_settings.aniso);
    fprintf(f, "[gameplay]\n");
    fprintf(f, "text_speed = %d ; dialogue: 1 (the game's), 2, 3 times as fast, or 0: a page at once\n",
            g_settings.text_speed);
    fprintf(f, "berries = %d ; berries collected, percent of the game's: 50, 100, 150, 200 or 300\n", g_settings.berries);
    fprintf(f, "prices = %d ; berries spent, percent of the game's: 0 (free), 50, 100 or 200\n\n", g_settings.prices);
    fprintf(f, "[mouse]\n");
    fprintf(f, "sensitivity = %.2f ; camera turn, times the default\n", g_settings.mouse_sensitivity);
    fprintf(f, "invert_tilt = %d\n\n", g_settings.invert_tilt);
    fprintf(f, "[keyboard] ; Windows virtual-key codes, two per button (0: none)\n");
    for (i = 0; i < ACT_COUNT; i++) {
        char a[32], b[32];
        settings_key_name(g_settings.key[i][0], a, sizeof(a));
        settings_key_name(g_settings.key[i][1], b, sizeof(b));
        fprintf(f, "%s = 0x%02X 0x%02X ; %s, %s\n", kActionIni[i], g_settings.key[i][0], g_settings.key[i][1], a, b);
    }
    fprintf(f, "\n[controller] ; XInput buttons, two per button (0x10000: LT, 0x20000: RT)\n");
    fprintf(f, "rumble = %d ; 0 off, 1 light, 2 normal, 3 strong\n", g_settings.rumble);
    for (i = 0; i < ACT_COUNT; i++) {
        fprintf(f, "%s = 0x%04X 0x%04X ; %s, %s\n", kActionIni[i], g_settings.pad[i][0], g_settings.pad[i][1],
                settings_pad_name(g_settings.pad[i][0]), settings_pad_name(g_settings.pad[i][1]));
    }
    fclose(f);
}

void settings_load(void) {
    char line[256], section[32] = "";
    int sawShake2 = 0, i; /* the second shake key's lines: 1 keyboard, 2 controller */
    FILE* f;
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.msaa = 1;
    g_settings.aniso = 1;
    g_settings.rumble = 2;
    g_settings.text_speed = 1;
    g_settings.berries = 100;
    g_settings.prices = 100;
    g_settings.frame_rate = 30;
    g_settings.fov = 100;
    g_settings.mouse_sensitivity = 1.0f;
    settings_default_controls();
    f = fopen(settings_path(), "r");
    if (!f) {
        settings_save();
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *c, *end, name[32];
        int i;
        unsigned long a, b;
        if ((c = strchr(line, ';'))) *c = '\0';
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            sscanf(p, "[%31[^]]", section);
            continue;
        }
        if (!(c = strchr(p, '='))) continue;
        *c = '\0';
        if (sscanf(p, "%31s", name) != 1) continue;
        p = c + 1;
        if (!strcmp(section, "display")) {
            if (!strcmp(name, "fullscreen")) g_settings.fullscreen = atoi(p) != 0;
            if (!strcmp(name, "scale")) g_settings.scale = atoi(p) < 0 ? 0 : atoi(p) > MAX_SCALE ? MAX_SCALE : atoi(p);
            if (!strcmp(name, "msaa")) g_settings.msaa = valid_msaa(atoi(p));
            if (!strcmp(name, "aniso")) g_settings.aniso = valid_aniso(atoi(p));
            if (!strcmp(name, "frame_rate")) g_settings.frame_rate = valid_frame_rate(atoi(p));
            if (!strcmp(name, "fov")) {
                int v = atoi(p) / 5 * 5;
                g_settings.fov = v < FOV_MIN ? FOV_MIN : v > FOV_MAX ? FOV_MAX : v;
            }
        } else if (!strcmp(section, "gameplay")) {
            if (!strcmp(name, "text_speed")) g_settings.text_speed = kTextSpeeds[choice(kTextSpeeds, COUNT(kTextSpeeds), atoi(p))];
            if (!strcmp(name, "berries")) g_settings.berries = kBerries[choice(kBerries, COUNT(kBerries), atoi(p))];
            if (!strcmp(name, "prices")) g_settings.prices = kPrices[choice(kPrices, COUNT(kPrices), atoi(p))];
        } else if (!strcmp(section, "mouse")) {
            if (!strcmp(name, "sensitivity")) {
                float s = (float)atof(p);
                g_settings.mouse_sensitivity = s < 0.1f ? 0.1f : s > 3.0f ? 3.0f : s;
            }
            if (!strcmp(name, "invert_tilt")) g_settings.invert_tilt = atoi(p) != 0;
        } else if (!strcmp(section, "controller") && !strcmp(name, "rumble")) {
            g_settings.rumble = atoi(p) < 0 ? 0 : atoi(p) > 3 ? 3 : atoi(p);
        } else if (!strcmp(section, "keyboard") || !strcmp(section, "controller")) {
            for (i = 0; i < ACT_COUNT; i++) {
                if (strcmp(name, kActionIni[i])) continue;
                if (i == ACT_SHAKE2) sawShake2 |= section[0] == 'k' ? 1 : 2;
                a = strtoul(p, &end, 0);
                b = strtoul(end, NULL, 0);
                if (section[0] == 'k') {
                    g_settings.key[i][0] = (int)(a & 0xFF);
                    g_settings.key[i][1] = (int)(b & 0xFF);
                } else {
                    g_settings.pad[i][0] = (uint32_t)a;
                    g_settings.pad[i][1] = (uint32_t)b;
                }
            }
        }
    }
    fclose(f);
    /* Files from before the second shake key gave its default (RB) to
     * shake: it moves over. */
    if (!(sawShake2 & 2)) {
        for (i = 0; i < ACT_COUNT; i++) {
            int k;
            for (k = 0; k < BIND_SLOTS; k++) {
                if (i != ACT_SHAKE2 && g_settings.pad[i][k] == g_settings.pad[ACT_SHAKE2][0]) g_settings.pad[i][k] = 0;
            }
        }
    }
    if (!(sawShake2 & 1)) {
        for (i = 0; i < ACT_COUNT; i++) {
            int k;
            for (k = 0; k < BIND_SLOTS; k++) {
                if (i != ACT_SHAKE2 && g_settings.key[i][k] == g_settings.key[ACT_SHAKE2][0]) g_settings.key[i][k] = 0;
            }
        }
    }
    apply_frame_rate();
    hle_set_fov(g_settings.fov);
    apply_gameplay();
}

/* ------------------------------------------------------------------------ */
/* The menu */

enum { ROW_FULLSCREEN, ROW_SCALE, ROW_MSAA, ROW_FPS, ROW_FOV, ROW_MINIGAME, ROW_SENSITIVITY, ROW_INVERT, ROW_BIND, ROW_RESET = ROW_BIND + ACT_COUNT, ROW_COUNT };
/* Columns of a binding row: two keyboard/mouse slots, two controller. */
#define COLS 4
enum { CAPTURE_NONE, CAPTURE_KEY, CAPTURE_PAD };

static int sOpen, sRow, sCol, sCapture;

int menu_is_open(void) { return sOpen || options_is_open(); }

static void set_open(int open) {
    sOpen = open;
    sCapture = CAPTURE_NONE;
}

static void move(int d) { sRow = (sRow + d + ROW_COUNT) % ROW_COUNT; }

void settings_step(int which, int d) {
    static const int kMsaa[] = {1, 2, 4, 8};
    int i;
    switch (which) {
    case SET_FULLSCREEN: g_settings.fullscreen = !g_settings.fullscreen; break;
    case SET_SCALE: g_settings.scale = (g_settings.scale + d + MAX_SCALE + 1) % (MAX_SCALE + 1); break;
    case SET_MSAA:
        for (i = 0; i < 4 && kMsaa[i] != g_settings.msaa; i++) {}
        g_settings.msaa = kMsaa[(i + d + 4) % 4];
        break;
    case SET_FRAME_RATE:
        for (i = 0; i < FRAME_RATES && kFrameRates[i] != g_settings.frame_rate; i++) {}
        g_settings.frame_rate = kFrameRates[(i + d + FRAME_RATES) % FRAME_RATES];
        apply_frame_rate();
        break;
    case SET_FOV:
        g_settings.fov = g_settings.fov + 5 * d < FOV_MIN ? FOV_MIN : g_settings.fov + 5 * d > FOV_MAX ? FOV_MAX : g_settings.fov + 5 * d;
        hle_set_fov(g_settings.fov);
        break;
    case SET_SENSITIVITY: {
        int tenths = (int)(g_settings.mouse_sensitivity * 10 + 0.5f) + d;
        g_settings.mouse_sensitivity = (tenths < 1 ? 1 : tenths > 30 ? 30 : tenths) / 10.0f;
        break;
    }
    case SET_INVERT: g_settings.invert_tilt = !g_settings.invert_tilt; break;
    case SET_ANISO: {
        static const int kAniso[] = {1, 2, 4, 8, 16};
        for (i = 0; i < 5 && kAniso[i] != g_settings.aniso; i++) {}
        g_settings.aniso = kAniso[(i + d + 5) % 5];
        break;
    }
    case SET_RUMBLE:
        g_settings.rumble = (g_settings.rumble + d + 4) % 4;
        input_rumble_preview();
        break;
    case SET_TEXT_SPEED:
        g_settings.text_speed = kTextSpeeds[(choice(kTextSpeeds, COUNT(kTextSpeeds), g_settings.text_speed) + d +
                                             COUNT(kTextSpeeds)) % COUNT(kTextSpeeds)];
        apply_gameplay();
        break;
    case SET_BERRIES:
        g_settings.berries =
            kBerries[(choice(kBerries, COUNT(kBerries), g_settings.berries) + d + COUNT(kBerries)) % COUNT(kBerries)];
        apply_gameplay();
        break;
    case SET_PRICES:
        g_settings.prices =
            kPrices[(choice(kPrices, COUNT(kPrices), g_settings.prices) + d + COUNT(kPrices)) % COUNT(kPrices)];
        apply_gameplay();
        break;
    default: return;
    }
    settings_save();
}

static void change(int d) {
    static const int kStep[] = {SET_FULLSCREEN, SET_SCALE, SET_MSAA, SET_FRAME_RATE, SET_FOV, -1, SET_SENSITIVITY, SET_INVERT};
    if (sRow == ROW_MINIGAME) {
        sMinigame = (sMinigame + d + MINIGAMES) % MINIGAMES;
    } else if (sRow < ROW_BIND) {
        settings_step(kStep[sRow], d);
    } else if (sRow < ROW_RESET) { /* a binding row: moves between its columns */
        sCol = sCol + d < 0 ? 0 : sCol + d >= COLS ? COLS - 1 : sCol + d;
    }
}

static void activate(void) {
    if (sRow >= ROW_BIND && sRow < ROW_RESET) {
        sCapture = sCol < BIND_SLOTS ? CAPTURE_KEY : CAPTURE_PAD;
    } else if (sRow == ROW_MINIGAME) {
        hle_request_minigame(kMinigames[sMinigame].at);
        set_open(0);
    } else if (sRow == ROW_RESET) {
        settings_default_controls();
        settings_save();
    } else {
        change(1);
    }
}

static void clear(void) {
    if (sRow < ROW_BIND || sRow >= ROW_RESET) return;
    if (sCol < BIND_SLOTS) g_settings.key[sRow - ROW_BIND][sCol] = 0;
    else g_settings.pad[sRow - ROW_BIND][sCol - BIND_SLOTS] = 0;
    settings_save();
}

void settings_bind(int act, int slot, int vk, uint32_t pad) {
    int i, k;
    for (i = 0; i < ACT_COUNT; i++) {
        for (k = 0; k < BIND_SLOTS; k++) {
            if (vk && g_settings.key[i][k] == vk) g_settings.key[i][k] = 0;
            if (pad && g_settings.pad[i][k] == pad) g_settings.pad[i][k] = 0;
        }
    }
    if (vk) g_settings.key[act][slot] = vk;
    else g_settings.pad[act][slot] = pad;
    settings_save();
}

/* Binds to the selected slot. */
static void bind(int vk, uint32_t pad) {
    settings_bind(sRow - ROW_BIND, vk ? sCol : sCol - BIND_SLOTS, vk, pad);
    sCapture = CAPTURE_NONE;
}

/* The key a WM_KEYDOWN means, with left and right modifiers told apart. */
static int key_of(WPARAM wp, LPARAM lp) {
    int extended = (int)((lp >> 24) & 1);
    if (wp == VK_SHIFT) return (int)MapVirtualKeyA((UINT)((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX);
    if (wp == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (wp == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    return (int)wp;
}

int menu_window_message(void* wnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, repeat = down && (lp & (1 << 30));
    (void)wnd;
    if (!sOpen && options_window_message(msg, wp, lp)) return 1;
    if (msg == WM_KEYDOWN && wp == VK_BACK && sCapture == CAPTURE_NONE) {
        if (!repeat) set_open(!sOpen);
        return 1;
    }
    if (!sOpen) return 0;
    if (sCapture != CAPTURE_NONE) {
        int vk = 0;
        if (down && !repeat) vk = key_of(wp, lp);
        if (msg == WM_LBUTTONDOWN) vk = VK_LBUTTON;
        if (msg == WM_RBUTTONDOWN) vk = VK_RBUTTON;
        if (msg == WM_MBUTTONDOWN) vk = VK_MBUTTON;
        if (msg == WM_XBUTTONDOWN) vk = HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        if (vk == VK_ESCAPE || vk == VK_BACK) sCapture = CAPTURE_NONE;
        else if (vk && sCapture == CAPTURE_KEY) bind(vk, 0);
        return down || vk;
    }
    if (!down) return 0;
    switch (wp) {
    case VK_ESCAPE: set_open(0); break;
    case VK_UP: move(-1); break;
    case VK_DOWN: move(1); break;
    case VK_LEFT: change(-1); break;
    case VK_RIGHT: change(1); break;
    case VK_RETURN: case VK_SPACE: if (!repeat) activate(); break;
    case VK_DELETE: clear(); break;
    }
    return 1;
}

void menu_controller(uint32_t raw, uint32_t nav) {
    static uint32_t lastRaw, lastNav;
    const uint32_t both = XPAD_BACK | XPAD_START;
    uint32_t pressed = raw & ~lastRaw, navPressed = nav & ~lastNav;
    lastRaw = raw;
    lastNav = nav;
    if ((raw & both) == both && (pressed & both) && !options_is_open()) {
        set_open(!sOpen);
        return;
    }
    if (!sOpen) {
        options_controller(raw, nav);
        return;
    }
    if (sCapture == CAPTURE_PAD) {
        if (pressed) bind(0, pressed & (0u - pressed)); /* the lowest one */
        return;
    }
    if (sCapture != CAPTURE_NONE) return;
    if (navPressed & XPAD_UP) move(-1);
    if (navPressed & XPAD_DOWN) move(1);
    if (navPressed & XPAD_LEFT) change(-1);
    if (navPressed & XPAD_RIGHT) change(1);
    if (pressed & XPAD_A) activate();
    if (pressed & XPAD_X) clear();
    if (pressed & XPAD_B) set_open(0);
}

/* ------------------------------------------------------------------------ */
/* Drawing */

static HDC sDC;
static HBITMAP sBitmap;
static uint32_t* sPixels;
static int sW, sH, sLine;
static HFONT sFont, sBold;

static const COLORREF kText = RGB(235, 235, 240), kDim = RGB(140, 145, 160), kHead = RGB(120, 170, 255),
                      kWait = RGB(255, 210, 80), kPanel = RGB(22, 24, 30), kRow = RGB(44, 52, 72),
                      kCell = RGB(70, 90, 140);

static void text(int x, int y, COLORREF color, const char* s) {
    SetTextColor(sDC, color);
    TextOutA(sDC, x, y, s, (int)strlen(s));
}

static void fill(int x, int y, int w, int h, COLORREF color) {
    RECT r;
    HBRUSH b = CreateSolidBrush(color);
    SetRect(&r, x, y, x + w, y + h);
    FillRect(sDC, &r, b);
    DeleteObject(b);
}

static void setup(int w, int h) {
    BITMAPINFO bi;
    int px = h / 44 < 12 ? 12 : h / 44;
    if (!sDC) sDC = CreateCompatibleDC(NULL);
    if (sBitmap) DeleteObject(sBitmap);
    if (sFont) DeleteObject(sFont);
    if (sBold) DeleteObject(sBold);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top row first */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    sBitmap = CreateDIBSection(sDC, &bi, DIB_RGB_COLORS, (void**)&sPixels, NULL, 0);
    SelectObject(sDC, sBitmap);
    sFont = CreateFontA(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    sBold = CreateFontA(-px * 3 / 2, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    SetBkMode(sDC, TRANSPARENT);
    sW = w;
    sH = h;
    sLine = px * 3 / 2;
}

static void value_text(int row, char* out, size_t n) {
    int s, ms;
    switch (row) {
    case ROW_FULLSCREEN: snprintf(out, n, g_settings.fullscreen ? "On (Alt+Enter)" : "Off (Alt+Enter)"); break;
    case ROW_SCALE:
        s = display_scale();
        if (g_settings.scale) s = g_settings.scale;
        snprintf(out, n, "%s%dx: %d x %d%s", g_settings.scale ? "" : "Fit the window (", s,
                 (int)(640 * s * (hle_widescreen() ? 4.0 / 3.0 : 1.0) + 0.5), 456 * s, g_settings.scale ? "" : ")");
        break;
    case ROW_MSAA:
        ms = display_msaa();
        if (g_settings.msaa == 1) snprintf(out, n, "Off");
        else if (ms < g_settings.msaa) snprintf(out, n, "%dx MSAA (this GPU does %dx)", g_settings.msaa, ms);
        else snprintf(out, n, "%dx MSAA", g_settings.msaa);
        break;
    case ROW_FPS: {
        int hz = gxb_display_hz();
        if (g_settings.frame_rate > 60 && hz >= 30 && hz < g_settings.frame_rate)
            snprintf(out, n, "%d fps (interpolated; display shows %d)", g_settings.frame_rate, hz);
        else
            snprintf(out, n, "%d fps%s", g_settings.frame_rate,
                     g_settings.frame_rate == 30 ? " (original)" : g_settings.frame_rate > 60 ? " (interpolated)" : "");
        break;
    }
    case ROW_MINIGAME: snprintf(out, n, "%s (Enter, from the park)", kMinigames[sMinigame].name); break;
    case ROW_FOV: snprintf(out, n, g_settings.fov == 100 ? "100%% (original)" : "%d%%", g_settings.fov); break;
    case ROW_SENSITIVITY: snprintf(out, n, "%.1f", g_settings.mouse_sensitivity); break;
    case ROW_INVERT: snprintf(out, n, g_settings.invert_tilt ? "On" : "Off"); break;
    default: out[0] = '\0';
    }
}

const uint32_t* menu_draw(int w, int h) {
    static const char* const kSettingLabel[] = {"Fullscreen", "Internal resolution", "Anti-aliasing", "Frame rate", "Field of view", "Test minigame",
                                                "Camera sensitivity", "Invert camera tilt"};
    int pw, ph, x0, y0, x, y, i, k, cell, valueX;
    char buf[64];
    if (!sOpen) return options_draw(w, h);
    if (w <= 0 || h <= 0) return NULL;
    if (w != sW || h != sH) setup(w, h);
    memset(sPixels, 0, (size_t)w * h * 4);
    pw = sLine * 30 < w - 2 * sLine ? sLine * 30 : w - 2 * sLine;
    ph = sLine * 32;
    x0 = (w - pw) / 2;
    y0 = (h - ph) / 2 < 0 ? 0 : (h - ph) / 2;
    fill(x0, y0, pw, ph, kPanel);
    x = x0 + sLine;
    y = y0 + sLine / 2;
    cell = (pw - 2 * sLine) * 16 / 100;
    valueX = x + (pw - 2 * sLine) * 36 / 100;

    SelectObject(sDC, sBold);
    text(x, y, kText, "Settings");
    SelectObject(sDC, sFont);
    y += sLine * 2;

    for (i = 0; i < ROW_COUNT; i++) {
        if (i == ROW_FULLSCREEN || i == ROW_MINIGAME || i == ROW_SENSITIVITY || i == ROW_BIND) {
            text(x, y, kHead, i == ROW_FULLSCREEN ? "Display" : i == ROW_MINIGAME ? "Debug" : i == ROW_SENSITIVITY ? "Mouse" : "Controls");
            if (i == ROW_BIND) {
                text(valueX, y, kDim, "Keyboard and mouse");
                text(valueX + 2 * cell, y, kDim, "Controller");
            }
            y += sLine;
        }
        if (i == ROW_RESET) y += sLine / 2;
        if (i == sRow) fill(x0 + sLine / 2, y - sLine / 8, pw - sLine, sLine, kRow);
        if (i < ROW_BIND) {
            text(x, y, kText, kSettingLabel[i]);
            value_text(i, buf, sizeof(buf));
            text(valueX, y, kText, buf);
        } else if (i < ROW_RESET) {
            int a = i - ROW_BIND;
            text(x, y, kText, kActionLabel[a]);
            for (k = 0; k < COLS; k++) {
                int cx = valueX + k * cell;
                if (i == sRow && k == sCol) fill(cx - sLine / 4, y - sLine / 8, cell, sLine, kCell);
                if (i == sRow && k == sCol && sCapture != CAPTURE_NONE) {
                    text(cx, y, kWait, sCapture == CAPTURE_KEY ? "Press a key..." : "Press a button...");
                    continue;
                }
                if (k < BIND_SLOTS) settings_key_name(g_settings.key[a][k], buf, sizeof(buf));
                else snprintf(buf, sizeof(buf), "%s", settings_pad_name(g_settings.pad[a][k - BIND_SLOTS]));
                text(cx, y, strcmp(buf, "-") ? kText : kDim, buf);
            }
        } else {
            text(x, y, kText, "Reset controls to defaults");
        }
        y += sLine;
    }
    y += sLine / 2;
    text(x, y, kDim, "Up/Down: choose   Left/Right: change   Enter: rebind   Delete: clear   Backspace: close");
    y += sLine;
    text(x, y, kDim, "Controller: d-pad, A, X clears, B closes; Back+Start opens and closes");
    GdiFlush();

    /* The panel nearly opaque, the picture around it dimmed. */
    for (k = 0; k < h; k++) {
        uint32_t* row = sPixels + (size_t)k * w;
        int inY = k >= y0 && k < y0 + ph;
        for (i = 0; i < w; i++) {
            uint32_t a = inY && i >= x0 && i < x0 + pw ? 240u : 150u;
            row[i] = (row[i] & 0xFFFFFFu) | a << 24;
        }
    }
    return sPixels;
}
