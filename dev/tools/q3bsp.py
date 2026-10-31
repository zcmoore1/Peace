"""Write tiny, original, vertex-lit Quake 3 BSPs without q3map2 or retail data.

Lifted out of build_ladder_map.py so more than one map can use it. The format
is unchanged: only axis-aligned boxes, one worldspawn model, two leaves that
both see everything, flat-coloured $whiteimage shaders. Good for test arenas,
not for real levels - a real level wants Radiant and q3map2.

Proof that this port is faithful: `python3 dev/tools/build_ladder_map.py --check`
still passes, and that script can be rebuilt on this module byte for byte (see
the self-test at the bottom: `python3 dev/tools/q3bsp.py`).

Q3 structures and flags: code/qcommon/{qfiles.h,surfaceflags.h}.
"""
import math
import struct

SOLID, LADDER = 1, 8
CONTENTS_SOLID = 1


class Map:
    def __init__(self, shader_dir, materials):
        """materials: [(name, (r, g, b), surface_flags), ...] - index = material id."""
        self.shader_dir = shader_dir
        self.materials = materials
        self.planes, self.brushes, self.sides = [], [], []
        self.surfaces, self.vertices, self.indexes = [], [], []
        self.plane_lookup = {}
        self.entities = []

    # --- geometry -----------------------------------------------------------
    def plane(self, normal, distance):
        key = (*normal, distance)
        if key not in self.plane_lookup:
            inverse = (*(-v for v in normal), -distance)
            self.plane_lookup[key] = len(self.planes)
            self.plane_lookup[inverse] = len(self.planes) + 1
            self.planes.extend((key, inverse))
        return self.plane_lookup[key]

    def face(self, points, normal, material):
        first_vertex, first_index = len(self.vertices), len(self.indexes)
        # Engine face culling expects clockwise winding viewed from outside.
        for i, xyz in enumerate(points):
            self.vertices.append((*xyz, float(i in (1, 2)), float(i >= 2),
                                  0., 0., *normal, 255, 255, 255, 255))
        self.indexes.extend((0, 2, 1, 0, 3, 2))
        self.surfaces.append((material, -1, 1, first_vertex, 4, first_index, 6,
                              -3, 0, 0, 0, 0, *points[0],
                              0., 0., 0., 0., 0., 0., *normal, 0, 0))

    def box(self, lo, hi, material, collision=True, front=None):
        """An axis-aligned box. collision=False draws it without making it solid."""
        x, y, z = lo
        X, Y, Z = hi
        faces = [
            ((-1, 0, 0), -x, [(x, y, z), (x, y, Z), (x, Y, Z), (x, Y, z)]),
            ((1, 0, 0), X, [(X, y, z), (X, Y, z), (X, Y, Z), (X, y, Z)]),
            ((0, -1, 0), -y, [(x, y, z), (X, y, z), (X, y, Z), (x, y, Z)]),
            ((0, 1, 0), Y, [(x, Y, z), (x, Y, Z), (X, Y, Z), (X, Y, z)]),
            ((0, 0, -1), -z, [(x, y, z), (x, Y, z), (X, Y, z), (X, y, z)]),
            ((0, 0, 1), Z, [(x, y, Z), (X, y, Z), (X, Y, Z), (x, Y, Z)]),
        ]
        first_side = len(self.sides)
        for i, (normal, distance, points) in enumerate(faces):
            shader = front if i == 0 and front is not None else material
            if collision:
                self.sides.append((self.plane(normal, distance), shader))
            self.face(points, normal, shader)
        if collision:
            self.brushes.append((first_side, 6, material))

    # --- entities -----------------------------------------------------------
    def entity(self, **keys):
        """Keys in the order given; numbers and tuples become Q3 strings."""
        def fmt(v):
            if isinstance(v, (tuple, list)):
                return " ".join(fmt(c) for c in v)
            if isinstance(v, float) and v.is_integer():
                return str(int(v))
            return str(v)
        self.entities.append([(k, fmt(v)) for k, v in keys.items()])

    def entity_string(self):
        out = ""
        for ent in self.entities:
            out += "{\n" + "".join(f'"{k}" "{v}"\n' for k, v in ent) + "}\n"
        return out

    # --- output -------------------------------------------------------------
    def bsp(self, bounds, gridsize=(64, 64, 128), entities=None):
        """bounds: (minx, miny, minz, maxx, maxy, maxz) of the whole map.
        entities: a raw entity string; defaults to the ones added with entity()."""
        lumps = [bytearray() for _ in range(17)]
        text = entities if entities is not None else self.entity_string()
        lumps[0] = text.encode() + b"\0"
        for name, _, flags in self.materials:
            lumps[1] += struct.pack("<64sii", f"{self.shader_dir}/{name}".encode(), flags, SOLID)
        split = self.plane((1, 0, 0), 0)
        for p in self.planes:
            lumps[2] += struct.pack("<4f", *p)
        lumps[3] += struct.pack("<9i", split, -1, -2, *bounds)
        # Conservative visibility: both leaves see the entire map.
        for _ in range(2):
            lumps[4] += struct.pack("<12i", 0, 0, *bounds, 0, len(self.surfaces), 0, len(self.brushes))
        lumps[5] += struct.pack(f"<{len(self.surfaces)}i", *range(len(self.surfaces)))
        lumps[6] += struct.pack(f"<{len(self.brushes)}i", *range(len(self.brushes)))
        lumps[7] += struct.pack("<6f4i", *bounds, 0, len(self.surfaces), 0, len(self.brushes))
        for b in self.brushes:
            lumps[8] += struct.pack("<3i", *b)
        for s in self.sides:
            lumps[9] += struct.pack("<2i", *s)
        for v in self.vertices:
            lumps[10] += struct.pack("<10f4B", *v)
        lumps[11] += struct.pack(f"<{len(self.indexes)}i", *self.indexes)
        for s in self.surfaces:
            lumps[13] += struct.pack("<12i12f2i", *s)
        grid_count = math.prod(math.floor(bounds[i + 3] / g) - math.ceil(bounds[i] / g) + 1
                               for i, g in enumerate(gridsize))
        lumps[15] += bytes((140, 140, 140, 90, 90, 90, 0, 0)) * grid_count
        lumps[16] += struct.pack("<iiB", 1, 1, 1)
        offset = 8 + 17 * 8
        directory, body = bytearray(), bytearray()
        for lump in lumps:
            directory += struct.pack("<ii", offset, len(lump))
            padded = lump + b"\0" * (-len(lump) % 4)
            body += padded
            offset += len(padded)
        return b"IBSP" + struct.pack("<i", 46) + directory + body

    def shader(self, generator):
        out = f"// Generated by {generator}.\n"
        for name, color, flags in self.materials:
            out += f"{self.shader_dir}/{name}\n{{\n"
            if flags & LADDER:
                out += "    surfaceparm ladder\n"
            out += ("    {\n        map $whiteimage\n        rgbGen const ( " +
                    " ".join(map(str, color)) + " )\n    }\n}\n")
        return out


