/* Bluetooth. There is no Bluetooth adapter (hle_ios.c refuses
 * /dev/usb/oh1/57e/305); Wii Remote input comes from an HLE of WPAD
 * (hle_wpad.c). WUD still sets up its own state and alarms. */
#include "hle.h"

/* void BTA_Init(void): opens the adapter and waits until it is ready. */
void F_BTA_Init(ppc_t* c) { (void)c; }

/* s32 WUDGetStatus(void), which WPADGetStatus forwards to: report the
 * stack as set up (WPAD_STATE_SETUP), with no remotes connected. */
void F_WUDGetStatus(ppc_t* c) { c->r[3] = 3; }
