/* Game-specific changes (not hardware emulation).
 *
 * Boot screens: the game boots into the Strap scene (the wrist strap and
 * safety notice), then Hold ("Hold Wii Remote Horizontally"), then the
 * StartMenu (title). Strap sets up the game and starts ParkPrepare, the
 * background load of the park's data, and stays up for a fixed time; Hold
 * then shows for a few more seconds after the load has finished. Here
 * Strap hands over to Hold as soon as the load is done. Hold still runs
 * its course (see f_80173E10). RECOMP_BOOT_SCREENS=1 keeps the original
 * screens. */
#include <math.h>
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
    HLE_LOG("next scene: %d", (int)c->r[3]);
    f_801741F0_orig(c);
}

/* void SceneSpecializerStrap::RunSystem(void): runs every frame. Once the
 * park has loaded, does what the Strap scene's script does when its time
 * is up (scene syscall 0x15, subcommands 1 and 2): pick the next scene,
 * Hold, then start the switch. Hold must not be skipped: the game records
 * that it has run, and a new game started before it has goes through Hold
 * after the opening, and Hold always leads to the start menu. (Leaving
 * Hold as soon as it has seen the load done isn't enough either: its
 * script records it at the end.) */
void f_80173E10(ppc_t* c) {
    if (!sLeftStrap && LD32(PARK_PREPARE_STATE) == PARK_PREPARE_DONE && !keep_boot_screens()) {
        sLeftStrap = 1;
        c->r[3] = SCENE_HOLD;
        f_801741F0(c);
        f_80019620(c); /* SceneManager::Get */
        f_8016C950(c); /* sets fChangeScene (+0x2C) */
    }
}

/* Frame pacing: the game times its updates and draws in fixed steps on
 * the time base, computed at start-up as (bus clock / 4) times 1/59.94 s
 * and 2/59.94 s (for 30 fps) by four schedulers, each with its own copy of
 * the two constants. They are set to the emulated refresh rate instead
 * (hle_irq.c), so the steps agree with the retrace. Each is rounded down
 * to at most one retrace period, as the originals are: a step even
 * slightly longer leaves some frames without one (30 fps). */
static const uint32_t kFrameStepConsts[] = {0x80487540, 0x804877A0, 0x80488924, 0x80488990};

static float step_const(double frames) {
    double hz = hle_vi_hz(), ticks = PPC_TB_HZ * frames / hz;
    float f = (float)(frames / hz);
    while ((double)f * PPC_TB_HZ > ticks) f = nextafterf(f, 0.0f);
    return f;
}

void hle_game_init(void) {
    size_t i;
    for (i = 0; i < sizeof(kFrameStepConsts) / sizeof(kFrameStepConsts[0]); i++) {
        ST32(kFrameStepConsts[i], f32_bits(step_const(1)));
        ST32(kFrameStepConsts[i] + 4, f32_bits(step_const(2)));
    }
}

/* Movies: the scripts open one (ThpManager syscall 0, with a loop flag),
 * then poll its state (syscall 5: 2 while playing, 3 once finished)
 * before closing it. RECOMP_SKIP_MOVIES=1 reports a movie that doesn't
 * loop as finished after about a second of playing, which skips the 56 s
 * opening for testing; the looping title background plays as usual.
 * (Reporting it finished at once closed the player while it was still
 * starting, and the game then waited forever.) */
enum { THP_SYS_OPEN = 0, THP_SYS_STATE = 5, THP_PLAYING = 2, THP_FINISHED = 3 };
#define SKIP_AFTER_POLLS 60
static int sMovieLoops, sMoviePolls;

/* int ThpManager::virt0x18(int sub, u32* args) */
void f_801776E0(ppc_t* c) {
    static int skip = -1;
    uint32_t sub = c->r[4], args = c->r[5];
    if (skip < 0) {
        const char* e = getenv("RECOMP_SKIP_MOVIES");
        skip = e && *e && *e != '0';
    }
    if (sub == THP_SYS_OPEN) {
        sMovieLoops = LD32(args + 4) != 0;
        sMoviePolls = 0;
    }
    f_801776E0_orig(c);
    if (sub == THP_SYS_STATE && skip && !sMovieLoops && c->r[3] == THP_PLAYING && ++sMoviePolls > SKIP_AFTER_POLLS)
        c->r[3] = THP_FINISHED;
}
