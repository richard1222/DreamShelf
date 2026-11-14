#ifndef A4_WATCHDOG_H
#define A4_WATCHDOG_H

#include <stdint.h>

/* Freeze watchdog (M1 freeze fix, 2026-11-14).
 *
 * The M1 blades UI froze on hardware: m0.log ended mid-loop with no
 * further lines at all, which the append-per-call log design proves
 * was the loop itself stopping (hung or dead), not lost output. The
 * prime suspect is SDL_RenderPresent with the PSL1GHT SDL2 driver:
 * its present path waits for flip completion (gcmSetWaitFlip /
 * gcmGetFlipStatus) and can block indefinitely if a flip completion
 * is missed -- consistent with an intermittent freeze after hundreds
 * of successful presents, and with the M0 history (accelerated
 * renderer hung on the first present).
 *
 * The watchdog runs on a second thread (SDL_CreateThread) and watches
 * a frame-sequence counter + phase variable the main thread updates.
 * It writes to its OWN file (/dev_hdd0/atari/logs/watchdog.log), never
 * m0.log, so its evidence survives even when the main thread is stuck
 * inside a call that holds no log lock. On a confirmed stall it logs
 * the phase the main thread died in; as a safety net it exits the
 * process cleanly (return to XMB) instead of leaving a hard freeze.
 * Stall budgets: 8 s stuck in PRESENT (the suspect), 30 s in any
 * other phase (covers slow core loads / long scans without firing).
 *
 * Usage: a4_watchdog_start() once after SDL_Init, a4_watchdog_tick()
 * once per loop iteration, a4_watchdog_phase() around blocking calls.
 * The watchdog thread dies with the process on spawn/exit; stop() is
 * best-effort for clean shutdown paths. */

enum A4Phase {
  A4_PHASE_BOOT = 0,
  A4_PHASE_SYSUTIL,
  A4_PHASE_PAD,
  A4_PHASE_INPUT,
  A4_PHASE_ANIM,
  A4_PHASE_DRAW,
  A4_PHASE_PRESENT,
  A4_PHASE_DELAY,
  A4_PHASE_SCAN,
  A4_PHASE_SPAWN,
  A4_PHASE_LOAD,
  A4_PHASE_RUN,
  A4_PHASE_WAIT,
  A4_PHASE_IDLE
};

/* Start the watchdog thread. owner is "launcher" or "runner" (for the
 * log lines). Safe to call once; a second call is a no-op. */
void a4_watchdog_start(const char* owner);

/* Record the current phase (call before a blocking call). Cheap:
 * one volatile store. */
void a4_watchdog_phase(enum A4Phase phase);

/* Heartbeat: call once per loop iteration. Advances the sequence and
 * stamps the time; the watchdog measures stalls from this. */
void a4_watchdog_tick(void);

/* Ask the watchdog thread to stop. Best-effort; the thread is
 * detached and exits on its next wake-up. */
void a4_watchdog_stop(void);

#endif
