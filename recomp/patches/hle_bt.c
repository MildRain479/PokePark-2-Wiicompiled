/* Bluetooth. There is no Bluetooth adapter (hle_ios.c refuses
 * /dev/usb/oh1/57e/305); Wii Remote input comes from an HLE of WPAD
 * (hle_wpad.c). WUD still sets up its own state and alarms. */
#include "recomp_funcs.h"

/* void BTA_Init(void): opens the adapter and waits until it is ready. */
void f_80260FC0(ppc_t* c) { (void)c; }

/* s32 WUDGetStatus(void), which WPADGetStatus forwards to: report the
 * stack as set up (WPAD_STATE_SETUP), with no remotes connected. */
void f_8025A870(ppc_t* c) { c->r[3] = 3; }
