#!/usr/bin/env python3
"""Makes src/logo_masks.c: the shapes of title logos that are painted into a
full-screen title picture rather than drawn on a layer of their own.

    GBADS_LOGO_DEBUG=1 build/host/harness THPS2.gba out logo.txt   # 'logo logo'
    (writes out/logo.png.fg.png: the exact title frame GBA DS renders)
    tools/make_logo_masks.py THPS2=THPS2-frame.png [SK8=SK8-frame.png] [THPS3=...]

Masks not named on the command line are kept as they are in
src/logo_masks.c, so one can be redone without the other games' ROMs.

Only the outline (which pixels are logo) goes into the source; the pixels
themselves are taken from the ROM when the logo is extracted. Needs Pillow,
numpy and scipy.
"""
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H, W = 160, 240


def fit_ellipse(rim, columns):
    """Axis-aligned ellipse through the top-most and bottom-most rim pixel of
    each column (least squares)."""
    pts = []
    for xr in columns:
        for x in xr:
            col = np.nonzero(rim[:, x])[0]
            if len(col):
                pts.append((x, col.min()))
                pts.append((x, col.max()))
    p = np.array(pts, float)
    a, b, c, d = np.linalg.lstsq(np.c_[p[:, 0] ** 2, p[:, 1] ** 2, p[:, 0], p[:, 1]], np.ones(len(p)), rcond=None)[0]
    cx, cy = -c / (2 * a), -d / (2 * b)
    k = 1 + c * c / (4 * a) + d * d / (4 * b)
    return cx, cy, np.sqrt(k / a), np.sqrt(k / b)


def largest(m):
    lab, n = ndimage.label(m)
    if not n:
        return m
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return lab == (1 + int(np.argmax(sizes)))


def thps2(a):
    """Tony Hawk's Pro Skater 2 (USA/Europe, France, Germany): the oval badge
    with its white rim, plus the red 2 and its emblem sticking out right."""
    mx, mn = a.max(axis=2), a.min(axis=2)
    ys, xs = np.mgrid[0:H, 0:W]
    rim = (mn > 170) & (mx - mn < 60) & (ys > 35) & (ys < 125)
    cx, cy, ax, by = fit_ellipse(rim, [range(28, 165)])
    m = ((xs - cx) / (ax + 0.8)) ** 2 + ((ys - cy) / (by + 0.8)) ** 2 <= 1
    red = (a[..., 0] > 170) & (a[..., 1] < 100) & (a[..., 2] < 120) & (xs > cx + 40) & (ys > cy - by - 12) & (ys < cy + by + 6)
    red = largest(ndimage.binary_dilation(red, iterations=1))
    yy, xx = np.nonzero(red)
    em = (mn > 140) & (xs >= xx.min() - 3) & (xs <= xx.max()) & (ys >= yy.min() - 3) & (ys <= yy.min() + 16)
    red = ndimage.binary_dilation(ndimage.binary_closing(red | em, iterations=1), iterations=1)
    return largest(ndimage.binary_fill_holes(m | red))


def sk8(a):
    """SK8 (Japan): the oval badge, the lettering that breaks out of it and the
    yellow starburst behind."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(axis=2), a.min(axis=2)
    ys, xs = np.mgrid[0:H, 0:W]
    rim = (mn > 150) & (mx - mn < 50) & (ys > 45) & (ys < 115) & (xs > 15) & (xs < 225)
    cx, cy, ax, by = fit_ellipse(rim, [range(30, 90), range(170, 212)])
    ell = ((xs - cx) / (ax + 0.8)) ** 2 + ((ys - cy) / (by + 0.8)) ** 2 <= 1
    near = (ys >= cy - by - 10) & (ys <= cy + by + 2)
    logo = ((r > 190) & (g > 150) & (b < 100)) | ((r > 150) & (g < 70) & (b < 80)) | ((mx < 45) & near) | ((mn > 190) & near)
    lab, _ = ndimage.label(logo | ell, structure=np.ones((3, 3)))
    keep = np.unique(lab[ell])
    m = np.isin(lab, keep[keep > 0])
    m = ndimage.binary_opening(m, iterations=1) | ell
    return largest(ndimage.binary_fill_holes(m))


def thps3(a):
    """Tony Hawk's Pro Skater 3 (USA/Europe, France, Germany: the same
    picture): everything on the band between the black bars that isn't the
    saturated blue behind the badge, holes filled."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    ys, xs = np.mgrid[0:H, 0:W]
    band = (ys >= 24) & (ys < 136)
    blue = (b - r > 45) & (b > g)
    m = largest(band & ~blue)
    return ndimage.binary_fill_holes(ndimage.binary_closing(m, iterations=1))


MASKS = (("THPS2", thps2), ("SK8", sk8), ("THPS3", thps3))


def runs(mask):
    ys, xs = np.nonzero(mask)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    bits = mask[y0:y1 + 1, x0:x1 + 1].flatten()
    out, cur, n = [], False, 0
    for v in bits:
        if bool(v) != cur:
            while n > 65535:
                out += [65535, 0]
                n -= 65535
            out.append(n)
            cur, n = bool(v), 0
        n += 1
    out.append(n)
    return int(x0), int(y0), int(x1 - x0 + 1), int(y1 - y0 + 1), out


def existing_blocks(path):
    """The masks already in src/logo_masks.c, by name: their source lines."""
    blocks = {}
    if not os.path.exists(path):
        return blocks
    text = open(path).read()
    for name, _ in MASKS:
        start = text.find(f"static const uint16_t runs{name}[")
        end = text.find(f"const LogoMask logoMask{name} =")
        if start >= 0 and end >= 0:
            end = text.index("\n", end) + 1
            blocks[name] = text[start:end].rstrip("\n").split("\n")
    return blocks


def main():
    given = dict(arg.split("=", 1) for arg in sys.argv[1:] if "=" in arg)
    if not given or len(given) != len(sys.argv) - 1 or any(n not in dict(MASKS) for n in given):
        sys.exit(__doc__)
    out = os.path.join(ROOT, "src", "logo_masks.c")
    old = existing_blocks(out)
    lines = ["/* Generated by tools/make_logo_masks.py - do not edit. */", '#include "logo_masks.h"', ""]
    for name, fn in MASKS:
        if name in given:
            a = np.asarray(Image.open(given[name]).convert("RGB")).astype(int)
            x, y, w, h, r = runs(fn(a))
            block = [f"static const uint16_t runs{name}[{len(r)}] = {{"]
            for i in range(0, len(r), 16):
                block.append("\t" + ", ".join(str(v) for v in r[i:i + 16]) + ",")
            block.append("};")
            block.append(f"const LogoMask logoMask{name} = {{ {x}, {y}, {w}, {h}, runs{name}, {len(r)} }};")
            print(f"{name}: {w}x{h} at {x},{y}, {len(r)} runs")
        elif name in old:
            block = old[name]
            print(f"{name}: kept")
        else:
            sys.exit(f"{name}: no frame given and none in {out}")
        lines += block + [""]
    with open(out, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
