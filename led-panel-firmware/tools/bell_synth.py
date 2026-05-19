#!/usr/bin/env python3
"""Synthesize an IPO-style ringing bell and save as a WAV for previewing.

Same math we'll port to the ESP32 A2DP code. Tweak the parameters at the
top, run, listen, iterate. When you're happy with the sound, the final
parameters get baked into the firmware.

Usage:
  cd led-panel-firmware
  python3 tools/bell_synth.py --play
  python3 tools/bell_synth.py --out tools/preview/bell.wav

The --play flag invokes the system audio player (afplay on macOS, aplay on
Linux, or mpv as a fallback) so you don't have to open the file manually.
"""

import argparse
import math
import struct
import subprocess
import wave
from pathlib import Path

# ============================================================================
# TWEAKABLE PARAMETERS — adjust these until it sounds right.
# ============================================================================

SAMPLE_RATE     = 44100   # Hz; CD quality. ESP32 will downsample.
FUNDAMENTAL_HZ  = 740     # lower fundamental → more "dang" weight

# Inharmonic bell partials — the non-integer ratios (1.183, 1.506, 2.61) are
# what makes it sound like struck metal rather than an electronic chime.
# Reference: Rossing's bell-acoustics work on the "Strike Five" pattern.
#
# Each partial: (frequency_ratio, relative_amplitude, decay_rate_per_second).
# Lower decay = longer metallic tail. Top partials are quieter + decay faster
# so the perceptual pitch settles on the prime/nominal after the initial clang.
PARTIALS = [
    # ratio,  amp,   decay
    (0.50,   0.45,  2.5),    # hum — sub-octave undertone, long metallic tail
    (1.00,   1.00,  4.0),    # prime — main "dang" pitch
    (1.183,  0.65,  5.5),    # tierce — inharmonic minor 3rd, the BELL CHARACTER
    (1.506,  0.45,  6.5),    # quint — inharmonic fifth
    (2.00,   0.55,  5.5),    # nominal — octave above prime
    (2.61,   0.30,  9.0),    # twelfth-ish, inharmonic
    (3.84,   0.18,  13.0),   # high partial — adds "clang" attack
    (5.42,   0.10,  20.0),   # ultra-high shimmer, fades immediately
]

# Timing — overlapping strikes like a bell tower.
# STRIKE_INTERVAL is how often a new strike hits (the perceived "pace").
# STRIKE_DURATION is how long each strike is rendered; if it's longer than
# STRIKE_INTERVAL, strikes overlap and ring through each other.
STRIKE_INTERVAL          = 0.30   # 0.30s between onsets → ~3.3 dangs/sec
STRIKE_DURATION          = 1.20   # each strike's tail rings ~1.2s
LAST_STRIKE_DURATION     = 2.80   # final dang rings out almost 3s
LAST_STRIKE_DECAY_MULT   = 0.55   # slow the last strike's decay → sustain
RING_COUNT               = 6      # number of strikes (incl. the final long one)

ATTACK_MS       = 3       # crisp attack with a tiny ramp to avoid click
MASTER_VOLUME   = 0.85    # 0.0–1.0; headroom for the mixed (overlapping) signal

# ============================================================================


def synthesize_strike(duration_s: float, decay_mult: float = 1.0) -> list[float]:
    """One bell strike: all partials sum, each with its own exp decay.

    `decay_mult` scales every partial's decay rate. Use < 1.0 to make the
    strike sustain longer (e.g. for the final ring-out).
    """
    n_samples = int(duration_s * SAMPLE_RATE)
    attack_n = int(ATTACK_MS / 1000.0 * SAMPLE_RATE)
    out: list[float] = []
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        attack = min(1.0, i / attack_n) if attack_n > 0 else 1.0
        s = 0.0
        for ratio, amp, decay in PARTIALS:
            freq = FUNDAMENTAL_HZ * ratio
            s += amp * math.sin(2.0 * math.pi * freq * t) * math.exp(-decay * decay_mult * t)
        out.append(s * attack)
    return out


def write_wav(path: Path, samples: list[float]) -> None:
    peak = max(abs(s) for s in samples) or 1.0
    # Print the peak so the firmware's bell.h can hardcode the same scale
    # (on-the-fly synthesis on the ESP32 can't compute peak after the fact).
    print(f"PEAK_NORMALIZE = {peak:.6f}  # paste into src/bell.h after tweaking params")
    scale = MASTER_VOLUME / peak * 32767
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        # Write in chunks of 1024 samples for speed
        chunk: list[bytes] = []
        for s in samples:
            v = max(-32768, min(32767, int(s * scale)))
            chunk.append(struct.pack("<h", v))
        w.writeframes(b"".join(chunk))


def try_play(path: Path) -> None:
    candidates = [
        ["afplay", str(path)],
        ["aplay", str(path)],
        ["mpv", "--no-audio-display", "--quiet", str(path)],
        ["paplay", str(path)],
    ]
    for cmd in candidates:
        try:
            subprocess.run(cmd, check=True)
            return
        except FileNotFoundError:
            continue
        except subprocess.CalledProcessError:
            return
    print("no audio player found (tried afplay/aplay/mpv/paplay).")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="tools/preview/bell.wav", help="output WAV path")
    p.add_argument("--play", action="store_true", help="play the result immediately")
    args = p.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    # Render two strike templates: normal ones for strikes 0..N-2, and a longer
    # slower-decaying one for the final strike (so the bell rings out at the end).
    strike_normal = synthesize_strike(STRIKE_DURATION)
    strike_final = synthesize_strike(LAST_STRIKE_DURATION, decay_mult=LAST_STRIKE_DECAY_MULT)

    # Total buffer = space for all strike onsets + the final strike's full tail.
    total_duration = (RING_COUNT - 1) * STRIKE_INTERVAL + LAST_STRIKE_DURATION
    total_samples = int(total_duration * SAMPLE_RATE)
    buf: list[float] = [0.0] * total_samples

    # Mix each strike into the buffer at its onset. Overlapping tails simply
    # add together — peaks above 1.0 are normalized away in write_wav.
    for i in range(RING_COUNT):
        onset = int(i * STRIKE_INTERVAL * SAMPLE_RATE)
        src = strike_final if i == RING_COUNT - 1 else strike_normal
        for j, s in enumerate(src):
            idx = onset + j
            if idx < total_samples:
                buf[idx] += s

    write_wav(out, buf)
    print(f"wrote {out} ({total_duration:.2f}s, {total_samples} samples @ {SAMPLE_RATE}Hz)")

    if args.play:
        try_play(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
