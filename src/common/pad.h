#ifndef A4_PAD_H
#define A4_PAD_H

#include <stddef.h>
#include <stdint.h>

/* DualShock 3 state, combined across all connected ports.
 *
 * Pattern follows Neon Lounge (proven on this console): ioPadGetInfo2 +
 * per-port status bits, all 7 ports polled, held state kept sticky per
 * port (ioPad only reports on change), combined with OR. The M0 code
 * polled port 0 only via the old ioPadGetInfo and saw no input on
 * hardware: if the pad is not assigned to port 0 in a given session,
 * port-0-only polling is blind. The button bitfield extraction is the
 * same as SalsaDS/Neon Lounge (proven).
 *
 * Init order matters: call a4_pad_init() AFTER SDL_Init (Neon Lounge
 * order). A PAD_ERROR_ALREADY_INITIALIZED result is benign. */

enum A4Button {
  A4B_CROSS = 0, A4B_CIRCLE, A4B_SQUARE, A4B_TRIANGLE,
  A4B_L1, A4B_R1, A4B_L2, A4B_R2, A4B_L3, A4B_R3,
  A4B_START, A4B_SELECT, A4B_UP, A4B_DOWN, A4B_LEFT, A4B_RIGHT,
  A4B_COUNT
};

#define A4B_BIT(b) (1u << (b))

struct A4Pad {
  uint32_t buttons; /* 1 << A4Button, OR-combined across ports */
  uint8_t lx, ly, rx, ry; /* 0..255, center 128, from the lowest port with data */
  int present; /* 1 when at least one pad is connected */
  int port;    /* lowest connected port, or -1 when none */
};

void a4_pad_init(void);
/* Raw ioPadInit result (0 = ok). Kept for boot logging. */
int a4_pad_init_result(void);
void a4_pad_poll(struct A4Pad* out);

/* One-line diagnostic: info2 result, connected count, per-port status
 * bits ("ports=1000000" = pad on port 0 only). Safe any time after
 * a4_pad_init; used for boot logging. */
void a4_pad_status_line(char* buf, size_t buf_size);

#endif
