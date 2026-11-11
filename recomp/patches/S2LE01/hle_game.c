/* PokePark 2's own changes. What the shared patches ask of a game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "host_input.h"

void hle_modifiers_frame(ppc_t* c, int inPark); /* hle_modifiers.c */

/* Scenes, as indexed by the scene table (0x80469360). */
enum {
    SCENE_PARK,
    SCENE_STRAP,
    SCENE_HOLD,
    SCENE_CHECK_DATA,
    SCENE_ATTRACTION,
    SCENE_LOBBY,
    SCENE_START_MENU,
    SCENE_COLOSSEUM_MENU,
    SCENE_ACHIEVEMENT_MENU,
    SCENE_MUMA_MENU,
    SCENE_MOVIE,
    SCENE_STAFF_ROLL
};

static int sScene = -1;

/* Boot screens, as in the first game (PokePark 1's hle_game.c): Strap stays up
 * for a fixed time while ParkPrepare loads the park's data in the
 * background, and hands over to Hold here as soon as that is done.
 * RECOMP_BOOT_SCREENS=1 keeps the original screens. */
#define PARK_PREPARE_STATE 0x804BA8C8
#define PARK_PREPARE_DONE 2
static int sLeftStrap, sSwallowChange;

static int keep_boot_screens(void) {
    const char* e = getenv("RECOMP_BOOT_SCREENS");
    return e && *e && *e != '0';
}

/* BOOL fn_80210660(s32 scene): picks the next scene, by index. */
void F_fn_801741F0(ppc_t* c) {
    /* The Strap scene's script, still running after we left it, picks Hold
     * again when its time is up: see F_EndScene. */
    if (sLeftStrap == 1 && sScene == SCENE_HOLD && c->r[3] == SCENE_HOLD) {
        sLeftStrap = 2;
        sSwallowChange = 1;
        c->r[3] = 1;
        return;
    }
    HLE_LOG("next scene: %d", (int)c->r[3]);
#ifdef RECOMP_ANDROID_LOG
    fprintf(stderr, "next scene: %d\n", (int)c->r[3]);
#endif
    sScene = (int)c->r[3];
    F_fn_801741F0_orig(c);
}

/* void SceneManager::EndScene(this): switches to the scene picked. Not for
 * the Strap script's late switch to Hold (above), which would start Hold
 * over, reinitializing the heaps under what the boot screens had loaded. */
void F_EndScene(ppc_t* c) {
    if (sSwallowChange) {
        sSwallowChange = 0;
        return;
    }
    F_EndScene_orig(c);
}

int hle_in_minigame(void) { return sScene == SCENE_ATTRACTION; }

/* The scripts and menus pass the attraction on from scene to scene as a
 * parameter, its index (0-3). */
#define ATTRACTION_ID_NAME 0x804530C4 /* "AttractionId" */
#define SCENE_NEXT_PARAMS 0xAC        /* SceneManager: the next scene's parameters */
static int sAttraction;

/* void AddSceneParameter(Params*, const char* name, s32 value) */
void F_AddSceneParameter(ppc_t* c) {
    const char* want = "AttractionId";
    uint32_t name = c->r[4];
    int i = 0;
    while (want[i] && LD8(name + i) == (uint8_t)want[i]) i++;
    if (!want[i] && !LD8(name + i)) sAttraction = (int)c->r[5] + 1;
    F_AddSceneParameter_orig(c);
}

/* The minigame being played (its At number plus one, 1-4), or 0 outside one. */
int hle_minigame(void) { return sScene == SCENE_ATTRACTION ? sAttraction : 0; }

/* The pointer: the lobbies and menu scenes are worked with it, and in the
 * park B looks around with it. */
int hle_pointer_wanted(uint32_t held) {
    switch (sScene) {
    case SCENE_LOBBY:
    case SCENE_START_MENU:
    case SCENE_COLOSSEUM_MENU:
    case SCENE_ACHIEVEMENT_MENU:
    case SCENE_MUMA_MENU:
        return 1;
    case SCENE_PARK:
        return (held & HOST_PAD_B) != 0;
    default:
        return 0;
    }
}

