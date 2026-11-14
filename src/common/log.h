#ifndef A4_LOG_H
#define A4_LOG_H

/* Append-only log shared by launcher and runner. Every call opens, appends
 * one line and closes, so a crash never loses buffered lines. */

void a4_log_init(void);
void a4_log(const char* tag, const char* fmt, ...);

#endif
