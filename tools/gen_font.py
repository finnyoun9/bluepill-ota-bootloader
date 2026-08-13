#!/usr/bin/env python3
"""Generate 16x16 Chinese glyph data for tft_chinese_font.c.

Renders each character with Microsoft YaHei at 16 px, centers the ink box in a
16x16 grid, binarizes at threshold 128, and packs 16 rows x 2 bytes with the
MSB of each row at x=0 (matching tft_st7789_draw_chinese's
`bits & (1 << (15 - source_x))`).

Usage:
    python tools/gen_font.py            # print ASCII art + C snippet
    python tools/gen_font.py --write    # print C snippet only (for appending)
"""

import sys

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/msyh.ttc"
SIZE = 16
THRESHOLD = 128

CHARS = [
    "旋", "转", "选", "择", "按", "下", "确", "认",
    "切", "换", "返", "回", "菜", "单",
]


def render_glyph(ch: str) -> Image.Image:
    font = ImageFont.truetype(FONT_PATH, SIZE)
    pad = 8
    canvas = Image.new("L", (SIZE + 2 * pad, SIZE + 2 * pad), 0)
    draw = ImageDraw.Draw(canvas)
    draw.text((pad, pad), ch, font=font, fill=255)
    canvas = canvas.point(lambda p: 255 if p > THRESHOLD else 0)
    left, top, right, bottom = canvas.getbbox()
    glyph = canvas.crop((left, top, right, bottom))
    w = right - left
    h = bottom - top
    out = Image.new("L", (SIZE, SIZE), 0)
    out.paste(glyph, ((SIZE - w) // 2, (SIZE - h) // 2))
    return out


def pack(img: Image.Image) -> list[int]:
    bytes_ = []
    for y in range(SIZE):
        row = 0
        for x in range(SIZE):
            if img.getpixel((x, y)):
                row |= 1 << (15 - x)
        bytes_.append((row >> 8) & 0xFF)
        bytes_.append(row & 0xFF)
    return bytes_


def ascii_art(img: Image.Image, ch: str) -> str:
    lines = [f"  {ch}  U+{ord(ch):04X}"]
    for y in range(SIZE):
        lines.append("  " + "".join("#" if img.getpixel((x, y)) else "." for x in range(SIZE)))
    return "\n".join(lines)


def c_line(ch: str, data: list[int]) -> str:
    body = ",".join(f"0x{b:02X}U" for b in data)
    return f"    {{0x{ord(ch):04X}U,{{{body}}}}}, /* {ch} */"


def main() -> int:
    write_only = "--write" in sys.argv
    for ch in CHARS:
        img = render_glyph(ch)
        data = pack(img)
        if not write_only:
            print(ascii_art(img, ch))
            print()
        print(c_line(ch, data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
