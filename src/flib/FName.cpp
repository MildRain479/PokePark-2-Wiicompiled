#include <types.h>

// Packed names store six characters per word, in base 40.
char FName_encoding[40] = {
    '\0', '0', '1', '2', '3', '4', '5', '6', '7', '8',
    '9',  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
    'J',  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
    'T',  'U', 'V', 'W', 'X', 'Y', 'Z', '_', '-', '/',
};

// TODO: Purpose unknown; both are empty in the shipped game.
extern "C" void fn_802ED900(int) {}

extern "C" void fn_802ED910() {}

// Unpacks a two-word name into "XXXXXX" + "XXXXXX", stopping after the
// first word that contains the terminator.
void Demangle_FName(const u32* in, char* out) {
    u32 value;
    int j;
    BOOL full;
    int i;

    // The original loop is tested at the top and continues only when the
    // word was full; an ordinary for/while loop compiles to a rotated loop.
    i = 0;
    goto check;
next:
    in++;
    i++;
    out += 6;
check:
    if (i >= 2) return;
    value = *in;
    full = TRUE;
    for (j = 5; j >= 0; j--) {
        if ((out[j] = FName_encoding[value % 40]) == '\0') {
            full = FALSE;
        }
        value /= 40;
    }
    out[6] = '\0';
    if (full) goto next;
}
