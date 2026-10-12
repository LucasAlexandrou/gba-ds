#!/usr/bin/env python3
"""Renders the square GBA DS icon: a purple horizontal handheld silhouette with a
second screen rising above the first, white screens and buttons, and the logo's
lettering (GBA on the top screen, DS on the bottom).

    tools/make_icon.py FUTURA_BOLD_ITALIC.otf GAMECUBEN.ttf [OUT.png]

Default output: assets/brand/gba-ds-icon.png (1024x1024, transparent). Needs Pillow.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
K = 4                      # supersampling
S = 1024 * K
PURPLE = (92, 72, 196, 255)
WHITE = (255, 255, 255, 255)
NAVY = (16, 32, 96, 255)


def k(v):
    return int(round(v * K))


def smooth_mask(mask, r):
    """Round off concave corners (fillets) and convex ones a little: blur, threshold."""
    return mask.filter(ImageFilter.GaussianBlur(k(r))).point(lambda v: 255 if v >= 128 else 0)


def sized(path, cap):
    f = ImageFont.truetype(path, 1000)
    b = f.getbbox("H")
    return ImageFont.truetype(path, round(1000 * cap / (b[3] - b[1])))


def word(text, font_path, cap, sw):
    """One word in the logo's style: white fill, thick navy outline merged into one silhouette."""
    font = sized(font_path, cap)
    track = 0.012 * cap
    xs, x = [], 0.0
    for c in text:
        xs.append(x)
        x += font.getlength(c) - track
    pad = sw + cap
    img = Image.new("RGBA", (int(x + 2 * pad), int(cap * 2 + 2 * pad)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    top = pad + cap * 0.5 - font.getbbox("H")[1]
    for c, gx in zip(text, xs):
        d.text((pad + gx, top), c, font=font, fill=NAVY, stroke_width=sw, stroke_fill=NAVY)
    a = img.getchannel("A").point(lambda v: 255 if v > 0 else 0)
    ImageDraw.floodfill(a, (0, 0), 128)
    holes = a.point(lambda v: 255 if v == 0 else 0).filter(ImageFilter.MaxFilter(7))
    img.paste(Image.new("RGBA", img.size, NAVY), (0, 0), holes)
    for c, gx in zip(text, xs):
        d.text((pad + gx, top), c, font=font, fill=WHITE)
    return img.crop(img.getbbox())


def place(canvas, img, box, fill=0.78):
    """Paste img centred in box (x0, y0, x1, y1), scaled to `fill` of the box's width/height."""
    bw, bh = box[2] - box[0], box[3] - box[1]
    s = min(bw * fill / img.width, bh * fill / img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    canvas.alpha_composite(img, (box[0] + (bw - img.width) // 2, box[1] + (bh - img.height) // 2))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    futura, gamecuben = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, "assets", "brand", "gba-ds-icon.png")

    # ---- body: the usual wide handheld, plus a housing for the second screen above
    body = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(body)
    bx0, by0, bx1, by1 = 56, 500, 968, 884
    d.rounded_rectangle((k(bx0), k(by0 + 24), k(bx1), k(by1)), radius=k(176), fill=255)   # grips
    d.rounded_rectangle((k(200), k(by0), k(824), k(by1)), radius=k(60), fill=255)         # centre
    # shoulders: the grips rise slightly above the centre section
    d.ellipse((k(bx0 + 20), k(by0 - 6), k(bx0 + 300), k(by0 + 200)), fill=255)
    d.ellipse((k(bx1 - 300), k(by0 - 6), k(bx1 - 20), k(by0 + 200)), fill=255)
    hx0, hx1, hy0 = 240, 784, 120                                                          # top-screen housing
    d.rounded_rectangle((k(hx0), k(hy0), k(hx1), k(by0 + 80)), radius=k(64), fill=255)
    body = smooth_mask(body, 10)

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(Image.new("RGBA", (S, S), PURPLE), (0, 0), body)

    # ---- white parts
    w = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(w)
    sw_, sh_ = 372, 248                       # screens, 3:2
    sx0 = 512 - sw_ // 2
    top_scr = (sx0, 180, sx0 + sw_, 180 + sh_)
    bot_scr = (sx0, 560, sx0 + sw_, 560 + sh_)
    for r in (top_scr, bot_scr):
        d.rounded_rectangle(tuple(k(v) for v in r), radius=k(14), fill=255)
    # d-pad
    cx, cy, arm, th = 170, 676, 66, 40
    d.rounded_rectangle((k(cx - arm), k(cy - th / 2), k(cx + arm), k(cy + th / 2)), radius=k(8), fill=255)
    d.rounded_rectangle((k(cx - th / 2), k(cy - arm), k(cx + th / 2), k(cy + arm)), radius=k(8), fill=255)
    # A / B
    for (x, y) in ((806, 708), (880, 650)):
        d.ellipse((k(x - 36), k(y - 36), k(x + 36), k(y + 36)), fill=255)
    # start / select
    for y in (790, 832):
        d.ellipse((k(250 - 13), k(y - 13), k(250 + 13), k(y + 13)), fill=255)
    # power light
    d.ellipse((k(878 - 9), k(566 - 9), k(878 + 9), k(566 + 9)), fill=255)
    # speaker slits
    for i in range(5):
        x = 802 + i * 22
        d.rounded_rectangle((k(x), k(782 + i * 4), k(x + 8), k(842 - (4 - i) * 0)), radius=k(4), fill=255)
    img.paste(Image.new("RGBA", (S, S), WHITE), (0, 0), w)

    # ---- lettering in the logo's style
    cap = 400
    place(img, word("GBA", futura, cap, 88), tuple(k(v) for v in top_scr))
    place(img, word("DS", gamecuben, cap, 88), tuple(k(v) for v in bot_scr), fill=0.62)

    img = img.crop(img.getbbox())
    # centre on a square canvas with a small margin
    side = max(img.width, img.height) + k(48)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.alpha_composite(img, ((side - img.width) // 2, (side - img.height) // 2))
    sq = sq.resize((1024, 1024), Image.LANCZOS)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sq.save(out)
    print(out)


if __name__ == "__main__":
    main()
