// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/bgm.h"
#include "common/log.h"
#include <SDL.h>
#include <cstdint>
#include <cstring>

/* Modest playback level: the asset itself is peak-normalized to 0.55, and
 * this gain keeps the loop well under UI-prominence while the app runs. */
static const float BGM_GAIN = 0.45f;

static SDL_AudioDeviceID bgm_dev = 0;
static uint8_t *bgm_pcm = nullptr; /* converted to the device format, gain applied */
static int bgm_len = 0;            /* bytes */
static int bgm_pos = 0;            /* bytes; only touched under the device lock */
static bool bgm_started = false;

/* Runs on SDL's audio thread. Fills the whole stream from the loop buffer;
 * SDL always requests whole frames and bgm_len is a whole number of
 * frames, so the wrap stays frame-aligned. */
static void bgm_callback(void *, uint8_t *stream, int len) {
 if (!bgm_pcm || bgm_len <= 0) { memset(stream, 0, (size_t)len); return; }
 int done = 0;
 while (done < len) {
  int chunk = bgm_len - bgm_pos;
  if (chunk > len - done) chunk = len - done;
  memcpy(stream + done, bgm_pcm + bgm_pos, (size_t)chunk);
  bgm_pos += chunk;
  done += chunk;
  if (bgm_pos >= bgm_len) bgm_pos = 0;
 }
}

static void bgm_fail(const char *what) {
 a4_log("bgm", "%s: %s", what, SDL_GetError());
 if (bgm_dev) { SDL_CloseAudioDevice(bgm_dev); bgm_dev = 0; }
 if (bgm_pcm) { SDL_free(bgm_pcm); bgm_pcm = nullptr; bgm_len = 0; bgm_pos = 0; }
 SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool a4_bgm_start(const char *path) {
 if (bgm_started) return true;
 if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
  a4_log("bgm", "SDL_INIT_AUDIO failed: %s", SDL_GetError());
  return false;
 }
 SDL_AudioSpec wav_spec{};
 uint8_t *wav_buf = nullptr;
 uint32_t wav_len = 0;
 if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len) || !wav_buf || wav_len == 0) {
  a4_log("bgm", "could not load %s: %s", path, SDL_GetError());
  if (wav_buf) SDL_FreeWAV(wav_buf);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  return false;
 }
 SDL_AudioSpec want{}, have{};
 want.freq = 48000;
 want.format = AUDIO_S16SYS;
 want.channels = 2;
 want.samples = 2048;
 want.callback = bgm_callback;
 want.userdata = nullptr;
 bgm_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                               SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                               SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
 if (!bgm_dev) {
  a4_log("bgm", "could not open audio device: %s", SDL_GetError());
  SDL_FreeWAV(wav_buf);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  return false;
 }
 if (have.format != AUDIO_S16SYS) {
  a4_log("bgm", "unexpected device format 0x%x; BGM disabled", (unsigned)have.format);
  SDL_FreeWAV(wav_buf);
  bgm_fail("device format");
  return false;
 }
 /* Convert the WAV to the device format once, up front. */
 uint8_t *pcm = wav_buf;
 int pcm_len = (int)wav_len;
 bool converted = false;
 SDL_AudioCVT cvt{};
 int cvt_rc = SDL_BuildAudioCVT(&cvt, wav_spec.format, wav_spec.channels, wav_spec.freq,
                                have.format, have.channels, have.freq);
 if (cvt_rc < 0) {
  SDL_FreeWAV(wav_buf);
  bgm_fail("SDL_BuildAudioCVT");
  return false;
 }
 if (cvt_rc > 0) {
  cvt.len = (int)wav_len;
  cvt.buf = (uint8_t *)SDL_malloc((size_t)wav_len * (size_t)cvt.len_mult);
  if (!cvt.buf) {
   SDL_FreeWAV(wav_buf);
   bgm_fail("SDL_malloc for conversion");
   return false;
  }
  memcpy(cvt.buf, wav_buf, wav_len);
  SDL_FreeWAV(wav_buf);
  wav_buf = nullptr;
  if (SDL_ConvertAudio(&cvt) != 0) {
   SDL_free(cvt.buf);
   bgm_fail("SDL_ConvertAudio");
   return false;
  }
  pcm = cvt.buf;
  pcm_len = cvt.len_cvt;
  converted = true;
 }
 (void)converted;
 /* Bake in the playback gain. */
 int16_t *samples = (int16_t *)pcm;
 int count = pcm_len / 2;
 for (int i = 0; i < count; ++i) {
  float v = (float)samples[i] * BGM_GAIN;
  if (v > 32767.0f) v = 32767.0f;
  if (v < -32768.0f) v = -32768.0f;
  samples[i] = (int16_t)v;
 }
 bgm_pcm = pcm;
 bgm_len = pcm_len;
 bgm_pos = 0;
 SDL_PauseAudioDevice(bgm_dev, 0);
 bgm_started = true;
 a4_log("bgm", "looping %s (device %d Hz, %d ch, %d bytes)", path, have.freq,
        (int)have.channels, bgm_len);
 return true;
}

void a4_bgm_stop(void) {
 if (!bgm_started) return;
 SDL_PauseAudioDevice(bgm_dev, 1);
 SDL_CloseAudioDevice(bgm_dev);
 bgm_dev = 0;
 if (bgm_pcm) { SDL_free(bgm_pcm); bgm_pcm = nullptr; }
 bgm_len = 0;
 bgm_pos = 0;
 SDL_QuitSubSystem(SDL_INIT_AUDIO);
 bgm_started = false;
 a4_log("bgm", "stopped");
}