/* Testing a minigame (RECOMP_ATTRACTION=<n>,<seconds> once in the park, n
 * from 1), as in the first game: its index in the next scene's parameters
 * and the park scene ended, the way an attraction's gate does it. */
static int sMinigameRequest;

void hle_request_minigame(int at) { sMinigameRequest = at; }

/* void SceneManager::RunBegin(this): each frame, at a point where the
 * scene can be changed. */
void F_SceneManager_RunBegin(ppc_t* c) {
    static int envDone;
    static uint64_t start;
    const char* e = getenv("RECOMP_ATTRACTION");
    int at;
    F_SceneManager_RunBegin_orig(c);
    hle_modifiers_frame(c, sScene == SCENE_PARK);
    if (sScene == SCENE_STRAP && !sLeftStrap && LD32(PARK_PREPARE_STATE) == PARK_PREPARE_DONE &&
        !keep_boot_screens()) {
        sLeftStrap = 1;
        c->r[3] = SCENE_HOLD;
        F_fn_801741F0(c);
        F_nsScene__SceneManager__Get(c);
        F_EndScene(c);
        return;
    }
    if (sScene != SCENE_PARK) return;
    if (!envDone && e && *e) {
        const char* comma = strchr(e, ',');
        if (!start) start = ppc_timebase();
        if ((double)(ppc_timebase() - start) / PPC_TB_HZ >= (comma ? atof(comma + 1) : 0)) {
            envDone = 1;
            sMinigameRequest = atoi(e);
        }
    }
    at = sMinigameRequest;
    if (at < 1 || at > 4) return;
    sMinigameRequest = 0;
    F_nsScene__SceneManager__Get(c); /* SceneManager::Get */
    c->r[5] = (uint32_t)(at - 1);
    hle_call(c, A_AddSceneParameter, c->r[3] + SCENE_NEXT_PARAMS, ATTRACTION_ID_NAME);
    F_nsScene__SceneManager__Get(c);
    F_EndScene(c);
}

void hle_pause_texture(uint32_t tex) { (void)tex; }

void hle_pad_art_load(void); /* hle_pad_art.c */

/* Options on the save select screen (options.c): CMenuScreenStartSaveData's
 * update runs each frame it is up, its state (+4) 2 while it waits for the
 * player. */
enum { SAVE_SCREEN_STATE = 4, SAVE_SCREEN_WAITING = 2 };
static uint32_t sSaveScreen;
static uint64_t sSaveScreenSeen;

/* CMenuScreenStartSaveData::virt0x1C */
void F_MenuScreenSaveData_Update(ppc_t* c) {
    sSaveScreen = c->r[3];
    sSaveScreenSeen = ppc_timebase();
    hle_pad_art_load(); /* the Options button's pictures (hle_pad_art.c) */
    F_MenuScreenSaveData_Update_orig(c);
}

int hle_file_select_shown(void) {
    return sSaveScreen && ppc_timebase() - sSaveScreenSeen < PPC_TB_HZ / 8 &&
           (int16_t)LD16(sSaveScreen + SAVE_SCREEN_STATE) == SAVE_SCREEN_WAITING;
}

/* The PokePark Pad, for the Options tab (options.c): CMenuUIPartsPause's
 * update runs each frame it is up, its state (+0x10) 3 on the main page and
 * 4 in one of its screens. Left and right do nothing on the main page. */
enum { PAUSE_STATE = 0x10, PAUSE_MAIN_PAGE = 3 };
static uint32_t sPause;
static uint64_t sPauseSeen;

/* CMenuUIPartsPause::virt0x2C */
void F_MenuUIPartsPause_Update(ppc_t* c) {
    sPause = c->r[3];
    sPauseSeen = ppc_timebase();
    hle_pad_art_load();
    F_MenuUIPartsPause_Update_orig(c);
}

