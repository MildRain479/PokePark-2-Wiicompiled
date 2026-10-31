/* Wii Remote 0 from the keyboard and the first XInput controller, for
 * hle_wpad.c. PokePark is played with the remote held sideways, like a
 * classic pad: the d-pad moves, 1 and 2 are the face buttons. By default:
 *
 *                    keyboard / mouse           controller
 *   d-pad (move)     WASD / arrows              d-pad / left stick
 *   2 (jump)         Space / X                  A
 *   1 (dash)         left click / Z             X
 *   A (Thunderbolt)  right click / Enter        B
 *   B                V / Left Ctrl              Y / right trigger
 *   +                P                          Start
 *   -                M                          Back
 *   HOME             Home / H                   right stick click
 *   camera           mouse                      right stick
 *   shake (held)     Q / Left Shift             LB
 *   shake 2          E                          RB
 *   (in minigames a shake is shake and shake 2 pressed by turns, hle_wpad.c)
 *
 * The keyboard counts only while the game's window has focus. While it
 * has focus the mouse is captured: hidden, held in the window, and its
 * movement turns the camera (hle_camera.c). Esc lets it go; a click in
 * the window captures it again. Mouse buttons count only while captured,
 * and not the click that captures it. The settings menu (menu_win32.c)
 * changes the bindings; while it is open the game gets no input. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "host_input.h"
#include "host_settings.h"

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XPadGamepad;
typedef struct {
    DWORD dwPacketNumber;
    XPadGamepad Gamepad;
} XPadState;
typedef struct {
    WORD wLeftMotorSpeed, wRightMotorSpeed;
} XPadVibration;

static DWORD(WINAPI* pXInputGetState)(DWORD, XPadState*);
static DWORD(WINAPI* pXInputSetState)(DWORD, XPadVibration*);

static int has_focus(void) {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static int sCaptured, sReleased, sSwallow, sButtonsLive;
static POINT sCenter;
static float sMouseX, sMouseY, sPointerX, sPointerY;

/* Keeps the cursor in the window while captured, checked on every poll:
 * going fullscreen moves the window. */
static void capture(int on) {
    if (on) {
        RECT r;
        POINT c;
        HWND w = GetForegroundWindow();
        GetClientRect(w, &r);
        MapWindowPoints(w, NULL, (POINT*)&r, 2);
        c.x = (r.left + r.right) / 2;
        c.y = (r.top + r.bottom) / 2;
        if (!sCaptured || c.x != sCenter.x || c.y != sCenter.y) {
            ClipCursor(&r);
            sCenter = c;
            SetCursorPos(sCenter.x, sCenter.y);
        }
        if (!sCaptured) while (ShowCursor(FALSE) >= 0) {}
    } else if (sCaptured) {
        ClipCursor(NULL);
        while (ShowCursor(TRUE) < 0) {}
    }
    sCaptured = on;
}

/* Captures or releases the mouse and collects its movement; works out
 * whether its buttons count (sButtonsLive). */
static void mouse(void) {
    int left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    POINT p;
    sButtonsLive = 0;
    if (!has_focus() || menu_is_open()) {
        capture(0);
        return;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) sReleased = 1;
    if (sReleased && (left || right)) {
        HWND w = GetForegroundWindow();
        RECT r;
        GetCursorPos(&p);
        ScreenToClient(w, &p);
        GetClientRect(w, &r);
        if (PtInRect(&r, p)) sReleased = 0, sSwallow = 1;
    }
    capture(!sReleased);
    if (!sCaptured) return;
    GetCursorPos(&p);
    sMouseX += (float)(p.x - sCenter.x);
    sPointerX += (float)(p.x - sCenter.x);
    sPointerY += (float)(p.y - sCenter.y);
    sMouseY += (float)(p.y - sCenter.y);
    if (p.x != sCenter.x || p.y != sCenter.y) SetCursorPos(sCenter.x, sCenter.y);
    if (sSwallow) {
        if (left || right) return;
        sSwallow = 0;
    }
    sButtonsLive = 1;
}

static void read_pointer(float* dx, float* dy) {
    *dx = sPointerX * g_settings.mouse_sensitivity;
    *dy = sPointerY * g_settings.mouse_sensitivity;
    sPointerX = sPointerY = 0;
}

/* A stick axis, -32768 to 32767, as -1 to 1 past a dead zone. */
static float stick_axis(SHORT v) {
    const float kDead = 7849.0f; /* XInput's own suggestion for the left stick */
    float f = (float)v;
    if (f > kDead) return (f - kDead) / (32767.0f - kDead);
    if (f < -kDead) return (f + kDead) / (32768.0f - kDead);
    return 0.0f;
}

static void read_sticks(float* lx, float* ly, float* rx, float* ry) {
    XPadState s;
    *lx = *ly = *rx = *ry = 0;
    if (!pXInputGetState || pXInputGetState(0, &s) != ERROR_SUCCESS || menu_is_open()) return;
    *lx = stick_axis(s.Gamepad.sThumbLX);
    *ly = stick_axis(s.Gamepad.sThumbLY);
    *rx = stick_axis(s.Gamepad.sThumbRX);
    *ry = stick_axis(s.Gamepad.sThumbRY);
}

/* The right stick turns the camera as the mouse does: full over turns it
 * as this many pixels a second would (180 degrees a second), up looking
 * up. */
#define LOOK_STICK_PIXELS_PER_SECOND 600.0f

/* The camera's turn since the last call (hle_camera.c), in mouse pixels:
 * the mouse's movement and the right stick's push over the time. */
