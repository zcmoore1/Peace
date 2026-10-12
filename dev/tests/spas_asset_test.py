#!/usr/bin/env python3
"""Check the shipped IQM, not a second exporter implementation.

Quake's default material culls GL_FRONT. Regressing to Blender's CCW order
renders the interior and makes the front sight post disappear in ADS.
"""
from pathlib import Path
import math
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

# Decode the shipped channels independently of the animation baker. This tests
# the actual engine input, including quantization and clip offsets/loop flags.
poses = [struct.unpack_from("<iI20f", data, h[16] + 88*i) for i in range(h[15])]
assert h[15] == h[13]
assert sum(p[1].bit_count() for p in poses) == h[20]
frames = []
offset = h[21]
for _ in range(h[19]):
    frame = []
    for p in poses:
        channels = list(p[2:12])
        for k in range(10):
            if p[1] & (1 << k):
                channels[k] += struct.unpack_from("<H", data, offset)[0]*p[12+k]
                offset += 2
        assert all(math.isfinite(c) for c in channels)
        frame.append(channels)
    frames.append(frame)
assert offset == h[21] + 2*h[19]*h[20]


def clip_frames(label, loop):
    first, count, _, flags = clips[label]
    assert flags == loop and count > 1 and first+count <= len(frames)
    return frames[first:first+count]


def difference(a, b):
    return max(abs(x-y) for aa, bb in zip(a, b) for x, y in zip(aa, bb))


idle = frames[clips["idle"][0]]
entry = clip_frames("sprint_in", 0)
carry = clip_frames("sprint_loop", 1)
leave = clip_frames("sprint_out", 0)
for a, b in [(idle, entry[0]), (entry[-1], carry[0]),
             (carry[0], leave[0]), (leave[-1], idle)]:
    assert difference(a, b) < .0001, "Sprint endpoint pop"
assert difference(entry[0], entry[-1]) > 1, "Sprint entry is still idle"
assert difference(carry[0], carry[len(carry)//4]) > .05, "Carry must move, not freeze"
steps = [difference(a, b) for a, b in zip(carry, carry[1:])]
assert difference(carry[-1], carry[0]) <= max(steps)*1.05, "Carry loop seam pops"

roots = [i for i, p in enumerate(poses) if p[0] < 0]
for frame in entry+carry+leave:
    # Child locals (fingers, pump, shell, attachments) must keep their grip.
    for i, p in enumerate(poses):
        if p[0] >= 0:
            assert max(abs(x-y) for x, y in zip(idle[i], frame[i])) < .0001
    # All independent hand/gun roots undergo the same rigid motion. Their
    # mutual distances must remain unchanged after export/quantization.
    for a in roots:
        for b in roots:
            before = math.dist(idle[a][:3], idle[b][:3])
            after = math.dist(frame[a][:3], frame[b][:3])
            assert abs(before-after) < .005, "Sprint separates gun and hands"

print(f"PASS: {tested} SPAS triangles; clip metadata, sprint entry/loop/exit and grips.")
