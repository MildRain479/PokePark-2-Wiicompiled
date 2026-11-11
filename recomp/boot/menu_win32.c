/* The settings menu, in builds but public ones (RECOMP_DEBUG_MENU,
 * tools/recomp/build.py): display, mouse and controls (settings.c), plain,
 * for testing (it can start any minigame). Backspace, or
 * Back+Start on a controller, opens and closes it; the game keeps running
 * behind it but gets no input. It is drawn with GDI into an image the
 * device lays over the picture. Public builds go straight to the Options
 * tab.
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
int display_scale(void); /* gx_frames.c: as applied */
int display_msaa(void);
int gxb_display_hz(void); /* window_win32.c: the window's monitor's refresh rate */
void hle_request_minigame(int at); /* hle_game.c */

/* ------------------------------------------------------------------------ */
/* The menu */

#ifdef RECOMP_DEBUG_MENU

static const char* const kActionLabel[ACT_COUNT] = {
    "Move up", "Move down", "Move left", "Move right", "2: jump, talk", "1: dash, attack",
    "A: call a friend", "B: look around, recenter camera", "+: PokePark Pad", "-: Photos", "HOME", "Shake", "Shake 2",
};

/* The minigames, by At number, for the Debug section. */
static const struct {
    int at;
    const char* name;
} kMinigames[] = {
    {1, "Cake Contraption"}, {2, "Dance Inferno"},
    {3, "Power Bash"}, {4, "Flight of Fancy"},
};
#define MINIGAMES ((int)(sizeof(kMinigames) / sizeof(kMinigames[0])))
static int sMinigame;

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

#else

/* Without the menu (public builds): the Options tab alone. */
int menu_is_open(void) { return options_is_open(); }

int menu_window_message(void* wnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    (void)wnd;
    return options_window_message(msg, wp, lp);
}

void menu_controller(uint32_t raw, uint32_t nav) { options_controller(raw, nav); }

const uint32_t* menu_draw(int w, int h) { return options_draw(w, h); }

#endif
