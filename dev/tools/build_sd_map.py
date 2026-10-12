#!/usr/bin/env python3
"""Build peace_sd: a small, original Search & Destroy arena. No q3map2, no retail data.

One long sealed hall. Attackers spawn at the -X end, defenders at the +X end.
Bomb sites A and B sit on the defenders' half, each with a pad on the floor and
a tall colour-coded pillar you can see from the far end: A is orange, B is cyan.
The middle has cover with lanes around it, and the defenders' side has a stair
up to a raised perch, because this is a trickshot game.

Sites are POINT trigger_bombsite entities - an origin and a radius - because the
writer only emits worldspawn and cannot make brush entities. g_round.c accepts
either form.

The source geometry here is authoritative. --check verifies committed outputs.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))   # q3bsp.py lives beside this
from q3bsp import Map  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "assets/baseq3"
NAME = "peace_sd"

# Interior: X -1536..1536, Y -768..768, Z 0..512. Walls 16 thick.
HX, HY, HZ, T = 1536, 768, 512, 16
SITE_RADIUS = 128
SITES = {"A": (700, 448), "B": (700, -448)}
ATTACK_X, DEFEND_X = -1344, 1344
SPAWN_YS = (-320, -160, 0, 160, 320)
PLAYER_Z = 24          # a standing player's origin sits 24 above the floor
STAIR_X, STAIR_Y = 896, 680     # foot of the stair; it climbs toward +X
PERCH_Z = 128

FLOOR, WALL, CRATE, COVER, SITE_A, SITE_B, ATTACK, DEFEND, CEILING = range(9)
MATERIALS = [
    ("floor",   (0.22, 0.24, 0.26), 0),
    ("wall",    (0.14, 0.17, 0.21), 0),
    ("crate",   (0.42, 0.34, 0.22), 0),
    ("cover",   (0.30, 0.33, 0.36), 0),
    ("site_a",  (0.97, 0.55, 0.12), 0),
    ("site_b",  (0.15, 0.75, 0.85), 0),
    ("attack",  (0.55, 0.17, 0.17), 0),
    ("defend",  (0.17, 0.28, 0.58), 0),
    ("ceiling", (0.10, 0.11, 0.13), 0),
]


def build():
    m = Map(f"textures/{NAME}", MATERIALS)

    # --- shell ---------------------------------------------------------------
    m.box((-HX - T, -HY - T, -32), (HX + T, HY + T, 0), FLOOR)
    m.box((-HX - T, -HY - T, HZ), (HX + T, HY + T, HZ + T), CEILING)
    m.box((-HX - T, -HY - T, 0), (-HX, HY + T, HZ), WALL)
    m.box((HX, -HY - T, 0), (HX + T, HY + T, HZ), WALL)
    m.box((-HX, -HY - T, 0), (HX, -HY, HZ), WALL)
    m.box((-HX, HY, 0), (HX, HY + T, HZ), WALL)

    # --- spawn ends: a coloured strip on the floor so you know whose end it is
    m.box((-HX, -HY, 0), (-HX + 384, HY, 1), ATTACK, collision=False)
    m.box((HX - 384, -HY, 0), (HX, HY, 1), DEFEND, collision=False)

    # --- the middle: a wall to break the sightline, lanes either side ---------
    m.box((-32, -256, 0), (32, 256, 160), COVER)
    for x, y in ((-448, 384), (-448, -384), (320, 192), (320, -192)):
        m.box((x - 48, y - 48, 0), (x + 48, y + 48, 48), CRATE)      # mountable
    for x, y in ((-704, 0), (448, 576), (448, -576)):
        m.box((x - 40, y - 40, 0), (x + 40, y + 40, 96), CRATE)      # cover

    # --- bomb sites: pad, a crate to plant behind, and a pillar landmark ------
    for label, (sx, sy) in SITES.items():
        mat = SITE_A if label == "A" else SITE_B
        pad = 88                                    # half-width; the corner stays inside the reach
        m.box((sx - pad, sy - pad, 0), (sx + pad, sy + pad, 1), mat, collision=False)
        towards_wall = 1 if sy > 0 else -1
        px, py = sx + 152, sy + towards_wall * 152
        m.box((px - 16, py - 16, 0), (px + 16, py + 16, 320), mat)
        m.box((sx - 140, sy - 40, 0), (sx - 108, sy + 40, 64), CRATE)

    # --- defenders' perch: a stair of 16-unit risers up to a 128-high ledge ---
    # 16 is under the 18-unit step height, so it walks like stairs. Tucked in
    # the back corner so it overlooks A and stays clear of the spawn line.
    for i in range(8):
        x0 = STAIR_X + i * 32
        m.box((x0, STAIR_Y - 64, 0), (x0 + 32, STAIR_Y + 64, 16 * (i + 1)), COVER)
    m.box((STAIR_X + 256, STAIR_Y - 112, 0), (STAIR_X + 512, HY, PERCH_Z), COVER)

    # --- entities ------------------------------------------------------------
    m.entity(classname="worldspawn", message="Peace - Search & Destroy",
             gridsize=(64, 64, 128))
    for y in SPAWN_YS:
        for cls in ("team_CTF_redplayer", "team_CTF_redspawn"):
            m.entity(classname=cls, origin=(ATTACK_X, y, PLAYER_Z), angle=0)
        for cls in ("team_CTF_blueplayer", "team_CTF_bluespawn"):
            m.entity(classname=cls, origin=(DEFEND_X, y, PLAYER_Z), angle=180)
    # Deathmatch spawns so the map also loads for FFA / TDM.
    for x, y, a in ((-1100, 560, 0), (-1100, -560, 0), (1100, 360, 180), (1100, -560, 180)):
        m.entity(classname="info_player_deathmatch", origin=(x, y, PLAYER_Z), angle=a)
    for label, (sx, sy) in SITES.items():
        m.entity(classname="trigger_bombsite", site=label,
                 origin=(sx, sy, PLAYER_Z), radius=SITE_RADIUS)
    m.entity(classname="info_player_intermission", origin=(-1200, 0, 360),
             angles=(25, 0, 0))
    # Positions for dev/tests/sd_map_test.c, so it reads the stair from the BSP
    # rather than hardcoding it. info_null frees itself at spawn: no cost in game.
    m.entity(classname="info_null", targetname="test_stair_foot",
             origin=(STAIR_X - 40, STAIR_Y, PLAYER_Z), angle=0)
    m.entity(classname="info_null", targetname="test_perch",
             origin=(STAIR_X + 384, STAIR_Y, PERCH_Z + PLAYER_Z))

    bounds = (-HX - T, -HY - T, -32, HX + T, HY + T, HZ + T)
    arena = (f'{{\nmap "{NAME}"\nlongname "Peace S&D Arena"\n'
             f'type "sd team ffa"\n}}\n')
    return {BASE / f"maps/{NAME}.bsp": m.bsp(bounds),
            BASE / f"scripts/{NAME}.shader": m.shader("dev/tools/build_sd_map.py").encode(),
            BASE / f"scripts/{NAME}.arena": arena.encode()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for path, data in build().items():
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"Stale or missing generated asset: {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f"{'Checked' if args.check else 'Built'} {path.relative_to(ROOT)} ({len(data)} bytes)")