static void read_mouse(float* dx, float* dy) {
    static LARGE_INTEGER last;
    LARGE_INTEGER now, freq;
    float lx, ly, rx, ry, dt = 0;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (last.QuadPart) dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
    if (dt > 0.1f) dt = 0.1f; /* not a jump after a stall */
    last = now;
    read_sticks(&lx, &ly, &rx, &ry);
    if (!has_focus()) rx = ry = 0;
    *dx = sMouseX * g_settings.mouse_sensitivity + rx * LOOK_STICK_PIXELS_PER_SECOND * dt;
    *dy = (sMouseY * g_settings.mouse_sensitivity - ry * LOOK_STICK_PIXELS_PER_SECOND * dt) * (g_settings.invert_tilt ? -1 : 1);
    sMouseX = sMouseY = 0;
}

static int is_mouse_button(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

/* The pad bit of each action (ACT_*). */
static const uint32_t kActionBit[ACT_COUNT] = {
    HOST_PAD_SCREEN_UP, HOST_PAD_SCREEN_DOWN, HOST_PAD_SCREEN_LEFT, HOST_PAD_SCREEN_RIGHT,
    HOST_PAD_2, HOST_PAD_1, HOST_PAD_A, HOST_PAD_B, HOST_PAD_PLUS, HOST_PAD_MINUS, HOST_PAD_HOME, HOST_PAD_SHAKE, HOST_PAD_SHAKE2,
};

static uint32_t keyboard(void) {
    uint32_t held = 0;
    int i, k;
    if (!has_focus()) return 0;
    for (i = 0; i < ACT_COUNT; i++) {
        for (k = 0; k < BIND_SLOTS; k++) {
            int vk = g_settings.key[i][k];
            if (vk && (!is_mouse_button(vk) || sButtonsLive) && (GetAsyncKeyState(vk) & 0x8000)) held |= kActionBit[i];
        }
    }
    return held;
}

/* Half the stick's travel, then the d-pad direction. */
#define STICK_THRESHOLD 16384

/* The controller's buttons (XPAD_*), and separately the directions the
 * left stick is pushed, as d-pad bits. Read once per poll: asking about
 * a controller that isn't plugged in takes milliseconds, and the menu
 * gets the same reading. */
static void controller_state(uint32_t* buttons, uint32_t* stick) {
    XPadState s;
    *buttons = *stick = 0;
    if (!pXInputGetState || pXInputGetState(0, &s) != ERROR_SUCCESS) return;
    *buttons = s.Gamepad.wButtons;
    if (s.Gamepad.bLeftTrigger > 128) *buttons |= XPAD_LT;
    if (s.Gamepad.bRightTrigger > 128) *buttons |= XPAD_RT;
    if (s.Gamepad.sThumbLY > STICK_THRESHOLD) *stick |= XPAD_UP;
    if (s.Gamepad.sThumbLY < -STICK_THRESHOLD) *stick |= XPAD_DOWN;
    if (s.Gamepad.sThumbLX < -STICK_THRESHOLD) *stick |= XPAD_LEFT;
    if (s.Gamepad.sThumbLX > STICK_THRESHOLD) *stick |= XPAD_RIGHT;
}

/* The left stick always moves, besides what the d-pad is bound to. */
static uint32_t controller(uint32_t b, uint32_t stick) {
    uint32_t held = 0;
    int i, k;
    for (i = 0; i < ACT_COUNT; i++) {
        for (k = 0; k < BIND_SLOTS; k++) {
            if (b & g_settings.pad[i][k]) held |= kActionBit[i];
        }
    }
    if (stick & XPAD_UP) held |= HOST_PAD_SCREEN_UP;
    if (stick & XPAD_DOWN) held |= HOST_PAD_SCREEN_DOWN;
    if (stick & XPAD_LEFT) held |= HOST_PAD_SCREEN_LEFT;
    if (stick & XPAD_RIGHT) held |= HOST_PAD_SCREEN_RIGHT;
    return held;
}

/* While the menu is open, and after it closes until everything held is
 * let go, nothing reaches the game: not the press that closed it. */
static int sControllerLast;

int input_controller_last(void) { return sControllerLast; }

static int read_pad(int chan, uint32_t* held) {
    static int menuHeld;
    uint32_t buttons, stick, keys, pad;
    if (chan != 0) return 0;
    mouse();
    controller_state(&buttons, &stick);
    menu_controller(buttons, buttons | stick);
    keys = keyboard();
    pad = controller(buttons, stick);
    if (pad) sControllerLast = 1;
    else if (keys) sControllerLast = 0;
    *held = keys | pad;
    if (menu_is_open()) menuHeld = 1;
    else if (menuHeld && !*held) menuHeld = 0;
    if (menuHeld) *held = 0;
    return 1;
}

static void rumble(int chan, int on) {
    XPadVibration v;
    if (chan != 0 || !pXInputSetState) return;
    v.wLeftMotorSpeed = v.wRightMotorSpeed = on ? 0x6000 : 0;
    pXInputSetState(0, &v);
}

void input_init(void) {
    HMODULE x = LoadLibraryA("xinput1_4.dll");
    if (!x) x = LoadLibraryA("xinput9_1_0.dll");
    if (x) {
        pXInputGetState = (DWORD(WINAPI*)(DWORD, XPadState*))(void*)GetProcAddress(x, "XInputGetState");
        pXInputSetState = (DWORD(WINAPI*)(DWORD, XPadVibration*))(void*)GetProcAddress(x, "XInputSetState");
    }
    g_host_pad_read = read_pad;
    g_host_pad_rumble = rumble;
    g_host_mouse_read = read_mouse;
    prompts_init();
    g_host_pointer_read = read_pointer;
    g_host_sticks_read = read_sticks;
}
