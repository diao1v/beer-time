# Adding & changing GIF animations

Animations on the LED panel come from two sources:

1. **Procedural** (rainbow, fireworks, hearts, confetti) — pure C++ functions in `src/main.cpp`.
2. **GIF-based** (panda, panda2) — pre-converted to RGB565 frame data in `include/animations/<name>.h` and blitted by a shared helper.

This doc covers the GIF path: adding a new gif, replacing an existing one, and the pitfalls we hit during the initial setup.

---

## TL;DR — add a new gif in 4 steps

1. Drop a **64×64** animated `.gif` (transparent background recommended) into `tools/gifs/`.
2. Generate a header:
   ```bash
   cd led-panel-firmware
   python3 tools/gif_to_rgb565.py tools/gifs/your.gif --name myanim > include/animations/myanim.h
   ```
3. In `src/main.cpp`:
   - Add `#include "animations/myanim.h"` next to the other anim includes.
   - Add a wrapper function (copy the panda wrapper, swap the name).
   - Register it in the `ANIMATIONS[]` table.
4. Flash: `pio run -t upload`. Trigger it via MQTT or by mapping a developer to it in `DEVELOPER_ANIMATION_MAP`.

The rest of this doc is the why.

---

## Source GIF requirements

| Property | Required | Why |
|---|---|---|
| Dimensions | **exactly 64×64** | Panel size. Converter rejects mismatches. |
| Transparent background | strongly recommended | Lets us zero out the bg cleanly via the alpha channel. Opaque backgrounds need `--bg-key` (see below). |
| Frame count | any | More frames → bigger flash footprint (8 KB per frame). 30+ frames is fine. |
| Frame delays | any | Honored at playback. If a gif reports very low delays (0 or 10 ms) the converter clamps to `--min-delay` (default 30 ms). |

If your gif is **not** 64×64, resize it first (e.g. ezgif.com → "Resize" → 64×64, nearest neighbor for pixel art).

---

## The converter: `tools/gif_to_rgb565.py`

Generates a C++ header containing:

```cpp
#define MYANIM_FRAMES   N
#define MYANIM_W        64
#define MYANIM_H        64
#define MYANIM_TOTAL_MS <sum of delays>

const uint16_t myanimFrames[N][64*64] PROGMEM = { ... };  // RGB565
const uint16_t myanimDelaysMs[N] PROGMEM = { ... };       // per-frame delay
```

### Background handling (the part that bit us)

GIFs encode "no background" as **transparent pixels**, but PIL's `convert("RGB")` fills transparent areas with the gif's palette background color — which may happen to match the subject. We hit this with the red panda gif: its palette background was (189, 142, 114), the exact same as the panda's body. Color-based detection wiped out the panda.

The converter now reads the **alpha channel** (`convert("RGBA")`) and zeros any pixel with `alpha < 128`. This works regardless of what color the transparent pixels were filled with.

### Flags

| Flag | Default | Use |
|---|---|---|
| `--name` | required | C identifier prefix (e.g. `panda`, `myanim`). |
| `--width` / `--height` | `64` | Panel dimensions. |
| `--min-delay` | `30` | Floor for per-frame delays in ms. |
| `--bg-key` | `auto` | `"none"` to disable bg removal; `"R,G,B"` to also zap pixels close to a specific color (in addition to transparency). Useful for **opaque** gifs where the alpha trick alone won't help. |
| `--bg-tolerance` | `24` | Max per-channel distance from `--bg-key` to also zero. |

### Examples

```bash
# Transparent-background gif (most common case)
python3 tools/gif_to_rgb565.py tools/gifs/fireworks.gif --name fireworks > include/animations/fireworks.h

# Opaque gif with a solid white background we want black
python3 tools/gif_to_rgb565.py tools/gifs/heart.gif --name heart \
  --bg-key "255,255,255" --bg-tolerance 20 \
  > include/animations/heart.h

# Disable bg removal entirely
python3 tools/gif_to_rgb565.py tools/gifs/full-bleed.gif --name fullbleed \
  --bg-key none \
  > include/animations/fullbleed.h
```

---

## Wiring it into the firmware

Each gif animation needs three things in `src/main.cpp`:

### 1. Include the header

```cpp
#include "animations/panda.h"
#include "animations/panda2.h"
#include "animations/myanim.h"      // <-- add
```

### 2. Add a wrapper function

Wrappers exist so each gif has its own `lastIdx` state for the frame-skip optimization. Copy an existing one:

