# Adding & changing GIFs (celebrations + idle background)

Two GIF slots exist on the panel:

1. **Celebration animations** — played for ~10s when a Jira event fires (panda, david, etc.).
2. **Idle clock background** — looping bg behind the clock when nothing's happening.

Both use the same converter (`tools/gif_to_rgb565.py`). The difference is where you wire the resulting header.

The rest of this doc is the why behind these steps.

---

## Pipeline (local)

Adding a celebration works the same for **animated GIFs and static PNGs** — the converter accepts both (PNGs become 1-frame "animations"). `main.cpp` is **never edited** for an asset change; everything flows through the manifest and a generated header.

1. Drop your source (`your.gif` **or** `your.png`, 64×64) into `tools/gifs/`.
2. Add an entry to `tools/animations.json`:
   ```json
   { "kind": "celebration", "name": "myanim", "source": "your.gif", "args": [] }
   ```
   `args` is the converter flags (e.g. `["--bg-key", "255,255,255", "--circle-bg", "255,255,255", "--inset", "6"]` for an avatar PNG).
   Add `"optional": true` if the source isn't checked in yet — the build skips it instead of failing.
3. Regenerate + flash:
   ```bash
   python3 tools/build_animations.py
   pio run -t upload
   ```
4. Commit `tools/animations.json`, `include/animations/<name>.h`, and the updated `include/animations/_registry.h`.

That's it — no `main.cpp` change at any point.

### How the generated registry works

`tools/build_animations.py` writes `include/animations/_registry.h` containing:

- `#include` lines for every manifest entry (celebrations + idle_bg).
- A `drawX` wrapper function for every `celebration` entry.
- An `ASSET_ANIMATION_ENTRIES` macro used inside `main.cpp`'s `ANIMATIONS[]`.

`main.cpp` includes it once (after `drawGifFrame` is defined) and references the macro:
```cpp
#include "animations/_registry.h"
...
static const Animation ANIMATIONS[] = {
  { "rainbow",   drawRainbow,   false },
  { "fireworks", drawFireworks, false },
  { "hearts",    drawHearts,    false },
  { "confetti",  drawConfetti,  false },
  ASSET_ANIMATION_ENTRIES
};
```

Procedural animations (rainbow, fireworks, hearts, confetti) stay hand-coded in `main.cpp` since they aren't generated from sources.

`_registry.h` is **committed**, so a fresh clone can `pio run` without first running the Python script.

### Idle background

Same flow, but the manifest entry uses `"kind": "idle_bg"` and `"name": "clock_bg"`:
```json
{ "kind": "idle_bg", "name": "clock_bg", "source": "bg5.gif", "args": [] }
```
Only one entry should be named `clock_bg` (overwrite to swap bg).

### Running the build locally

```bash
cd led-panel-firmware
python3 tools/build_animations.py --list   # see the current inventory
python3 tools/build_animations.py          # regenerate headers + _registry.h
pio run -e esp32dev                        # build firmware
```

`--list` shows every asset-based animation declared in the manifest along with whether its source is present, optional/missing, or required-but-missing.

### Manifest fields

| Field | Purpose |
|---|---|
| `kind` | `celebration` or `idle_bg` — informational, helps human readers |
| `name` | C identifier and MQTT trigger name — what `main.cpp` references and what `DEVELOPER_ANIMATION_MAP` maps to |
| `source` | filename inside `tools/gifs/` (`.gif` or `.png`) |
| `args` | extra flags passed to `gif_to_rgb565.py` (e.g. avatar circle/inset) |
| `optional` | if `true`, build skips the entry when source is missing instead of failing — useful for animations whose `.h` is checked in but the source PNG isn't (e.g. existing avatars) |
| `note` | freeform comment shown in the manifest only — purely documentation |

---

## Quick reference

### Add a new celebration animation

```bash
cd led-panel-firmware
python3 tools/gif_to_rgb565.py tools/gifs/your.gif --name myanim > include/animations/myanim.h
```
Then in `src/main.cpp`:
1. `#include "animations/myanim.h"` (next to other anim includes)
2. Copy a wrapper function (e.g. `drawPanda`), rename to `drawMyAnim`, swap the constants.
3. Add `{ "myanim", drawMyAnim, true }` to `ANIMATIONS[]`.
4. `pio run -t upload`.

Trigger via MQTT (`myanim`, or `myanim-ws` to force the star border).

### Replace an existing celebration animation

```bash
python3 tools/gif_to_rgb565.py tools/gifs/new.gif --name panda > include/animations/panda.h
pio run -t upload
```
Same `--name` = drop-in. No code changes.

### Change the idle clock background

```bash
python3 tools/gif_to_rgb565.py tools/gifs/bg5.gif --name clock_bg > include/animations/clock_bg.h
pio run -t upload
```
No code changes. Skip `--circle-bg` / `--inset` for bgs — the bg fills the whole panel.

### Avatar-style portrait (white circle, inset for star border)

```bash
python3 tools/gif_to_rgb565.py tools/gifs/avatar.gif --name marcel \
  --bg-key "255,255,255" --circle-bg "255,255,255" --inset 6 \
  > include/animations/marcel.h
```

