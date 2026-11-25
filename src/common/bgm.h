#ifndef DS_BGM_H
#define DS_BGM_H

/* Startup BGM (0.1.7): loads a PCM WAV and loops it quietly through SDL
 * audio. Playback runs entirely on SDL's audio thread via the device
 * callback -- the callback only memcpys from a pre-converted buffer, so
 * the UI thread never blocks on audio after the one-time load in start().
 * Every failure is soft: log it and continue without music. */
bool a4_bgm_start(const char *path);
void a4_bgm_stop(void);

#endif
