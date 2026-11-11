/* Example hook: wraps the game's strlen. Gameplay changes such as
 * difficulty scaling or a stick-driven camera follow the same pattern:
 * list the function in hooks.txt, then read or change guest registers and
 * memory before or after calling the original. */
#include "hle.h"

unsigned g_strlen_calls;

void F_strlen(ppc_t* c) {
    g_strlen_calls++;
    F_strlen_orig(c);
}
