#!/usr/bin/env python3
"""Generate DreamShelf's startup BGM (assets/bgm_startup.wav).

An ORIGINAL sound-alike of the Sega Dreamcast startup jingle, composed and
synthesized for this project -- no Sega audio is used or reproduced. The
character being evoked: a soft swirling whoosh rising, then glassy FM-bell
chimes arpeggiating a dreamy major chord over a gentle pad, ~9.6 seconds.

Pure standard library (no numpy on the build box). Renders in a few seconds:
bells are only synthesized over their decay windows and the pad/whoosh are
single passes over the timeline.

Usage: python3 tools/gen_bgm.py [output.wav]
"""

import math
import random
import struct
import sys
import wave
from array import array

SR = 44100
DUR = 9.6
N = int(SR * DUR)
TWO_PI = 2.0 * math.pi


def smoothstep(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


# --------------------------------------------------------------------------
# Buses (float, interleaved stereo)
# --------------------------------------------------------------------------
bells = array("f", bytes(4 * 2 * N))
pad = array("f", bytes(4 * 2 * N))
whoosh = array("f", bytes(4 * 2 * N))


def render_whoosh():
    """Noise through a lowpass whose cutoff sweeps upward = rising swirl,
    plus a faint sine 'air' sweep. Different seeds/cutoffs per channel."""
    rng = [random.Random(0xD15C), random.Random(0xB00B)]
    lp = [0.0, 0.0]
    hp = [0.0, 0.0]
    phase = 0.0
    for i in range(N):
        t = i / SR
        # envelope: rise over ~1.0 s, gone by ~2.9 s
        env = smoothstep(t / 1.0) * (1.0 - smoothstep((t - 1.7) / 1.2))
        if env <= 0.0:
            continue
        frac = min(1.0, t / 2.6)
        for ch in range(2):
            top = 3400.0 if ch == 0 else 3800.0
            cutoff = 240.0 * (top / 240.0) ** frac
            a = 1.0 - math.exp(-TWO_PI * cutoff / SR)
            lp[ch] += a * ((rng[ch].random() * 2.0 - 1.0) - lp[ch])
            hp[ch] += 0.002 * (lp[ch] - hp[ch])  # slow tracker -> band feel
            whoosh[2 * i + ch] += (lp[ch] - hp[ch]) * env * 0.85
        # air sweep 160 -> 660 Hz
        f = 160.0 * (660.0 / 160.0) ** frac
        phase += TWO_PI * f / SR
        s = math.sin(phase) * env * 0.045
        whoosh[2 * i] += s
        whoosh[2 * i + 1] += s * 0.9


def render_bell(t0, freq, vel, pan):
    """Glassy FM bell: carrier with an inharmonic modulator (~3.007x) whose
    index decays, plus a plain 2x partial. Exponential decay ~2 s."""
    start = int(t0 * SR)
    length = min(int(2.1 * SR), N - start)
    if length <= 0:
        return
    lgain = math.sqrt((1.0 - pan) * 0.5) * 1.4142
    rgain = math.sqrt((1.0 + pan) * 0.5) * 1.4142
    fm = freq * 3.007
    for k in range(length):
        t = k / SR
        env = math.exp(-t / 0.52) * vel
        if env < 0.0004:
            break
        idx = 3.6 * math.exp(-t / 0.30) + 0.35
        ph = TWO_PI * freq * t
        s = math.sin(ph + idx * math.sin(TWO_PI * fm * t))
        s += 0.38 * math.exp(-t / 0.22) * math.sin(2.0 * ph)
        v = s * env * 0.5
        bells[2 * (start + k)] += v * lgain
        bells[2 * (start + k) + 1] += v * rgain


# Original melody: a dreamy Cmaj9-flavoured arpeggio of my own arrangement,
# rising to a peak and settling back down -- the Dreamcast *shape*, not its
# notes.
MELODY = [
    (1.10, 261.63, 0.30,  0.00),   # C4 root, soft
    (1.15, 523.25, 0.55, -0.10),   # C5
    (1.48, 659.25, 0.60,  0.08),   # E5
    (1.81, 783.99, 0.62, -0.06),   # G5
    (2.18, 987.77, 0.66,  0.10),   # B5
    (2.60, 1174.66, 0.70, -0.08),  # D6
    (3.10, 1318.51, 0.78,  0.00),  # E6 peak
    (3.80, 783.99, 0.34,  0.12),   # G5 echo
    (4.30, 659.25, 0.30, -0.12),   # E5 echo
    (5.10, 523.25, 0.26,  0.00),   # C5 settle
    (5.10, 659.25, 0.18,  0.06),   # E5 settle (gentle dyad)
    (6.40, 783.99, 0.12,  0.00),   # G5 last sparkle
]


def render_bells():
    for (t0, f, v, p) in MELODY:
        render_bell(t0, f, v, p)
    # Light feed-forward taps on the bell bus for a sense of space.
    d1 = int(0.29 * SR)
    d2 = int(0.47 * SR)
    src = array("f", bells)
    for i in range(N):
        for ch in range(2):
            j = 2 * i + ch
            v = src[j]
            if i >= d1:
                v += 0.26 * src[j - 2 * d1]
            if i >= d2:
                v += 0.15 * src[j - 2 * d2]
            bells[j] = v


def render_pad():
    """Soft Cmaj7 bed: plain sines, two gently detuned, slow attack and a
    long release into the end of the clip."""
    partials = [
        (130.81, 0.050, -0.15),  # C3
        (196.00, 0.042,  0.15),  # G3
        (261.63, 0.036,  0.00),  # C4
        (329.63, 0.040, -0.20),  # E4
        (331.10, 0.020,  0.20),  # E4 detuned +
        (493.88, 0.034,  0.10),  # B4
        (495.22, 0.017, -0.10),  # B4 detuned +
    ]
    phases = [0.0] * len(partials)
    for i in range(N):
        t = i / SR
        env = smoothstep((t - 0.6) / 2.8) * (1.0 - smoothstep((t - 7.6) / 2.0))
        if env <= 0.0:
            continue
        l = r = 0.0
        for pi, (f, g, pan) in enumerate(partials):
            phases[pi] += TWO_PI * f / SR
            s = math.sin(phases[pi]) * g * env
            l += s * math.sqrt((1.0 - pan) * 0.5) * 1.4142
            r += s * math.sqrt((1.0 + pan) * 0.5) * 1.4142
        pad[2 * i] += l
        pad[2 * i + 1] += r


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/bgm_startup.wav"
    render_whoosh()
    render_bells()
    render_pad()
    # Mix, master fades, normalize to a modest peak.
    peak = 0.0
    mix = array("f", bytes(4 * 2 * N))
    for i in range(N):
        t = i / SR
        fade = smoothstep(t / 0.03) * (1.0 - smoothstep((t - (DUR - 0.9)) / 0.9))
        for ch in range(2):
            j = 2 * i + ch
            v = (whoosh[j] + bells[j] + pad[j]) * fade
            mix[j] = v
            if abs(v) > peak:
                peak = abs(v)
    scale = 0.55 / peak if peak > 0.0 else 1.0
    pcm = array("h", bytes(2 * 2 * N))
    for j in range(2 * N):
        v = mix[j] * scale
        v = min(1.0, max(-1.0, v))
        pcm[j] = int(v * 32767.0)
    if sys.byteorder == "big":
        pcm.byteswap()
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print("wrote %s: %.2f s, %d Hz stereo, peak-normalized (raw peak %.3f)"
          % (out, DUR, SR, peak))


if __name__ == "__main__":
    main()
