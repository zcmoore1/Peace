#!/usr/bin/env python3
"""Build a tiny, original, vertex-lit Q3 BSP without retail data or q3map2.

Only axis-aligned brushes/flat faces: one sealed room, one platform, one ladder.
The source geometry here is authoritative. --check verifies committed outputs.
Q3 structures/flags are defined in code/qcommon/{qfiles.h,surfaceflags.h}.
"""
import argparse
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "assets/baseq3"
SOLID, LADDER = 1, 8
MATERIALS = [
    ("floor", (0.25, 0.29, 0.34), 0),
    ("wall", (0.12, 0.19, 0.26), 0),
    ("platform", (0.33, 0.38, 0.42), 0),
    ("ladder", (0.13, 0.14, 0.16), LADDER),
    ("rail", (0.97, 0.61, 0.12), 0),
    ("rung", (0.76, 0.84, 0.88), 0),
]


def build():
    lumps = [bytearray() for _ in range(17)]
    planes, brushes, sides, surfaces, vertices, indexes = [], [], [], [], [], []
    plane_lookup = {}

    def plane(normal, distance):
        key = (*normal, distance)
        if key not in plane_lookup:
            inverse = (*(-v for v in normal), -distance)
            plane_lookup[key] = len(planes)
            plane_lookup[inverse] = len(planes) + 1
            planes.extend((key, inverse))
        return plane_lookup[key]

    def face(points, normal, material):
        first_vertex, first_index = len(vertices), len(indexes)
        # Engine face culling expects clockwise winding viewed from outside.
        for i, xyz in enumerate(points):
            vertices.append((*xyz, float(i in (1, 2)), float(i >= 2),
                             0., 0., *normal, 255, 255, 255, 255))
        indexes.extend((0, 2, 1, 0, 3, 2))
        surfaces.append((material, -1, 1, first_vertex, 4, first_index, 6,
                         -3, 0, 0, 0, 0, *points[0],
                         0., 0., 0., 0., 0., 0., *normal, 0, 0))

    def box(lo, hi, material, collision=True, front=None):
        x, y, z = lo
        X, Y, Z = hi
        faces = [
            ((-1, 0, 0), -x, [(x,y,z), (x,y,Z), (x,Y,Z), (x,Y,z)]),
            ((1, 0, 0), X, [(X,y,z), (X,Y,z), (X,Y,Z), (X,y,Z)]),
            ((0, -1, 0), -y, [(x,y,z), (X,y,z), (X,y,Z), (x,y,Z)]),
            ((0, 1, 0), Y, [(x,Y,z), (x,Y,Z), (X,Y,Z), (X,Y,z)]),
            ((0, 0, -1), -z, [(x,y,z), (x,Y,z), (X,Y,z), (X,y,z)]),
            ((0, 0, 1), Z, [(x,y,Z), (X,y,Z), (X,Y,Z), (x,Y,Z)]),
        ]
        first_side = len(sides)
        for i, (normal, distance, points) in enumerate(faces):
            shader = front if i == 0 and front is not None else material
            if collision:
                sides.append((plane(normal, distance), shader))
            face(points, normal, shader)
        if collision:
            brushes.append((first_side, 6, material))

    # Room interior: -384..384 X, -256..256 Y, 0..512 Z.
    box((-400,-272,-32), (400,272,0), 0)
    box((-400,-272,512), (400,272,528), 1)
    box((-400,-272,0), (-384,272,512), 1)
    box((384,-272,0), (400,272,512), 1)
    box((-384,-272,0), (384,-256,512), 1)
    box((-384,256,0), (384,272,512), 1)
    # Ladder backing protrudes from a platform; only its front is climbable.
    # Decorative rails/rungs are non-solid: they cannot snag a sideways slide.
    box((144,-224,0), (384,224,320), 2)
    box((128,-56,0), (144,56,320), 2, front=3)
    for y in (-52, 48):
        box((122,y,0), (128,y+4,320), 4, collision=False)
    for z in range(16, 320, 24):
        box((121,-48,z), (128,48,z+4), 5, collision=False)

    entities = '''{
"classname" "worldspawn"
"message" "Peace - ladder lab"
"gridsize" "64 64 128"
}
{
"classname" "info_player_deathmatch"
"origin" "-180 0 32"
"angle" "0"
}
{
"classname" "info_player_intermission"
"origin" "-250 -180 150"
"angles" "0 25 0"
}
'''
    lumps[0] = entities.encode() + b"\0"
    for name, _, flags in MATERIALS:
        lumps[1] += struct.pack("<64sii", f"textures/peace_ladder/{name}".encode(), flags, SOLID)
    split = plane((1, 0, 0), 0)
    for p in planes:
        lumps[2] += struct.pack("<4f", *p)
    bounds = (-400,-272,-32,400,272,528)
    lumps[3] += struct.pack("<9i", split, -1, -2, *bounds)
    # Conservative visibility: both leaves see the entire tiny room.
    for _ in range(2):
        lumps[4] += struct.pack("<12i", 0, 0, *bounds, 0, len(surfaces), 0, len(brushes))
    lumps[5] += struct.pack(f"<{len(surfaces)}i", *range(len(surfaces)))
    lumps[6] += struct.pack(f"<{len(brushes)}i", *range(len(brushes)))
    lumps[7] += struct.pack("<6f4i", *bounds, 0, len(surfaces), 0, len(brushes))
    for b in brushes:
        lumps[8] += struct.pack("<3i", *b)
    for s in sides:
        lumps[9] += struct.pack("<2i", *s)
    for v in vertices:
        lumps[10] += struct.pack("<10f4B", *v)
    lumps[11] += struct.pack(f"<{len(indexes)}i", *indexes)
    for s in surfaces:
        lumps[13] += struct.pack("<12i12f2i", *s)
    grid_count = math.prod(math.floor(bounds[i+3] / g) - math.ceil(bounds[i] / g) + 1
                           for i, g in enumerate((64, 64, 128)))
    lumps[15] += bytes((140, 140, 140, 90, 90, 90, 0, 0)) * grid_count
    lumps[16] += struct.pack("<iiB", 1, 1, 1)
    offset = 8 + 17 * 8
    directory, body = bytearray(), bytearray()
    for lump in lumps:
        directory += struct.pack("<ii", offset, len(lump))
        padded = lump + b"\0" * (-len(lump) % 4)
        body += padded
        offset += len(padded)
    bsp = b"IBSP" + struct.pack("<i", 46) + directory + body
    shader = "// Generated by dev/tools/build_ladder_map.py.\n"
    for name, color, flags in MATERIALS:
        shader += f"textures/peace_ladder/{name}\n{{\n"
        if flags & LADDER:
            shader += "    surfaceparm ladder\n"
        shader += ("    {\n        map $whiteimage\n        rgbGen const ( " +
                   " ".join(map(str, color)) + " )\n    }\n}\n")
    arena = '{\nmap "peace_ladder"\nlongname "Peace Ladder Lab"\ntype "ffa tourney"\n}\n'
    return {BASE / "maps/peace_ladder.bsp": bsp,
            BASE / "scripts/peace_ladder.shader": shader.encode(),
            BASE / "scripts/peace_ladder.arena": arena.encode()}


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
