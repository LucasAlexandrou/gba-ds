#!/usr/bin/env python3
"""Renders the square GBA DS icon: two stacked rounded screens with a glass
sheen, lettered in the logo's style (GBA on the top screen, DS on the bottom).

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


def rounded(box, r):
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle(tuple(k(v) for v in box), radius=k(r), fill=255)
    return m


def gradient(top, bottom):
    """Vertical gradient across the whole canvas."""
    g = Image.new("RGBA", (1, 256))
    for y in range(256):
        t = y / 255
        g.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    return g.resize((S, S), Image.BILINEAR)


def screen(img, box):
    """One screen: navy rim, purple glass with a gradient, and a diagonal sheen."""
    x0, y0, x1, y1 = box
    rim, r = 22, 78
    img.paste(Image.new("RGBA", (S, S), NAVY), (0, 0), rounded(box, r))
    inner = (x0 + rim, y0 + rim, x1 - rim, y1 - rim)
    glass = rounded(inner, r - rim)
    h = y1 - y0
    grad = gradient((122, 104, 236), (58, 42, 156))
    # gradient spans this screen only
    band = grad.crop((0, 0, S, S)).resize((S, k(h)), Image.BILINEAR)
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    layer.paste(band, (0, k(y0)))
    img.paste(layer, (0, 0), glass)

    sheen = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(sheen)
    w = x1 - x0
    # broad soft highlight over the top half
    d.rounded_rectangle(tuple(k(v) for v in (x0 + rim, y0 + rim, x1 - rim, y0 + h * 0.48)),
                        radius=k(r - rim), fill=40)
    # two diagonal glints
    for off, width, a in ((0.18, 0.16, 70), (0.40, 0.06, 55)):
        bx = x0 + w * off
        d.polygon([(k(bx), k(y1)), (k(bx + w * width), k(y1)),
                   (k(bx + w * width + h * 0.9), k(y0)), (k(bx + h * 0.9), k(y0))], fill=a)
    sheen = Image.composite(sheen, Image.new("L", (S, S), 0), glass)
    white = Image.new("RGBA", (S, S), WHITE)
    white.putalpha(sheen)
    img.alpha_composite(white)
    return inner



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

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # two 3:2 screens stacked, filling the square
    m, gap = 40, 44
    sh = (1024 - 2 * m - gap) / 2
    sw = sh * 1.5
    x0 = (1024 - sw) / 2
    top = screen(img, (x0, m, x0 + sw, m + sh))
    bot = screen(img, (x0, m + sh + gap, x0 + sw, 1024 - m))

    cap = 400
    place(img, word("GBA", futura, cap, 88), tuple(k(v) for v in top), fill=0.80)
    place(img, word("DS", gamecuben, cap, 88), tuple(k(v) for v in bot), fill=0.66)

    img = img.resize((1024, 1024), Image.LANCZOS)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print(out)


if __name__ == "__main__":
    main()
