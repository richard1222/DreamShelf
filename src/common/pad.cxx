#include "common/pad.h"
#include "common/log.h"

#include <io/pad.h>
#include <stdio.h>
#include <string.h>

static int g_init_rc = -1;

void a4_pad_init(void) {
  g_init_rc = ioPadInit(7);
}

int a4_pad_init_result(void) {
  return g_init_rc;
}

void a4_pad_status_line(char* buf, size_t buf_size) {
  padInfo2 info;
  memset(&info, 0, sizeof info);
  int rc = ioPadGetInfo2(&info);
  char ports[16];
  size_t o = 0;
  for (int p = 0; p < 7 && o < sizeof ports - 1; p++)
    ports[o++] = (info.port_status[p] & 1) ? '1' : '0';
  ports[o] = '\0';
  snprintf(buf, buf_size, "info2 rc=%d connected=%u ports=%s",
           rc, (unsigned)info.connected, ports);
}

void a4_pad_poll(struct A4Pad* out) {
  /* Per-port sticky state: ioPadGetData only fills padData when the
   * input changed since the last call (len == 0 otherwise), so the
   * last good state per port is kept and OR-combined. */
  static uint32_t held_btn[7];
  static uint8_t held_lx[7], held_ly[7], held_rx[7], held_ry[7];
  static int have[7];
  static int logged_mask = -1;

  padInfo2 info;
  memset(&info, 0, sizeof info);
  int rc = ioPadGetInfo2(&info);

  uint32_t buttons = 0;
  int present = 0;
  int first_port = -1;
  uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
  int have_stick = 0;
  int mask = 0;

  if (rc == 0) {
    for (int port = 0; port < 7; port++) {
      if (!(info.port_status[port] & 1)) {
        held_btn[port] = 0;
        have[port] = 0;
        continue;
      }
      mask |= 1 << port;
      present = 1;
      if (first_port < 0) first_port = port;
      padData data;
      memset(&data, 0, sizeof data);
      if (ioPadGetData(port, &data) == 0 && data.len > 0) {
        uint32_t cur = 0;
        cur |= (uint32_t)data.BTN_CROSS << A4B_CROSS;
        cur |= (uint32_t)data.BTN_CIRCLE << A4B_CIRCLE;
        cur |= (uint32_t)data.BTN_SQUARE << A4B_SQUARE;
        cur |= (uint32_t)data.BTN_TRIANGLE << A4B_TRIANGLE;
        cur |= (uint32_t)data.BTN_L1 << A4B_L1;
        cur |= (uint32_t)data.BTN_R1 << A4B_R1;
        cur |= (uint32_t)data.BTN_L2 << A4B_L2;
        cur |= (uint32_t)data.BTN_R2 << A4B_R2;
        cur |= (uint32_t)data.BTN_L3 << A4B_L3;
        cur |= (uint32_t)data.BTN_R3 << A4B_R3;
        cur |= (uint32_t)data.BTN_START << A4B_START;
        cur |= (uint32_t)data.BTN_SELECT << A4B_SELECT;
        cur |= (uint32_t)data.BTN_UP << A4B_UP;
        cur |= (uint32_t)data.BTN_DOWN << A4B_DOWN;
        cur |= (uint32_t)data.BTN_LEFT << A4B_LEFT;
        cur |= (uint32_t)data.BTN_RIGHT << A4B_RIGHT;
        held_btn[port] = cur;
        held_lx[port] = (uint8_t)data.ANA_L_H;
        held_ly[port] = (uint8_t)data.ANA_L_V;
        held_rx[port] = (uint8_t)data.ANA_R_H;
        held_ry[port] = (uint8_t)data.ANA_R_V;
        have[port] = 1;
      }
      buttons |= held_btn[port];
      if (!have_stick && have[port]) {
        lx = held_lx[port];
        ly = held_ly[port];
        rx = held_rx[port];
        ry = held_ry[port];
        have_stick = 1;
      }
    }
  }

  /* Log connect/disconnect transitions only (no per-frame spam). */
  if (mask != logged_mask) {
    logged_mask = mask;
    a4_log("pad", "ports mask=0x%x info2 rc=%d", mask, rc);
  }

  out->buttons = buttons;
  out->lx = lx;
  out->ly = ly;
  out->rx = rx;
  out->ry = ry;
  out->present = present;
  out->port = first_port;
}
