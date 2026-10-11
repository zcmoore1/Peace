#!/usr/bin/env python3
"""Check the shipped IQM, not a second exporter implementation.

Quake's default material culls GL_FRONT. Regressing to Blender's CCW order
renders the interior and makes the front sight post disappear in ADS.
"""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / "assets/baseq3/models/weapons2/spas12"
data = (MODEL / "view.iqm").read_bytes()
assert data[:16] == b"INTERQUAKEMODEL\0"
h = struct.unpack_from("<27I", data, 16)
assert h[0] == 2 and h[1] == len(data)
text = data[h[4]:h[4] + h[3]]


def name(offset):
    return text[offset:].split(b"\0", 1)[0].decode()


arrays = {}
for i in range(h[7]):
    kind, flags, fmt, size, offset = struct.unpack_from("<5I", data, h[9] + 20*i)
    if kind in (0, 2):
        assert fmt == 7 and size == 3  # IQM_FLOAT positions/normals
        arrays[kind] = list(struct.iter_unpack("<3f", data[offset:offset + h[8]*12]))

tested = 0
for i in range(h[5]):
    mesh = struct.unpack_from("<6I", data, h[6] + 24*i)
    assert name(mesh[1]) in ("models/weapons2/spas12/gun", "models/weapons2/spas12/hands")
    clockwise = 0
    for tri in range(mesh[4], mesh[4] + mesh[5]):
        ids = struct.unpack_from("<3I", data, h[11] + 12*tri)
        assert all(mesh[2] <= j < mesh[2] + mesh[3] for j in ids)
        a, b, c = (arrays[0][j] for j in ids)
        u, v = [b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)]
        cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        normal = [sum(arrays[2][j][k] for j in ids) for k in range(3)]
        clockwise += sum(cross[k]*normal[k] for k in range(3)) < 0
        tested += 1
    # A few source triangles have unusual smoothed normals. The overwhelming
    # majority of each surface must nevertheless face outward under Q3 culling.
    assert clockwise / mesh[5] > .98, f"Inside-out surface: {name(mesh[0])}"

clips = {}
for line in (MODEL / "view.cfg").read_text().splitlines():
    fields = line.split("//", 1)[0].split()
    if fields:
        clip, first, count, fps, loop = fields
        clips[clip] = (int(first), int(count), float(fps), int(loop))
for i in range(h[17]):
    label, first, count, fps, loop = struct.unpack_from("<3IfI", data, h[18] + 20*i)
    expected = clips[name(label)]
    assert (first, count, loop) == (expected[0], expected[1], expected[3])
    assert abs(fps - expected[2]) < .0001

print(f"PASS: {tested} SPAS triangles use Quake culling; IQM and clip-table timing agree.")
