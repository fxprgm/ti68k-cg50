#!/usr/bin/env python3
"""Draw TI68k's MAIN MENU icons (92x64: assets-cg/icon-uns.png, icon-sel.png) with Pillow.
The OS writes the add-in's name over the bottom rows, so the picture stays in the top 48."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))


def font(size):
    for f in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def icon(selected):
    im = Image.new("RGB", (92, 64), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((2, 2, 89, 47), 7, fill=(70, 22, 22) if not selected else (170, 50, 40))
    # a little TI-89: a wide gray-green screen with a curve, rows of keys below
    d.rounded_rectangle((6, 5, 40, 44), 4, fill=(48, 48, 54), outline=(205, 205, 210))
    d.rectangle((9, 8, 37, 22), fill=(196, 206, 176))
    d.line((10, 18, 14, 12, 18, 11, 22, 16, 26, 20, 30, 19, 35, 12), fill=(20, 30, 20), width=1)
    for r in range(4):
        for c in range(5):
            x, y = 10 + 5 * c, 26 + 4 * r
            d.rectangle((x, y, x + 3, y + 2), fill=(225, 225, 230) if r else (240, 200, 60))
    d.text((45, 5), "TI", font=font(17), fill=(255, 220, 90))
    d.text((45, 25), "68k", font=font(15), fill=(255, 255, 255))
    return im


os.makedirs(os.path.join(HERE, "assets-cg"), exist_ok=True)
icon(False).save(os.path.join(HERE, "assets-cg", "icon-uns.png"))
icon(True).save(os.path.join(HERE, "assets-cg", "icon-sel.png"))
print("wrote assets-cg/icon-uns.png, icon-sel.png")
