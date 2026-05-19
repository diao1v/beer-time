# Bell tone (A2DP audio on celebration)

Every Jira celebration plays an IPO-style "DANG DANG DANG" bell tone through a paired Bluetooth speaker (BTP0419 in our case), in addition to the visual celebration.

The bell is synthesized on the fly in C++ — no PCM file is stored. The Python script `tools/bell_synth.py` is the design tool: edit the parameters, listen locally, then copy the chosen values into `src/bell.cpp`.

## How it works

```
MQTT led/command  →  onMessage()
                       ↓
                       Bell::trigger()     ← resets the synth state
                       ↓
              ┌──→ panel renders celebration (gif + border)
              │
              └──→ A2DP callback runs on Bluedroid task
                      ↓ (every ~22 ms, asks for ~512 frames)
                   Bell::fillFrames(frames, n)
                      ↓ computes 7 partials × decays × sin per sample
                      ↓ mixes 6 overlapping strikes
                   returns stereo int16 PCM at 44.1 kHz
                      ↓
                   [SBC encode] → AVDTP → BTP0419 speaker
```

## Tweaking the sound

1. Edit parameters at the top of `tools/bell_synth.py` (frequency, partials, decays, strike count, etc.).
2. Preview locally:
   ```bash
   cd led-panel-firmware
   python3 tools/bell_synth.py --play
   ```
3. Iterate until it sounds right. The script also prints `PEAK_NORMALIZE = X.XXXXXX` — copy that value.
4. Paste the new constants into the `PARAMS` block at the top of `src/bell.cpp`:
   - `FUNDAMENTAL_HZ`
   - `PARTIALS[]` (ratio, amp, decay per row)
   - `STRIKE_INTERVAL_S`, `STRIKE_DURATION_S`, `LAST_STRIKE_DURATION_S`, `LAST_STRIKE_DECAY_MULT`, `RING_COUNT`, `ATTACK_S`, `MASTER_VOLUME`
   - `PEAK_NORMALIZE` (from the Python print)
5. Build + ship via OTA: `git tag v0.x.y && git push origin v0.x.y`.

## Pairing the speaker (MAC-pinned)

The firmware identifies the speaker by **MAC address**, not by name. The target MAC is hardcoded in `include/secrets.h`:

```c
#define BT_PEER_NAME    "BTP0419"
#define BT_PEER_MAC     { 0x6B, 0xD2, 0xC8, 0x0F, 0xAC, 0x49 }
```

The `BT_PEER_NAME` is just for log messages; only `BT_PEER_MAC` controls who the ESP32 actually connects to. This is more stable than name-based connect: no name-collision risk, faster reconnect, and the library won't accidentally bind to a different device with a matching name.

### First-time setup

1. Put BTP0419 into pairing/discoverable mode (typically: power-on with the button held).
2. Power on the ESP32. Watch serial monitor for:
   ```
   [bt] a2dp source starting → BTP0419 (MAC-pinned)
   [bt] matched peer BTP0419 rssi=-53
   ```
3. The library connects within ~5–10 s. Subsequent reboots auto-reconnect using the bonded MAC.

### If the MAC changes

If you swap to a different speaker:

1. Get the new MAC (most easily via macOS Bluetooth dialog or a phone app).
2. Update `BT_PEER_MAC` in `secrets.h`.
3. Rebuild + flash (USB or OTA).

### If pairing fails

- The speaker is paired to another device (phone, laptop) — disconnect it from there first; BT classic speakers usually accept only one connection at a time.
- The MAC in `secrets.h` is wrong — verify with `bluetoothctl scan on` (Linux) or System Settings → Bluetooth (Mac).
- The speaker is out of range or off — the library retries silently in the background.

## Failure modes

| Scenario | Behavior |
|---|---|
| Speaker is off | Library retries connection silently in the background. Panel still celebrates visually; no audio. |
| Speaker out of range | Same as off — silent visual celebration; reconnects when back in range. |
| Speaker paired to another device | Won't connect. Disconnect the other device first. |
| Re-trigger during playback | New trigger restarts the bell from frame 0 — the in-progress bell is overwritten. |
| Audio mid-OTA | OTA reboot kills the audio task; new firmware starts fresh. |

## Why hardcode `PEAK_NORMALIZE`?

On-the-fly synthesis can't peak-detect a waveform it hasn't generated yet. The Python script does the full sequence in memory, finds the peak (after all partial/strike mixing), and prints it. The C++ side trusts that value to normalize each computed sample. If you tweak partials/decays/strikes and forget to update `PEAK_NORMALIZE`, the bell will either clip (peak got bigger) or sound quiet (peak got smaller).

The `tools/bell_synth.py` output line `PEAK_NORMALIZE = X.XXXXXX` is the single value to keep in sync.

## CPU and flash cost

- Per-sample: 8 partials × (sinf + expf) ≈ 16 FPU ops. At 88.2k samples/sec (stereo 44.1 kHz) that's ~1.4M FPU ops/sec, well under 2% of one ESP32 core.
- Bluedroid + ESP32-A2DP library: ~500–700 KB of flash. Project headroom checked at build time — if the build overflows the 1984 KB OTA slot, see `docs/animations.md` for trimming tips.
- Bluedroid RAM: ~30 KB.

## What's NOT implemented (yet)

- Per-developer custom sounds.
- Volume control via MQTT.
- A2DP sink mode (the panel is source-only; can't receive audio).
- LC3 / BLE Audio (would need ESP32-S3+).
