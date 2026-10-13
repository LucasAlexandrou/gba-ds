#!/usr/bin/env python3
"""Renders the GBA DS branding images and embeds them in src/brand_data.c.

    tools/make_brand.py FUTURA_BOLD_ITALIC.otf GAMECUBEN.ttf

The font files are not part of the repository (Futura is a commercial font);
only the rendered images are. Outputs:

    assets/brand/gba-ds-logo.png        full-size logo (transparent)
    assets/brand/gba-ds-logo-1024.png   1024 px wide
    src/brand_data.c                    PNGs used by the app, as byte arrays

Needs Pillow.
"""
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WHITE = (255, 255, 255, 255)
NAVY = (16, 32, 96, 255)  # #102060


def sized(path, cap):
    """Font at the size whose capital H is `cap` px tall."""
    f = ImageFont.truetype(path, 1000)
    b = f.getbbox("H")
    return ImageFont.truetype(path, round(1000 * cap / (b[3] - b[1])))


def logo(futura, gamecuben):
    """GBA (Futura Bold Italic) + DS (Gamecuben, 75% height), white with a thick navy outline."""
    cap = 400
    fut, gc = sized(futura, cap), sized(gamecuben, cap * 0.75)
    sw = 88                      # outline thickness
    track, wgap = 0.012, 0.13    # letter spacing removed / gap between words, x cap height
    glyphs, x = [], 0.0
    for word, font in (("GBA", fut), ("DS", gc)):
        if glyphs:
            x += wgap * cap
        for c in word:
            glyphs.append((font, c, x))
            x += font.getlength(c) - track * cap
    pad = sw + 200
    img = Image.new("RGBA", (int(x + 2 * pad + cap), int(cap * 2 + 2 * pad)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    base = pad + cap * 1.3

    def pos(font, gx):
        return (pad + gx, base - font.getbbox("H")[3])

    for font, c, gx in glyphs:
        d.text(pos(font, gx), c, font=font, fill=NAVY, stroke_width=sw, stroke_fill=NAVY)
    # one solid silhouette: fill pinholes the stroke leaves inside letters (B's counters)
    a = img.getchannel("A").point(lambda v: 255 if v > 0 else 0)
    ImageDraw.floodfill(a, (0, 0), 128)
    holes = a.point(lambda v: 255 if v == 0 else 0).filter(ImageFilter.MaxFilter(7))
    img.paste(Image.new("RGBA", img.size, NAVY), (0, 0), holes)
    for font, c, gx in glyphs:
        d.text(pos(font, gx), c, font=font, fill=WHITE)
    img = img.crop(img.getbbox())
    return img.crop((-24, -24, img.width + 24, img.height + 24))


def button(futura, letter, size=96):
    """White disc with a navy letter, drawn 4x and scaled down for smooth edges."""
    k = 4
    s = size * k
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((0, 0, s - 1, s - 1), fill=WHITE)
    font = sized(futura, s * 0.5)
    b = font.getbbox(letter)
    d.text(((s - (b[0] + b[2])) / 2, (s - (b[1] + b[3])) / 2), letter, font=font, fill=NAVY)
    return img.resize((size, size), Image.LANCZOS)


def label(futura, text, cap=48):
    """Text in Futura Bold Italic, white on transparent."""
    font = sized(futura, cap)
    b = font.getbbox(text)
    pad = cap // 4
    img = Image.new("RGBA", (b[2] - b[0] + 2 * pad, b[3] - b[1] + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((pad - b[0], pad - b[1]), text, font=font, fill=WHITE)
    return img


def pill(futura, text, w=100, h=64):
    """Shoulder-button prompt (L1 / R1): white rounded box, navy lettering; 4x then scaled."""
    k = 4
    img = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, w * k - 1, h * k - 1), radius=24 * k, fill=WHITE)
    font = sized(futura, 34 * k)
    b = font.getbbox("H")
    tw = d.textlength(text, font=font)
    d.text(((w * k - tw) / 2, (h * k - (b[1] + b[3])) / 2), text, font=font, fill=NAVY)
    return img.resize((w, h), Image.LANCZOS)


def star(d, cx, cy, r, fill):
    """Five-pointed star, point up."""
    import math
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.4
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def flag(kind, w=60, h=40):
    """Small region flag for the game list: drawn 4x, rounded corners, a thin
    dark edge so the white parts read on any tile colour."""
    k = 4
    W, H = w * k, h * k
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if kind == "us":
        red, white, blue = (178, 34, 52, 255), (255, 255, 255, 255), (60, 59, 110, 255)
        for i in range(13):
            d.rectangle((0, i * H / 13, W, (i + 1) * H / 13), fill=red if i % 2 == 0 else white)
        cw, ch = W * 0.42, H * 7 / 13
        d.rectangle((0, 0, cw, ch), fill=blue)
        for row in range(5):
            for col in range(6 if row % 2 == 0 else 5):
                x = cw * (col + (1.0 if row % 2 == 0 else 1.5)) / 7
                y = ch * (row + 1) / 6
                star(d, x, y, H * 0.035, white)
    elif kind == "eu":
        import math
        d.rectangle((0, 0, W, H), fill=(0, 51, 153, 255))
        for i in range(12):
            a = i * math.pi / 6
            star(d, W / 2 + H / 3 * math.sin(a), H / 2 - H / 3 * math.cos(a), H * 0.075, (255, 204, 0, 255))
    elif kind == "jp":
        d.rectangle((0, 0, W, H), fill=(255, 255, 255, 255))
        r = H * 3 / 10
        d.ellipse((W / 2 - r, H / 2 - r, W / 2 + r, H / 2 + r), fill=(188, 0, 45, 255))
    # rounded corners and edge
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=5 * k, fill=255)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    ImageDraw.Draw(out).rounded_rectangle((0, 0, W - 1, H - 1), radius=5 * k, outline=(20, 20, 36, 255), width=2 * k)
    return out.resize((w, h), Image.LANCZOS)


# Menu text drawn in Futura Bold Italic (white; the app tints it). Each image
# is TEXT_CAP * 2 tall with the capitals centred vertically, so labels line up
# whatever their letters. Keep in sync with the labels in src/game.c.
TEXT_CAP = 44
MENU_TEXTS = [
    "Game", "Display", "Paused",
    "Resume", "Restart Game", "Tap Again to Restart", "Save State", "Load State", "Close Game", "Close GBA DS",
    "Swap Screens", "Scale: Pixel Perfect", "Scale: Full Width", "Scale: Stretch",
    "LCD Grid: On", "LCD Grid: Off", "Show FPS: On", "Show FPS: Off",
    "Overlays: None", "Overlays: Primary", "Overlays: Secondary", "Overlays: Both",
    "Map Animations: On", "Map Animations: Off",
    # game list
    "Play", "Quit", "Touch or use the D-Pad to select a game", "Loading thumbnail...",
]


def menu_text(futura, text):
    font = sized(futura, TEXT_CAP)
    cap = font.getbbox("H")
    full = font.getbbox(text)
    pad = TEXT_CAP // 4
    w = full[2] - min(full[0], 0) + 2 * pad
    h = TEXT_CAP * 2
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((pad - min(full[0], 0), h / 2 - (cap[1] + cap[3]) / 2), text, font=font, fill=WHITE)
    return img


def scale_h(img, h):
    return img.resize((round(img.width * h / img.height), h), Image.LANCZOS)


def png_bytes(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    futura, gamecuben = sys.argv[1], sys.argv[2]
    full = logo(futura, gamecuben)
    out = os.path.join(ROOT, "assets", "brand")
    os.makedirs(out, exist_ok=True)
    full.save(os.path.join(out, "gba-ds-logo.png"))
    full.resize((1024, round(full.height * 1024 / full.width)), Image.LANCZOS).save(
        os.path.join(out, "gba-ds-logo-1024.png"))

    # in-app images, sized for ~2x their on-screen size on a 1024x768 screen
    images = {
        "logo": scale_h(full, 192),
        "btn_a": button(futura, "A"),
        "btn_b": button(futura, "B"),
        "no_saves": label(futura, "no save state yet!"),
        "btn_l1": pill(futura, "L1"),
        "btn_r1": pill(futura, "R1"),
        "flag_us": flag("us"),
        "flag_eu": flag("eu"),
        "flag_jp": flag("jp"),
    }
    lines = ["/* Generated by tools/make_brand.py - do not edit. */", '#include "brand.h"', ""]
    for name, img in images.items():
        data = png_bytes(img)
        lines.append(f"static const unsigned char {name}_png[{len(data)}] = {{")
        for i in range(0, len(data), 20):
            lines.append("\t" + ", ".join(str(b) for b in data[i:i + 20]) + ",")
        lines.append("};")
    lines.append("")
    lines.append("const BrandImage brandImages[BRAND_COUNT] = {")
    for name in images:
        lines.append(f"\t[BRAND_{name.upper()}] = {{ {name}_png, sizeof({name}_png) }},")
    lines.append("};")
    lines.append("")
    for i, t in enumerate(MENU_TEXTS):
        data = png_bytes(menu_text(futura, t))
        lines.append(f"static const unsigned char text{i}_png[{len(data)}] = {{")
        for j in range(0, len(data), 20):
            lines.append("\t" + ", ".join(str(b) for b in data[j:j + 20]) + ",")
        lines.append("};")
    lines.append("")
    lines.append(f"const int brandTextCap = {TEXT_CAP};")
    lines.append(f"const int brandTextCount = {len(MENU_TEXTS)};")
    lines.append("const BrandText brandTexts[] = {")
    for i, t in enumerate(MENU_TEXTS):
        lines.append(f'\t{{ "{t}", text{i}_png, sizeof(text{i}_png) }},')
    lines.append("};")
    with open(os.path.join(ROOT, "src", "brand_data.c"), "w") as f:
        f.write("\n".join(lines) + "\n")
    for name, img in images.items():
        print(f"{name}: {img.width}x{img.height}")


if __name__ == "__main__":
    main()
