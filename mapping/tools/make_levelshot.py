#!/usr/bin/env python3
"""make_levelshot.py - generate a map levelshot (menu thumbnail) as a TGA.

Quake 3 shows levelshots/<mapname>.tga in the map selection menu. This draws
a watermelon-slice thumbnail procedurally and writes an uncompressed 24-bit
TGA - no image libraries required.

    python3 make_levelshot.py [output_dir]
"""

import math
import os
import struct
import sys

W, H = 512, 384

# colours as (r, g, b)
BG = (24, 26, 34)
RIND_DARK = (26, 115, 42)
RIND_LIGHT = (176, 222, 150)
FLESH = (226, 58, 72)
SEED = (32, 22, 22)

CX, CY = W // 2, 96          # centre of the slice's flat (top) edge
R = 250                      # slice radius
RIND_OUTER = 16              # dark rind thickness
RIND_INNER = 14             # light rind thickness

# seed centres, relative to (CX, CY), all below the flat edge
SEEDS = [(-150, 70), (-86, 50), (-20, 64), (52, 50), (120, 72),
         (-118, 140), (-44, 126), (32, 132), (104, 140), (-6, 190)]
SEED_A, SEED_B = 5, 8        # seed half-axes (x, y)


def _seed_pixel(dx, dy):
    """True if (dx, dy) relative to slice centre lands inside a seed."""
    for sx, sy in SEEDS:
        ex = (dx - sx) / SEED_A
        ey = (dy - sy) / SEED_B
        if ex * ex + ey * ey <= 1.0:
            return True
    return False


def watermelon(w, h):
    """Return a top-to-bottom, row-major list of (r, g, b) pixels."""
    px = []
    for y in range(h):
        for x in range(w):
            dx = x - CX
            dy = y - CY
            if dy < 0:
                px.append(BG)
                continue
            d = math.hypot(dx, dy)
            if d > R:
                px.append(BG)
            elif d >= R - RIND_OUTER:
                px.append(RIND_DARK)
            elif d >= R - RIND_OUTER - RIND_INNER:
                px.append(RIND_LIGHT)
            elif _seed_pixel(dx, dy):
                px.append(SEED)
            else:
                px.append(FLESH)
        px.append  # noqa - keep loop body explicit
    return px


def write_tga(path, w, h, px):
    """Write an uncompressed 24-bit TGA (bottom-left origin, BGR)."""
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,      # id length
        0,      # no colour map
        2,      # uncompressed true-colour
        0, 0, 0,  # colour map spec
        0, 0,   # x/y origin
        w, h,   # dimensions
        24,     # bits per pixel
        0,      # descriptor: bottom-left origin
    )
    body = bytearray()
    # bottom-left origin => write rows bottom-to-top, channels as BGR
    for y in range(h - 1, -1, -1):
        row = y * w
        for x in range(w):
            r, g, b = px[row + x]
            body += bytes((b, g, r))
    with open(path, "wb") as f:
        f.write(header)
        f.write(body)


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "levelshots")
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    px = watermelon(W, H)
    path = os.path.join(out_dir, "box2guns.tga")
    write_tga(path, W, H, px)
    print("wrote %s (%dx%d)" % (path, W, H))


if __name__ == "__main__":
    main(sys.argv)
