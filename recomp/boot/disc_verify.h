/* Whether a disc image is one the game can run from: the checks behind
 * import_win32.c and disc_image_android.c, with no OS calls so that they
 * are tested on their own (recomp/tests/test_recomp.cpp).
 *
 * The recompiled code was made from one main.dol, so another one can't
 * run: another game, region or revision, or a different main.dol, is
 * refused. An image that only differs from the original disc elsewhere
 * often plays (WBFS and scrubbed copies leave out the disc's padding), so
 * that is the player's to decide. */
#ifndef DISC_VERIFY_H
#define DISC_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#include "../runtime/game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISC_OK = 0,
    DISC_UNREADABLE,     /* not a disc image */
    DISC_WRONG_GAME,     /* another game */
    DISC_WRONG_REGION,   /* the game, not the USA release */
    DISC_WRONG_REVISION, /* USA, not Rev 0 */
    DISC_WRONG_CODE      /* GAME_ID Rev 0 by its header, another main.dol */
} disc_result;

/* GAME_ID Rev 0 on Redump, and its main.dol. */
#define DISC_REDUMP_SHA1 "e429a232431ea7075ae959b030a7675846357c19"
#define DISC_REDUMP_CRC32 "7771d7ef"
#define DISC_MAIN_DOL_SHA1 "34f024f683c8d068020066a07a2a1f939735eff2"

/* From the header's game ID (6 characters) and revision. Failing, msg says
 * why, for the player. */
disc_result disc_check_id(const char* id, int revision, char* msg, size_t n);

/* The same from what "dtk disc info" prints. */
disc_result disc_check_info(const char* info, char* msg, size_t n);

/* main.dol's bytes against the one the code was recompiled from. */
disc_result disc_check_dol(const uint8_t* dol, size_t len, char* msg, size_t n);

/* Whether what "dtk disc verify" prints is the original disc's hashes. */
int disc_is_original(const char* verify);

/* out: 40 hex digits and a 0. */
void disc_sha1(const uint8_t* data, size_t len, char out[41]);

#ifdef __cplusplus
}
#endif

#endif