---

## Source GIF requirements

| Property | Required | Why |
|---|---|---|
| Dimensions | **exactly 64×64** | Panel size. Converter rejects mismatches. |
| Transparent background | strongly recommended | Lets us zero out the bg cleanly via the alpha channel. Opaque backgrounds need `--bg-key` (see below). |
| Frame count | any | More frames → bigger flash footprint (8 KB per frame). 30+ frames is fine. |
| Frame delays | any | Honored at playback. If a gif reports very low delays (0 or 10 ms) the converter clamps to `--min-delay` (default 30 ms). |

### Where to find gifs

- **[Tenor](https://tenor.com/)** — large library, easy to search ("celebration", "panda", "fireworks"). Right-click → Save Image As to download the .gif.
- **[Giphy](https://giphy.com/)** — similar; use the "Download" button.
- Pixel-art sprite GIFs (search "pixel art gif" + your theme) tend to look best — they're already low-detail and read clearly on a 64×64 LED grid.

### Resizing to 64×64

Most gifs you'll find are 200×200+ and need to be downsized first.

1. Open the gif in **[ezgif.com → Resize](https://ezgif.com/resize)**.
2. Set width and height both to **64**.
3. Pick a resample method:
   - **Nearest neighbor** for crisp pixel-art look (recommended for LED).
   - **Lanczos / Mitchell** for smoother detail (better for photographic gifs).
4. Click **Resize image** → **Save** the result.
5. Drop the saved file into `tools/gifs/`.

If the source gif isn't square (e.g. 240×180), use ezgif's **Crop** first to make it square, then resize to 64×64. Otherwise the resize will squash the aspect ratio.

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

## Changing the idle clock background

The idle screen (when no celebration is playing) shows the clock on top of a looping background gif. This is a single slot — there's one bg at a time, swapped at build time.

### Steps

1. **Drop a 64×64 gif** into `tools/gifs/` (e.g. `bg5.gif`).
2. **Regenerate the header** with the name `clock_bg` (drop-in replacement):
   ```bash
   cd led-panel-firmware
   python3 tools/gif_to_rgb565.py tools/gifs/bg5.gif --name clock_bg > include/animations/clock_bg.h
   ```
3. **Flash:**
   ```bash
   pio run -t upload
   ```

No code changes needed — `main.cpp` references `clock_bgFrames`, `clock_bgDelaysMs`, `CLOCK_BG_FRAMES`, `CLOCK_BG_TOTAL_MS`, `CLOCK_BG_W`, `CLOCK_BG_H`, all auto-generated from `--name clock_bg`.

### Flag differences vs celebration gifs

- **Don't** use `--circle-bg` or `--inset` — the bg fills the whole panel; no avatar framing.
- **Skip `--bg-key`** if you want the bg fully opaque. Most bg gifs look better solid; alpha-only handling (default) leaves true-transparent pixels black so the clock text composite still works if needed.

### Picking a good bg gif

- **Dark / low-contrast works best** — the white clock text must stay readable. Bright bgs (lots of white/yellow) wash out the time.
- **Loops smoothly** — a visible jump at the loop point is more obvious on a stationary-clock bg than on a 10s celebration.
- **Short loop is fine** — 20–40 frames at ~60ms each (~1.5–2.5s) keeps flash usage reasonable. Each frame = 8 KB.

### Static (single-frame) bg

Use a 1-frame gif (or a PNG round-tripped through a 1-frame gif). The compositing path is happy to hold frame 0 forever.

### Multiple bgs side-by-side

If you want to keep several bg headers in the tree (e.g. `bgNight.h`, `bgDay.h`) instead of overwriting `clock_bg.h`:

1. Generate with a different `--name` (e.g. `bgNight`).
2. Update `drawIdle` in `main.cpp` to reference `bgNightFrames` / `BG_NIGHT_*` macros — or add a selector.

For simple "swap the bg," just keep the name `clock_bg` and overwrite the header.

---

## Lessons learned (post-mortem)

These three issues showed up in sequence while bringing up the gif pipeline. They all looked like "ghosting" or "residue" but had different root causes.

| Symptom | Root cause | Fix |
|---|---|---|
| Every previous frame's pixels stayed lit indefinitely | Converter used a running canvas (`paste(rgba, mask=alpha)`) that **accumulated** frames on top of each other. Frame N's stored data contained frames 0..N composited. | Trust PIL: `src.seek(i); src.convert("RGBA")`. Disposal is handled automatically. |
| Panda body was wiped out, leaving only stripes/outlines | Auto-detected the gif's transparent-fill color (palette bg) as "the background" and zeroed it — but that color happened to match the panda's body. | Use the alpha channel instead of color matching. `alpha < 128 → 0x0000`. |
| Two pandas visible at once / mid-frame tearing | `drawGifFrame` was called every `loop()` iteration, re-blitting 4096 pixels into the live DMA buffer while the panel scanned it. | Frame-skip: only blit when `idx` changes since last call. |

Sanity test: when iterating on a new gif, look at the generated header's file size and the count of `0x0000` entries. Constant size across frames + a count roughly equal to `frames × transparent_pixels` means the converter is producing clean output.
