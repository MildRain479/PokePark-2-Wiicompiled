/* Game-specific changes (not hardware emulation).
 *
 * Boot screens: the game boots into the Strap scene (the wrist strap and
 * safety notice), then Hold ("Hold Wii Remote Horizontally"), then the
 * StartMenu (title). Strap sets up the game and starts ParkPrepare, the
 * background load of the park's data, and stays up for a fixed time; Hold
 * then shows for a few more seconds after the load has finished. Here
 * Strap hands over to the title as soon as the load is done, and Hold is
 * skipped. RECOMP_BOOT_SCREENS=1 keeps the original screens. */
#include <stdlib.h>

#include "hle.h"

/* Scenes, as indexed by the scene table (0x80337FA8). */
enum { SCENE_PARK, SCENE_ATTRACTION, SCENE_START_MENU, SCENE_LOBBY, SCENE_STRAP, SCENE_HOLD, SCENE_CHECK_DATA };

/* ParkPrepare::_static.fState (+0x60): 2 once the park's data is loaded. */
#define PARK_PREPARE_STATE 0x803A21D8
#define PARK_PREPARE_DONE 2

static int sLeftStrap;

static int keep_boot_screens(void) {
    const char* e = getenv("RECOMP_BOOT_SCREENS");
    return e && *e && *e != '0';
}

/* BOOL fn_801741F0(s32 scene): picks the next scene, by index. */
void f_801741F0(ppc_t* c) {
    if ((int32_t)c->r[3] == SCENE_HOLD && !sLeftStrap && !keep_boot_screens()) {
        /* Strap's own hand-over, if the load outlasted its timer. */
        sLeftStrap = 1;
        c->r[3] = SCENE_START_MENU;
    }
    HLE_LOG("next scene: %d", (int)c->r[3]);
    f_801741F0_orig(c);
}

/* void SceneSpecializerStrap::RunSystem(void): runs every frame. Does what
 * the Strap scene's script does when its time is up (scene syscall 0x15,
 * subcommands 1 and 2): pick the next scene, then start the switch. */
void f_80173E10(ppc_t* c) {
    if (!sLeftStrap && LD32(PARK_PREPARE_STATE) == PARK_PREPARE_DONE && !keep_boot_screens()) {
        sLeftStrap = 1;
        c->r[3] = SCENE_START_MENU;
        f_801741F0(c);
        f_80019620(c); /* SceneManager::Get */
        f_8016C950(c); /* sets fChangeScene (+0x2C) */
    }
}