```cpp
static void drawMyAnim(uint32_t elapsed) {
  static int16_t lastIdx = -1;
  drawGifFrame(elapsed, &myanimFrames[0][0], myanimDelaysMs,
               MYANIM_FRAMES, MYANIM_TOTAL_MS, MYANIM_W, MYANIM_H, lastIdx);
}
```

### 3. Register in the table

```cpp
static const Animation ANIMATIONS[] = {
  { "rainbow",   drawRainbow },
  ...
  { "panda",     drawPanda },
  { "panda2",    drawPanda2 },
  { "myanim",    drawMyAnim },   // <-- add
};
```

The string here is what MQTT clients publish and what `DEVELOPER_ANIMATION_MAP` references. **Lowercase, no spaces**, since lookup is case-insensitive but spaces in MQTT payloads are awkward.

---

## Replacing an existing gif

Two common cases.

### Same name (drop-in replacement)

```bash
# Just regenerate the header with the same --name
python3 tools/gif_to_rgb565.py tools/gifs/new-panda.gif --name panda > include/animations/panda.h
pio run -t upload
```

No code changes needed. The DEVELOPER_ANIMATION_MAP entries that referenced `panda` continue to work, now playing the new gif.

### Renaming

If you want to drop a gif entirely and add a new one with a different name:

1. Delete `include/animations/old.h`.
2. Remove the `#include`, the `drawOld` wrapper, and the `{ "old", drawOld }` row from `ANIMATIONS[]`.
3. Update any `DEVELOPER_ANIMATION_MAP` entries referencing `old` — otherwise they'll fall back to `rainbow` (the unknown-name default).
4. Add the new gif following the 4-step process above.

---

## Celebration duration

The panel plays the chosen animation on a continuous loop for `CELEBRATE_MS` milliseconds, then returns to idle. Configure in `src/main.cpp`:

```cpp
const uint32_t CELEBRATE_MS = 15000;  // 15s per celebration
```

The gif's own length (e.g. niebuu-panda is ~1.0 s) is independent — short gifs simply loop more times within the celebration window. Long gifs (e.g. 53-frame tagay was ~4.5 s) play through ~3× in a 15 s window.

---

## Flash & partition considerations

Each 64×64 RGB565 frame is **8 192 bytes** of flash:

| Frames | Flash used per gif |
|---|---|
| 6 (panda) | 49 KB |
| 24 (panda2) | 196 KB |
| 53 (tagay, removed) | 434 KB |

`platformio.ini` already sets:
```ini
board_build.partitions = huge_app.csv
```
…which gives the app 3 MB of the 4 MB flash. You can fit roughly 350 frames of 64×64 gif data before hitting that ceiling.

If you blow past it, the build fails with something like `region 'iram0_0_seg' overflowed`. Options:
- Reduce frame counts (sample gif at lower fps).
- Move to LittleFS + runtime GIF decoder (out of scope for this firmware right now).

---

## Discovery mode (debugging)

There's a second PlatformIO env `discovery` that boots the panel, skips WiFi/MQTT, and loops a single hardcoded animation. Use it when iterating on:
- A new gif's converter output
- Panel hardware tuning (latch_blanking, brightness)
- Rendering bugs

```bash
pio run -e discovery -t upload
pio device monitor -e discovery
```

To switch which animation discovery plays, edit the body of:
```cpp
#ifdef DISCOVERY_MODE
  drawPanda2(millis());   // <-- swap to your animation
  return;
#endif
```

---

## Lessons learned (post-mortem)

These three issues showed up in sequence while bringing up the gif pipeline. They all looked like "ghosting" or "residue" but had different root causes.

| Symptom | Root cause | Fix |
|---|---|---|
| Every previous frame's pixels stayed lit indefinitely | Converter used a running canvas (`paste(rgba, mask=alpha)`) that **accumulated** frames on top of each other. Frame N's stored data contained frames 0..N composited. | Trust PIL: `src.seek(i); src.convert("RGBA")`. Disposal is handled automatically. |
| Panda body was wiped out, leaving only stripes/outlines | Auto-detected the gif's transparent-fill color (palette bg) as "the background" and zeroed it — but that color happened to match the panda's body. | Use the alpha channel instead of color matching. `alpha < 128 → 0x0000`. |
| Two pandas visible at once / mid-frame tearing | `drawGifFrame` was called every `loop()` iteration, re-blitting 4096 pixels into the live DMA buffer while the panel scanned it. | Frame-skip: only blit when `idx` changes since last call. |

Sanity test: when iterating on a new gif, look at the generated header's file size and the count of `0x0000` entries. Constant size across frames + a count roughly equal to `frames × transparent_pixels` means the converter is producing clean output.
