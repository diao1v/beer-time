#!/usr/bin/env python3
"""Convert a horizontal sprite sheet PNG into a C header of RGB565 frames.

Usage:
  python tools/png_to_rgb565.py sheet.png --frames 8 --name celebrate \
      > include/animations/celebrate.h

Sheet layout: width = frames * 16, height = 16. One row of frames.
"""

import argparse
import sys
from PIL import Image


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("sheet", help="path to PNG sprite sheet")
    p.add_argument("--frames", type=int, required=True)
    p.add_argument("--name", required=True, help="C identifier prefix")
    p.add_argument("--width", type=int, default=16)
    p.add_argument("--height", type=int, default=16)
    args = p.parse_args()

    img = Image.open(args.sheet).convert("RGB")
    expected = (args.frames * args.width, args.height)
    if img.size != expected:
        print(
            f"error: sheet is {img.size}, expected {expected}",
            file=sys.stderr,
        )
        return 1

    name = args.name
    upper = name.upper()
    out = sys.stdout

    out.write("#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n\n")
    out.write(f"#define {upper}_FRAMES {args.frames}\n")
    out.write(f"#define {upper}_W      {args.width}\n")
    out.write(f"#define {upper}_H      {args.height}\n\n")
    out.write(
        f"const uint16_t {name}Frames[{upper}_FRAMES][{upper}_W * {upper}_H] PROGMEM = {{\n"
    )

    for f in range(args.frames):
        out.write("  {\n    ")
        for y in range(args.height):
            for x in range(args.width):
                r, g, b = img.getpixel((f * args.width + x, y))
                out.write(f"0x{rgb_to_565(r, g, b):04X},")
            out.write("\n    ")
        out.write("\n  },\n")
    out.write("};\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
