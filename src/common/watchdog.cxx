#include "common/watchdog.h"
#define A4_WATCHDOG_LOG "/dev_hdd0/tmp/themeshelf-watchdog.log"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include <lv2/process.h>
#include "SDL.h"

/* Stall budgets (ms). PRESENT gets the short budget: a present that
 * does not return in 8 s is the freeze, not a slow frame. Other
 * phases get 30 s so slow loads/scans never false-positive. */
#define A4_WD_STALL_MS 3000u
#define A4_WD_RELOG_MS 2000u
#define A4_WD_PRESENT_EXIT_MS 8000u
#define A4_WD_ANY_EXIT_MS 30000u
#define A4_WD_POLL_MS 250u

static const char* kPhaseNames[] = {
  "BOOT", "SYSUTIL", "PAD", "INPUT", "ANIM", "DRAW", "PRESENT",
  "DELAY", "SCAN", "SPAWN", "LOAD", "RUN", "WAIT", "IDLE"
};

static std::atomic<uint32_t> g_seq{0};
static std::atomic<int> g_phase{A4_PHASE_BOOT};
static std::atomic<uint32_t> g_last_tick_ms{0};
static std::atomic<int> g_stop{0};
static int g_started = 0;
static SDL_Thread* g_thread = NULL;
static char g_owner[16] = "app";

static void wd_append(const char* fmt, uint32_t seq, const char* phase,
                      uint32_t stuck_ms) {
  FILE* f = fopen(A4_WATCHDOG_LOG, "a");
  if (!f) return;
  fprintf(f, "watchdog [%s] %s seq=%u phase=%s stuck_ms=%u\n", g_owner, fmt,
          (unsigned)seq, phase, (unsigned)stuck_ms);
  fclose(f);
}

static int watchdog_thread(void* arg) {
  (void)arg;
  uint32_t reported_seq = 0;
  uint32_t last_report_ms = 0;
  int reported = 0;
  for (;;) {
    SDL_Delay(A4_WD_POLL_MS);
    if (g_stop.load(std::memory_order_acquire)) return 0;
    uint32_t seq = g_seq.load(std::memory_order_acquire);
    uint32_t last = g_last_tick_ms.load(std::memory_order_relaxed);
    if (last == 0) continue; /* no tick yet: still in boot */
    uint32_t now = SDL_GetTicks();
    uint32_t stuck = now - last;
    if (seq != reported_seq) {
      /* Main thread advanced: clear the stall report state. */
      reported_seq = seq;
      reported = 0;
      last_report_ms = 0;
    }
    if (stuck < A4_WD_STALL_MS) continue;
    int phase = g_phase.load(std::memory_order_relaxed);
    const char* pname =
        (phase >= 0 && phase < (int)(sizeof kPhaseNames / sizeof kPhaseNames[0]))
            ? kPhaseNames[phase]
            : "?";
    if (!reported || (now - last_report_ms) >= A4_WD_RELOG_MS) {
      wd_append(reported ? "STALL (still stuck)" : "STALL", seq, pname, stuck);
      reported = 1;
      last_report_ms = now;
    }
    int present_phase = (phase == A4_PHASE_PRESENT);
    if ((present_phase && stuck >= A4_WD_PRESENT_EXIT_MS) ||
        stuck >= A4_WD_ANY_EXIT_MS) {
      wd_append("EXIT safety net: exiting to XMB", seq, pname, stuck);
      /* Clean return to XMB beats a hard freeze: the user can
       * relaunch, and watchdog.log holds the evidence. */
      sysProcessExit(0);
      /* If that ever returns, keep reporting; do not spin. */
      SDL_Delay(1000);
    }
  }
  return 0;
}

void a4_watchdog_start(const char* owner) {
  if (g_started) return;
  g_stop.store(0,std::memory_order_relaxed);
  if (owner) {
    size_t n = strlen(owner);
    if (n >= sizeof g_owner) n = sizeof g_owner - 1;
    memcpy(g_owner, owner, n);
    g_owner[n] = '\0';
  }
  uint32_t now=SDL_GetTicks();
  g_last_tick_ms.store(now?now:1,std::memory_order_relaxed);
  g_thread = SDL_CreateThread(watchdog_thread, "a4-watchdog", NULL);
  g_started = g_thread != NULL;
  wd_append(g_started?"START observer active":"START FAILED",0,"BOOT",0);
  /* If thread creation failed there is no watchdog; the app still
   * runs (the main-thread phase log in m0.log is the fallback). */
}

void a4_watchdog_phase(enum A4Phase phase) {
  g_phase.store((int)phase,std::memory_order_relaxed);
}

void a4_watchdog_tick(void) {
  // Publish the timestamp before the sequence observed by the watchdog.
  uint32_t now=SDL_GetTicks();
  g_last_tick_ms.store(now?now:1,std::memory_order_relaxed);
  g_seq.fetch_add(1,std::memory_order_release);
}

void a4_watchdog_stop(void) {
  g_stop.store(1,std::memory_order_release);
  // The SDL timer/thread facilities remain live until the observer exits.
  // Callers may safely destroy SDL immediately after this function returns.
  if(g_thread) { SDL_WaitThread(g_thread,NULL); g_thread=NULL; }
  g_started=0;
}
