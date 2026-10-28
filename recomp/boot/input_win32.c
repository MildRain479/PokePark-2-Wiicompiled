/* Wii Remote 0 from the keyboard and the first XInput controller, for
 * hle_wpad.c. PokePark is played with the remote held sideways, like a
 * classic pad: the d-pad moves, 1 and 2 are the face buttons.
 *
 *                    keyboard              controller
 *   d-pad            arrows / WASD         d-pad / left stick
 *   2                X / K / Space         A
 *   1                Z / J                 X
 *   A                Enter / C             B
 *   B                V / Left Ctrl         Y / right trigger
 *   +                P                     Start
 *   -                M                     Back
 *   HOME             Home / H              right stick click
 *   shake            Q / Left Shift        LB / RB
 *
 * The keyboard counts only while the game's window has focus. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "host_input.h"

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

enum {
    XPAD_UP = 0x0001, XPAD_DOWN = 0x0002, XPAD_LEFT = 0x0004, XPAD_RIGHT = 0x0008,
    XPAD_START = 0x0010, XPAD_BACK = 0x0020, XPAD_RTHUMB = 0x0080,
    XPAD_LB = 0x0100, XPAD_RB = 0x0200, XPAD_A = 0x1000, XPAD_B = 0x2000, XPAD_X = 0x4000, XPAD_Y = 0x8000,
};

static DWORD(WINAPI* pXInputGetState)(DWORD, XPadState*);
static DWORD(WINAPI* pXInputSetState)(DWORD, XPadVibration*);

static int has_focus(void) {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static uint32_t keyboard(void) {
    static const struct { int vk; uint32_t bit; } kKeys[] = {
        {VK_UP, HOST_PAD_SCREEN_UP},       {'W', HOST_PAD_SCREEN_UP},
        {VK_DOWN, HOST_PAD_SCREEN_DOWN},   {'S', HOST_PAD_SCREEN_DOWN},
        {VK_LEFT, HOST_PAD_SCREEN_LEFT},   {'A', HOST_PAD_SCREEN_LEFT},
        {VK_RIGHT, HOST_PAD_SCREEN_RIGHT}, {'D', HOST_PAD_SCREEN_RIGHT},
        {'X', HOST_PAD_2}, {'K', HOST_PAD_2}, {VK_SPACE, HOST_PAD_2},
        {'Z', HOST_PAD_1}, {'J', HOST_PAD_1},
        {VK_RETURN, HOST_PAD_A}, {'C', HOST_PAD_A},
        {'V', HOST_PAD_B}, {VK_LCONTROL, HOST_PAD_B},
        {'P', HOST_PAD_PLUS}, {'M', HOST_PAD_MINUS},
        {VK_HOME, HOST_PAD_HOME}, {'H', HOST_PAD_HOME},
        {'Q', HOST_PAD_SHAKE}, {VK_LSHIFT, HOST_PAD_SHAKE},
    };
    uint32_t held = 0;
    size_t i;
    if (!has_focus()) return 0;
    for (i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
        if (GetAsyncKeyState(kKeys[i].vk) & 0x8000) held |= kKeys[i].bit;
    }
    return held;
}

static uint32_t controller(void) {
    static const struct { WORD xpad; uint32_t bit; } kButtons[] = {
        {XPAD_UP, HOST_PAD_SCREEN_UP}, {XPAD_DOWN, HOST_PAD_SCREEN_DOWN},
        {XPAD_LEFT, HOST_PAD_SCREEN_LEFT}, {XPAD_RIGHT, HOST_PAD_SCREEN_RIGHT},
        {XPAD_A, HOST_PAD_2}, {XPAD_X, HOST_PAD_1}, {XPAD_B, HOST_PAD_A}, {XPAD_Y, HOST_PAD_B},
        {XPAD_START, HOST_PAD_PLUS}, {XPAD_BACK, HOST_PAD_MINUS}, {XPAD_RTHUMB, HOST_PAD_HOME},
        {XPAD_LB, HOST_PAD_SHAKE}, {XPAD_RB, HOST_PAD_SHAKE},
    };
    /* Half the stick's travel, then the d-pad direction. */
    const SHORT kStick = 16384;
    XPadState s;
    uint32_t held = 0;
    size_t i;
    if (!pXInputGetState || pXInputGetState(0, &s) != ERROR_SUCCESS) return 0;
    for (i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
        if (s.Gamepad.wButtons & kButtons[i].xpad) held |= kButtons[i].bit;
    }
    if (s.Gamepad.bRightTrigger > 128) held |= HOST_PAD_B;
    if (s.Gamepad.sThumbLY > kStick) held |= HOST_PAD_SCREEN_UP;
    if (s.Gamepad.sThumbLY < -kStick) held |= HOST_PAD_SCREEN_DOWN;
    if (s.Gamepad.sThumbLX < -kStick) held |= HOST_PAD_SCREEN_LEFT;
    if (s.Gamepad.sThumbLX > kStick) held |= HOST_PAD_SCREEN_RIGHT;
    return held;
}

static int read_pad(int chan, uint32_t* held) {
    if (chan != 0) return 0;
    *held = keyboard() | controller();
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
}