int hle_pause_main_page(void) {
    return sPause && ppc_timebase() - sPauseSeen < PPC_TB_HZ / 8 && LD16(sPause + PAUSE_STATE) == PAUSE_MAIN_PAGE;
}

/* Frame timing, as in the first game (PokePark 1's hle_game.c): the frame steps
 * are constants of 1/59.94 s and 2/59.94 s, pooled in .sdata2 by each file
 * that uses them, and follow RECOMP_VI_HZ. */
#define SDATA2_START 0x804B19A0u
#define SDATA2_END 0x804B5640u
#define STEP_ONE 0x3C88AB7Cu
#define STEP_TWO 0x3D08AB7Cu

/* The clocks turn a step into ticks as it stands (the first game scales it by
 * 1.001): one tick too few here and the clocks slip past the retrace every
 * few seconds, each pass a few seconds at half the frame rate. */
static uint64_t step_ticks(float f) { return (uint64_t)((double)f * PPC_TB_HZ); }

/* The largest step not above the refresh period whose double is exactly
 * twice as many ticks, so the 30 Hz alarm stays locked to the 60 Hz one. */
static float step_const(void) {
    double ticks = PPC_TB_HZ / hle_vi_hz();
    float f = (float)(1.0 / hle_vi_hz()) * 1.001f;
    while ((double)step_ticks(f) > ticks || step_ticks(2 * f) != 2 * step_ticks(f)) f = nextafterf(f, 0.0f);
    return f;
}

void hle_game_init(void) {
    uint32_t a;
    for (a = SDATA2_START; a < SDATA2_END; a += 4) {
        uint32_t v = LD32(a);
        if (v == STEP_ONE) ST32(a, f32_bits(step_const()));
        else if (v == STEP_TWO) ST32(a, f32_bits(2 * step_const()));
    }
}

/* Frame rate. Clock 1's period comes from the scene table, two steps in the
 * park; set_frame_limit also takes 0 (the clock's own) and 1 (its slow-motion
 * period), which it resolves itself, so the result is read back from the
 * clock. At 60 fps and up it is held to one step. */
#define CLOCKS 0x804B9B58u
#define CLOCK_SIZE 0x70
#define CLOCK_WANT_PERIOD 0x40 /* u64, taken up by the clock's next tick */

static int sFrameRate;        /* 0: the game's own; 60: every update */
static uint64_t sScenePeriod; /* clock 1's period as the scene set it */

static uint64_t draw_period(void) {
    uint64_t one = step_ticks(step_const());
    return sFrameRate >= 60 && sScenePeriod > one ? one : sScenePeriod;
}

static void want_period(uint64_t p) {
    ST32(CLOCKS + CLOCK_SIZE + CLOCK_WANT_PERIOD, (uint32_t)(p >> 32));
    ST32(CLOCKS + CLOCK_SIZE + CLOCK_WANT_PERIOD + 4, (uint32_t)p);
}

/* void set_frame_limit(Clocks*, int clock, u64 period) */
void F_set_frame_limit(ppc_t* c) {
    int clock = (int)c->r[4];
    F_set_frame_limit_orig(c);
    if (clock == 1) {
        sScenePeriod = ((uint64_t)LD32(CLOCKS + CLOCK_SIZE + CLOCK_WANT_PERIOD) << 32) |
                       LD32(CLOCKS + CLOCK_SIZE + CLOCK_WANT_PERIOD + 4);
        want_period(draw_period());
    }
}

void hle_set_frame_rate(int fps) {
    sFrameRate = fps;
    if (sScenePeriod) want_period(draw_period());
}

/* void CMovieManager::Advance(this): decodes the next frame of a playing
 * movie, once per update. Updates come faster than the scene's own at 60 fps
 * and up, so the movie only steps as often as the scene's clock would. */
static uint64_t sMovieTicks;

void F_MovieManager_Advance(ppc_t* c) {
    if (sScenePeriod) {
        sMovieTicks += draw_period();
        if (sMovieTicks < sScenePeriod) return;
        sMovieTicks -= sScenePeriod;
    }
    F_MovieManager_Advance_orig(c);
}
