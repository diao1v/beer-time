#!/usr/bin/env python3
"""Convert an animated GIF into a C++ header of RGB565 frames + per-frame delays.

Usage:
  python tools/gif_to_rgb565.py path/to/anim.gif --name panda \
      > include/animations/panda.h

Handles GIF frame disposal correctly by compositing each frame onto a running
canvas (so partial-frame deltas render the way the GIF intends).
"""

import argparse
import sys

from PIL import Image


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("gif", help="path to animated GIF")
    p.add_argument("--name", required=True, help="C identifier prefix (e.g. panda)")
    p.add_argument("--width", type=int, default=64)
    p.add_argument("--height", type=int, default=64)
    p.add_argument(
        "--min-delay",
        type=int,
        default=30,
        help="minimum per-frame delay in ms (some gifs report 0/10)",
    )
    p.add_argument(
        "--bg-key",
        default="auto",
        help='background color to map to black. "auto" picks the most-common '
        'color in frame 0; "none" disables; or "R,G,B" e.g. "189,142,114".',
    )
    p.add_argument(
        "--bg-tolerance",
        type=int,
        default=24,
        help="max per-channel distance from bg color to also zero (anti-aliased edge pixels)",
    )
    p.add_argument(
        "--circle-bg",
        default="none",
        help='if set to "R,G,B", paint background pixels inside a centered '
        "circle (radius=min(w,h)/2) with this color, leaving outside the "
        "circle black. Useful for avatar-style portraits.",
    )
    p.add_argument(
        "--inset",
        type=int,
        default=0,
        help="shrink the source by INSET pixels on every side and pad with "
        "black, so the final image fits inside e.g. a star border. "
        "Example: --inset 6 → content rendered at 52x52 centered in 64x64.",
    )
    args = p.parse_args()

    src = Image.open(args.gif)
    if src.size != (args.width, args.height):
        print(
            f"error: gif is {src.size}, expected ({args.width},{args.height})",
            file=sys.stderr,
        )
        return 1

    # Convert each frame to RGBA so we can use the alpha channel to detect
    # gif-transparent pixels (they share color with the gif's palette bg, so
    # color-only detection fails when the bg matches the subject).
    frames_rgba: list[Image.Image] = []
    delays_ms: list[int] = []
    inner_w = args.width - 2 * args.inset
    inner_h = args.height - 2 * args.inset
    if args.inset and (inner_w <= 0 or inner_h <= 0):
        print(f"error: --inset {args.inset} too large for {args.width}x{args.height}",
              file=sys.stderr)
        return 1
    for i in range(getattr(src, "n_frames", 1)):
        src.seek(i)
        frame = src.convert("RGBA").copy()
        if args.inset > 0:
            shrunk = frame.resize((inner_w, inner_h), Image.LANCZOS)
            padded = Image.new("RGBA", (args.width, args.height), (0, 0, 0, 0))
            padded.paste(shrunk, (args.inset, args.inset))
            frame = padded
        frames_rgba.append(frame)
        delay = src.info.get("duration", 100)
        if delay < args.min_delay:
            delay = args.min_delay
        delays_ms.append(delay)

    bg_rgb: tuple[int, int, int] | None = None
    if args.bg_key == "auto":
        # Don't auto-detect by color anymore — alpha handles it. Leave None.
        pass
    elif args.bg_key != "none":
        parts = [int(x) for x in args.bg_key.split(",")]
        if len(parts) != 3:
            print("error: --bg-key must be R,G,B or 'auto'/'none'", file=sys.stderr)
            return 1
        bg_rgb = (parts[0], parts[1], parts[2])

    circle_bg: tuple[int, int, int] | None = None
    if args.circle_bg != "none":
        parts = [int(x) for x in args.circle_bg.split(",")]
        if len(parts) != 3:
            print("error: --circle-bg must be R,G,B or 'none'", file=sys.stderr)
            return 1
        circle_bg = (parts[0], parts[1], parts[2])
    cx = args.width / 2 - 0.5
    cy = args.height / 2 - 0.5
    # Circle sizes itself to the *content* area when inset is used, so it
    # doesn't bleed into the padding ring that the star border will occupy.
    radius = min(args.width - 2 * args.inset, args.height - 2 * args.inset) / 2

    name = args.name
    upper = name.upper()
    n = len(frames_rgba)
    out = sys.stdout

    out.write("#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n\n")
    out.write(f"#define {upper}_FRAMES {n}\n")
    out.write(f"#define {upper}_W      {args.width}\n")
    out.write(f"#define {upper}_H      {args.height}\n\n")

    out.write(
        f"const uint16_t {name}Frames[{upper}_FRAMES][{upper}_W * {upper}_H] PROGMEM = {{\n"
    )
    tol = args.bg_tolerance
    for img in frames_rgba:
        out.write("  {\n   ")
        px = img.load()
        for y in range(args.height):
            for x in range(args.width):
                r, g, b, a = px[x, y]
                is_bg = a < 128 or (
                    bg_rgb is not None
                    and abs(r - bg_rgb[0]) <= tol
                    and abs(g - bg_rgb[1]) <= tol
                    and abs(b - bg_rgb[2]) <= tol
                )
                if is_bg:
                    inside_circle = (
                        circle_bg is not None
                        and ((x - cx) ** 2 + (y - cy) ** 2) <= radius * radius
                    )
                    if inside_circle:
                        out.write(
                            f" 0x{rgb_to_565(*circle_bg):04X},"
                        )
                    else:
                        out.write(" 0x0000,")
                else:
                    out.write(f" 0x{rgb_to_565(r, g, b):04X},")
            out.write("\n   ")
        out.write("\n  },\n")
    out.write("};\n\n")

    out.write(f"const uint16_t {name}DelaysMs[{upper}_FRAMES] PROGMEM = {{\n  ")
    for i, d in enumerate(delays_ms):
        out.write(f"{d},")
        if (i + 1) % 16 == 0:
            out.write("\n  ")
    out.write("\n};\n")

    total = sum(delays_ms)
    out.write(f"\n#define {upper}_TOTAL_MS {total}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
