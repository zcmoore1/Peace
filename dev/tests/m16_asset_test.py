#!/usr/bin/env python3
"""Inspect the shipped M16/Masterkey files independently of their exporter."""
from pathlib import Path
import math
import struct

ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / 'assets/baseq3/models/weapons2/m16'
def validate_model(variant):
    data = (MODEL / (variant+'.iqm')).read_bytes()
    h = struct.unpack_from('<27I', data, 16)
    assert data[:16] == b'INTERQUAKEMODEL\0' and h[0] == 2 and h[1] == len(data)
    text = data[h[4]:h[4]+h[3]]


    def label(offset):
        return text[offset:].split(b'\0', 1)[0].decode()


    arrays = {}
    for i in range(h[7]):
        kind, flags, fmt, size, offset = struct.unpack_from('<5I', data, h[9]+20*i)
        assert fmt in (1, 7)
        word, width = ('f', 4) if fmt == 7 else ('B', 1)
        arrays[kind] = list(struct.iter_unpack('<'+str(size)+word,
                                              data[offset:offset+h[8]*size*width]))
    for i in range(h[5]):
        mesh = struct.unpack_from('<6I', data, h[6]+24*i)
        assert label(mesh[1]).startswith('peace/m16_')
        assert mesh[3] < 1000 and mesh[5] < 2000, 'Surface exceeds GL1 limits'
        for tri in range(mesh[4], mesh[4]+mesh[5]):
            ids = struct.unpack_from('<3I', data, h[11]+12*tri)
            assert all(mesh[2] <= j < mesh[2]+mesh[3] for j in ids)
            a, b, c = (arrays[0][j] for j in ids)
            u, v = [b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)]
            cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
            normal = [sum(arrays[2][j][k] for j in ids) for k in range(3)]
            assert sum(cross[k]*normal[k] for k in range(3)) < 0, 'Inside-out or degenerate face'
    for indices, weights in zip(arrays[4], arrays[5]):
        assert all(i < h[13] for i in indices) and sum(weights) == 255
    assert all(math.isfinite(value) for vertex in arrays[0] for value in vertex)

    nodes = [struct.unpack_from('<Ii10f', data, h[14]+48*i) for i in range(h[13])]
    names = [label(node[0]) for node in nodes]
    assert 'tag_flash' in names and any('pump' in name.lower() for name in names)
    assert any('left' in name.lower() for name in names), 'Support hand needs its own joint'
    for i, node in enumerate(nodes):
        assert -1 <= node[1] < i

    clips = {}
    for line in (MODEL / (variant+'.cfg')).read_text().splitlines():
        fields = line.split('//', 1)[0].split()
        if fields:
            name, first, count, fps, loop = fields
            clips[name] = (int(first), int(count), float(fps), int(loop))
    assert {'idle', 'raise', 'drop', 'fire', 'reload_loop', 'sprint_in', 'sprint_loop', 'sprint_out',
            'alt_raise'} <= clips.keys()
    for i in range(h[17]):
        name, first, count, fps, loop = struct.unpack_from('<3IfI', data, h[18]+20*i)
        expected = clips[label(name)]
        assert count >= 2 and first+count <= h[19] and fps > 0
        assert (first, count, loop) == (expected[0], expected[1], expected[3])
        assert abs(fps-expected[2]) < .0001

    poses = [struct.unpack_from('<iI20f', data, h[16]+88*i) for i in range(h[15])]
    assert h[13] == h[15] and sum(p[1].bit_count() for p in poses) == h[20]
    frames = []
    offset = h[21]
    for _ in range(h[19]):
        frame = []
        for p in poses:
            channels = list(p[2:12])
            for k in range(10):
                if p[1] & (1 << k):
                    channels[k] += struct.unpack_from('<H', data, offset)[0]*p[12+k]
                    offset += 2
            assert all(math.isfinite(x) for x in channels)
            assert abs(sum(x*x for x in channels[3:7])-1) < .0002
            assert min(channels[7:]) > 0
            frame.append(channels)
        frames.append(frame)
    assert offset == h[21]+2*h[19]*h[20]


    def clip(name):
        first, count, _, _ = clips[name]
        return frames[first:first+count]


    def difference(a, b):
        return max(abs(x-y) for c, d in zip(a, b) for x, y in zip(c, d))


    idle = clip('idle')[0]
    entry, carry, leave = clip('sprint_in'), clip('sprint_loop'), clip('sprint_out')
    for a, b in [(idle, entry[0]), (entry[-1], carry[0]), (carry[0], leave[0]),
                 (leave[-1], idle), (clip('raise')[-1], idle), (clip('drop')[0], idle)]:
        assert difference(a, b) < .001, 'Authored transition endpoint pops'
    assert difference(entry[0], entry[-1]) > 1
    assert difference(carry[0], carry[len(carry)//4]) > .02
    assert difference(carry[-1], carry[0]) <= max(difference(a, b) for a, b in zip(carry, carry[1:]))*1.1

    if variant == 'masterkey':
        assert {'reload_start', 'reload_end'} <= clips.keys()
        pump = names.index('pump'); hand = names.index('left_hand')
        fire = clip('fire')
        assert max(abs(frame[pump][0]-idle[pump][0]) for frame in fire) > 3.5
        assert difference(fire[-1], idle) < .001, 'Pump must settle to ready'
        assert all(abs((frame[pump][0]-idle[pump][0]) -
                       (frame[hand][0]-idle[hand][0])) < .001 for frame in fire)
        for a,b in [(clip('reload_start')[0],idle),
                    (clip('reload_start')[-1],clip('reload_loop')[0]),
                    (clip('reload_loop')[-1],clip('reload_end')[0]),
                    (clip('reload_end')[-1],idle)]:
            assert difference(a,b) < .001, 'Shell reload boundary pops'
        assert nodes[names.index('tag_flash')][4] < 0, 'Wrong barrel muzzle'
    else:
        assert nodes[names.index('tag_flash')][4] > 0
    # The mode switch's raise ends at rest, and the trigger hand travels in it.
    switch = clip('alt_raise'); trigger = names.index('right_hand')
    assert difference(switch[-1], idle) < .001, 'Mode switch must settle to ready'
    assert max(abs(a-b) for a, b in zip(switch[0][trigger][:3], idle[trigger][:3])) > 5
    print(f'PASS: {variant}: {h[10]} triangles, {h[19]} poses, rig, winding and transition continuity')
    return names, idle, switch


rifle = validate_model('view')
shotgun = validate_model('masterkey')
# The mode switch flows: each mode's switch clip starts on the other mode's rest
# pose, joint for joint (the two variants name their muzzle tags differently).
for (names, idle, switch), (other_names, other_idle, _) in [(rifle, shotgun), (shotgun, rifle)]:
    for index, name in enumerate(names):
        if name.startswith('tag_'): continue
        there = other_idle[other_names.index(name)]
        assert max(abs(a-b) for a, b in zip(switch[0][index], there)) < .001, 'Mode switch pops at its start: '+name
print('PASS: each mode switch starts on the other mode\'s rest pose')

world = (MODEL / 'world.md3').read_bytes()
assert world[:4] == b'IDP3' and struct.unpack_from('<i', world, 104)[0] == len(world)
assert (MODEL / 'materials.tga').stat().st_size > 18
assert (ROOT / 'assets/baseq3/scripts/peace_m16.shader').is_file()
