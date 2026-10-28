/* Example hook: wraps the game's strlen. Gameplay changes such as
 * difficulty scaling or a stick-driven camera follow the same pattern:
 * list the function in hooks.txt, then read or change guest registers and
 * memory before or after calling the original. */
#include "recomp_funcs.h"

unsigned g_strlen_calls;

void f_802F3464(ppc_t* c) {
    g_strlen_calls++;
    f_802F3464_orig(c);
}
