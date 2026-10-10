#!/usr/bin/env python3
"""Tile PNGs into a contact sheet: sheet.py OUT.png [--cols N] [--scale S] FILES..."""
import sys
from PIL import Image, ImageDraw
args = sys.argv[1:]
out = args.pop(0)
cols, scale = 4, 1
while args and args[0].startswith("--"):
    k = args.pop(0)
    if k == "--cols": cols = int(args.pop(0))
    if k == "--scale": scale = float(args.pop(0))
ims = [Image.open(f).convert("RGB") for f in args]
w = max(i.width for i in ims); h = max(i.height for i in ims)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * w, rows * (h + 10)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for n, (im, f) in enumerate(zip(ims, args)):
    x, y = (n % cols) * w, (n // cols) * (h + 10)
    sheet.paste(im, (x, y + 10))
    d.text((x + 2, y), f.split("/")[-1], fill=(255, 255, 0))
if scale != 1:
    sheet = sheet.resize((int(sheet.width * scale), int(sheet.height * scale)), Image.NEAREST)
sheet.save(out)
