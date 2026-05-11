#!/usr/bin/env python3
"""Convert a TTF font into an Adafruit GFX `.h` bitmap-font header.

Usage:
  python tools/ttf_to_gfxfont.py path/to/font.ttf --size 15 --name jersey15 \
      > include/fonts/jersey15.h

Outputs three things in standard Adafruit GFX layout:
  - <name>Bitmaps[]: bit-packed glyph bitmaps (MSB-first, row-major)
  - <name>Glyphs[]:  GFXglyph entries (offset, w, h, xAdvance, dx, dy)
  - <name>:          GFXfont struct
"""

import argparse
import sys
from PIL import Image, ImageDraw, ImageFont


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("ttf")
    p.add_argument("--size", type=int, required=True, help="font size in px")
    p.add_argument("--name", required=True, help="C identifier (e.g. jersey15)")
    p.add_argument("--first", type=lambda x: int(x, 0), default=0x20)
    p.add_argument("--last", type=lambda x: int(x, 0), default=0x7E)
    p.add_argument("--threshold", type=int, default=128,
                   help="grayscale threshold for ON pixel (0..255)")
    args = p.parse_args()

    font = ImageFont.truetype(args.ttf, args.size)
    ascent, descent = font.getmetrics()
    y_advance = ascent + descent

    bitmaps: list[int] = []
    glyphs: list[tuple[int, int, int, int, int, int]] = []

    for code in range(args.first, args.last + 1):
        ch = chr(code)
        bbox = font.getbbox(ch)
        x0, y0, x1, y1 = bbox
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        x_advance = int(round(font.getlength(ch)))
        offset = len(bitmaps)
        if w > 0 and h > 0:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255)
            bits = []
            for yy in range(h):
                for xx in range(w):
                    bits.append(1 if img.getpixel((xx, yy)) >= args.threshold else 0)
            # Pack MSB-first into bytes (Adafruit GFX layout).
            for i in range(0, len(bits), 8):
                b = 0
                for j in range(8):
                    if i + j < len(bits):
                        b |= bits[i + j] << (7 - j)
                bitmaps.append(b)
        dy = y0 - ascent       # glyph top relative to baseline (negative when above)
        glyphs.append((offset, w, h, x_advance, x0, dy))

    out = sys.stdout
    name = args.name
    out.write("#pragma once\n")
    out.write("#include <Adafruit_GFX.h>\n\n")

    out.write(f"const uint8_t {name}Bitmaps[] PROGMEM = {{\n  ")
    for i, b in enumerate(bitmaps):
        out.write(f"0x{b:02X},")
        if (i + 1) % 16 == 0:
            out.write("\n  ")
        else:
            out.write(" ")
    if bitmaps and len(bitmaps) % 16 != 0:
        out.write("\n")
    out.write("};\n\n")

    out.write(f"const GFXglyph {name}Glyphs[] PROGMEM = {{\n")
    for code, g in zip(range(args.first, args.last + 1), glyphs):
        off, w, h, xa, dx, dy = g
        out.write(
            f"  {{ {off:5d}, {w:3d}, {h:3d}, {xa:3d}, {dx:4d}, {dy:4d} }},"
            f"  // 0x{code:02X} '{chr(code) if 0x20 <= code < 0x7F else '?'}'\n"
        )
    out.write("};\n\n")

    out.write(f"const GFXfont {name} PROGMEM = {{\n")
    out.write(f"  (uint8_t  *){name}Bitmaps,\n")
    out.write(f"  (GFXglyph *){name}Glyphs,\n")
    out.write(f"  0x{args.first:02X}, 0x{args.last:02X}, {y_advance} }};\n")
    out.write(f"// total bitmap bytes: {len(bitmaps)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
