/* Shared helpers for the high-level replacements (HLE) of SDK and
 * hardware functions. */
#pragma once

#include <stdio.h>

#include "recomp_funcs.h"

/* RECOMP_HLE_LOG=1 logs HLE requests; 2 also logs thread switches with
 * guest backtraces. */
int hle_log_enabled(void);
#define HLE_LOG(...) do { if (hle_log_enabled()) { fprintf(stderr, "hle: " __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* Warnings are printed at most three times per call site. */
#define HLE_WARN(...) do { static int n_; if (n_ < 3) { fprintf(stderr, "hle: " __VA_ARGS__); fputs(++n_ == 3 ? " (further repeats hidden)\n" : "\n", stderr); } } while (0)

/* Queues a call fn(r3, r4) into guest code, made the next time the game
 * enables interrupts, the way a hardware interrupt would be taken. */
void hle_post(uint32_t fn, uint32_t r3, uint32_t r4, void (*after)(ppc_t* c, uint32_t data), uint32_t data);

/* Raises hardware interrupt n (__OS_INTERRUPT_*); its OS handler runs
 * like a queued call. */
void hle_raise_interrupt(int n);
/* ...calling before(data) just before the handler runs, e.g. to set the
 * device register that tells the handler what happened. */
void hle_raise_interrupt_ex(int n, void (*before)(uint32_t data), uint32_t data);

/* Periodic timers in guest time-base ticks, fired like interrupts. */
void hle_timer_start(int id, uint64_t period, void (*fire)(ppc_t* c));
void hle_timer_stop(int id);
enum { HLE_TIMER_AUDIO = 0, HLE_TIMER_PAD = 1 };

/* The emulated video refresh rate (RECOMP_VI_HZ, default 60). */
double hle_vi_hz(void);

/* Whether the console is set up for a 16:9 TV (RECOMP_WIDESCREEN, default
 * on; 0 for 4:3). The game then draws 16:9 pictures squeezed into its
 * 640-pixel-wide frame, as the Wii does for a TV to stretch out. */
int hle_widescreen(void);

/* Runs queued calls if any; preserves all guest registers. */
void hle_deliver(ppc_t* c);

/* Waits for the next event (a queued completion; later, timers and
 * retrace) and delivers it. Returns 0 if nothing could ever arrive. */
int hle_wait_for_event(ppc_t* c);

/* Thread scheduling and interrupt frames: see hle_thread.c. */
void hle_idle_check(ppc_t* c);
void hle_interrupt(ppc_t* c, uint32_t fn, uint32_t r3, uint32_t r4);
int hle_in_interrupt(void);

/* Calls a guest function with up to two arguments and returns r3,
 * preserving the caller's registers. */
uint32_t hle_call(ppc_t* c, uint32_t fn, uint32_t r3, uint32_t r4);

/* The disc (hle_dvd.c). hle_dvd_init takes the directory holding sys/
 * and files/ extracted from the disc. */
int hle_dvd_init(const char* root);
void hle_dvd_boot_setup(void);
int hle_dvd_read(uint64_t offset, uint32_t len, uint32_t dst);
uint32_t hle_dvd_tmd(uint32_t dst, uint32_t max);
const uint8_t* hle_dvd_disc_id(void);

/* Copies a NUL-terminated guest string (at most n - 1 bytes). */
void hle_read_str(uint32_t addr, char* out, size_t n);
