/* The minimap, for a second screen to show (vk_backend.c).
 *
 * The HUD (fn_80142EC0) draws its layouts with fn_8013CAC0, most of them
 * from 0x801431C8: the minimap's (MnFdMap: the map, its marks and frame)
 * and others, the text boxes (MnFdMessage) among them. The minimap's draws
 * are marked in the FIFO, a write to CP register 0xF0 (one the GPU has
 * not) before and after, for the second screen to pick the minimap out of
 * (gx_fifo.c). Its friends list is kept current from here too
 * (hle_friends.c), and the autosave watched for (hle_autosave.c). */
#include "hle.h"

void hle_friends_update(ppc_t* c);
void hle_autosave_update(ppc_t* c);

static void mark_minimap(uint32_t on) {
    g_wgpipe_write(0x08, 1);
    g_wgpipe_write(0xF0, 1);
    g_wgpipe_write(on, 4);
}

/* Whether the layout at at is named name (its name at +0xC). */
static int named(uint32_t at, const char* name) {
    for (at += 0xC; *name; name++, at++) {
        if (LD8(at) != (uint8_t)*name) return 0;
    }
    return LD8(at) == 0;
}

/* A layout's draw. */
void f_8013CAC0(ppc_t* c) {
    int minimap = c->lr == 0x801431C8u && named(c->r[3], "MnFdMap");
    if (minimap) hle_friends_update(c);
    if (minimap) hle_autosave_update(c); /* hle_autosave.c */
    if (minimap) mark_minimap(1);
    f_8013CAC0_orig(c);
    if (minimap) mark_minimap(0);
}