def _ladder_lab():
    """build_ladder_map.py's geometry, call for call, rebuilt on this module."""
    m = Map("textures/peace_ladder", [
        ("floor", (0.25, 0.29, 0.34), 0),
        ("wall", (0.12, 0.19, 0.26), 0),
        ("platform", (0.33, 0.38, 0.42), 0),
        ("ladder", (0.13, 0.14, 0.16), LADDER),
        ("rail", (0.97, 0.61, 0.12), 0),
        ("rung", (0.76, 0.84, 0.88), 0),
    ])
    m.box((-400, -272, -32), (400, 272, 0), 0)
    m.box((-400, -272, 512), (400, 272, 528), 1)
    m.box((-400, -272, 0), (-384, 272, 512), 1)
    m.box((384, -272, 0), (400, 272, 512), 1)
    m.box((-384, -272, 0), (384, -256, 512), 1)
    m.box((-384, 256, 0), (384, 272, 512), 1)
    m.box((144, -224, 0), (384, 224, 320), 2)
    m.box((128, -56, 0), (144, 56, 320), 2, front=3)
    for y in (-52, 48):
        m.box((122, y, 0), (128, y + 4, 320), 4, collision=False)
    for z in range(16, 320, 24):
        m.box((121, -48, z), (128, 48, z + 4), 5, collision=False)
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
    return m.bsp((-400, -272, -32, 400, 272, 528), entities=entities), m


if __name__ == "__main__":
    from pathlib import Path
    root = Path(__file__).resolve().parents[2]
    committed = root / "assets/baseq3/maps/peace_ladder.bsp"
    rebuilt, m = _ladder_lab()
    shader_ok = (root / "assets/baseq3/scripts/peace_ladder.shader").read_text() == \
        m.shader("dev/tools/build_ladder_map.py")
    if committed.read_bytes() != rebuilt or not shader_ok:
        raise SystemExit("q3bsp: ladder lab rebuild DIFFERS from the committed map - the port is not faithful")
    print(f"q3bsp: ladder lab rebuilt byte for byte ({len(rebuilt)} bytes) - writer is faithful")
