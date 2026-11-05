#!/usr/bin/env python3
"""Inspect the shipped knife/hand binaries independently of their exporter."""
import math
from pathlib import Path
import struct

BASE = Path(__file__).resolve().parents[2] / 'assets/baseq3'
FOLDER = BASE / 'models/weapons2/throwingknife'
shader = (BASE / 'scripts/peace_throwingknife.shader').read_text()
total = 0


def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))


def clips(name, count):
    result = {}
    for line in (FOLDER / name).read_text().splitlines():
        if not line.strip() or line.startswith('//'): continue
        slot, first, frames, fps, loop = line.split()
        first, frames = int(first), int(frames)
        assert 0 <= first < first+frames <= count and float(fps) > 0 and loop == '0'
        result[slot] = (first, first+frames-1)
    return result


for filename, tag_count in (('knife.md3', 0), ('knife_hand.md3', 1)):
    data = (FOLDER / filename).read_bytes()
    h = struct.unpack_from('<4si64s9i', data)
    count = h[4]
    assert h[0:2] == (b'IDP3', 15) and 0 < count <= 1024 and h[5] == tag_count
    assert h[8] == 108 and h[9] == 108+56*count and h[10] == h[9]+112*tag_count*count and h[11] == len(data)
    bounds = [struct.unpack_from('<10f', data, h[8]+56*f) for f in range(count)]
    tags = []
    if tag_count:
        for f in range(count):
            tag = struct.unpack_from('<64s12f', data, h[9]+112*f)
            assert tag[0].split(b'\0')[0] == b'tag_weapon'
            origin, axis = tag[1:4], [tag[4+i*3:7+i*3] for i in range(3)]
            for i in range(3):
                for j in range(3): assert abs(dot(axis[i], axis[j])-int(i == j)) < .00001
            assert dot(cross(axis[0], axis[1]), axis[2]) > .999
            tags.append((origin, axis))
        throw, melee = clips('throw.cfg', count), clips('melee.cfg', count)
        assert tags[throw['raise'][1]] == tags[throw['idle'][0]], 'Pullout must settle exactly into idle'
        assert tags[melee['raise'][1]] == tags[melee['fire'][0]], 'Slash must flow through the hit'
        assert tags[0][0][:2] == (0, 0) and 0 < tags[0][0][2] < 4 and tags[0][1][0][2] > .8
    else:
        assert count == 1, 'The world/pickup knife remains static'
    offset = h[10]
    all_points = []
    for _ in range(h[6]):
        s = struct.unpack_from('<4s64s10i', data, offset)
        _, name, flags, frames, materials, nverts, ntris, tri_off, shader_off, uv_off, xyz_off, size = s
        assert s[0] == b'IDP3' and frames == count and materials == 1
        assert 0 < nverts < 1000 and 0 < ntris < 2000, 'GL1 surface overflow'
        assert tri_off == 108 and shader_off == tri_off+12*ntris
        assert uv_off == shader_off+68 and xyz_off == uv_off+8*nverts and size == xyz_off+8*nverts*count
        material = data[offset+shader_off:offset+shader_off+64].split(b'\0')[0].decode()
        assert material+'\n{' in shader, f'Missing shipping shader: {material}'
        positions = []
        for f in range(count):
            vertices, normals = [], []
            for i in range(nverts):
                x, y, z, packed = struct.unpack_from('<3hH', data, offset+xyz_off+8*(i+f*nverts))
                p = (x/64, y/64, z/64)
                assert all(bounds[f][k] <= p[k] <= bounds[f][k+3] for k in range(3)), 'Culling bounds omit geometry'
                assert math.sqrt(dot(p, p)) <= bounds[f][9]
                polar, azimuth = (packed & 255)*math.tau/255, (packed >> 8)*math.tau/255
                normals.append((math.cos(azimuth)*math.sin(polar), math.sin(azimuth)*math.sin(polar), math.cos(polar)))
                vertices.append(p)
                if f and any(part in material for part in ('_glove', '_pad')):
                    # The glove must stay wrapped around the moving grip tag.
                    local = [dot(sub(positions[0][i], tags[0][0]), a) for a in tags[0][1]]
                    expected = [tags[f][0][k]+sum(local[j]*tags[f][1][j][k] for j in range(3)) for k in range(3)]
                    assert max(abs(p[k]-expected[k]) for k in range(3)) < .03, 'Hand slips off its grip'
            for i in range(ntris):
                ids = struct.unpack_from('<3i', data, offset+tri_off+12*i)
                assert all(0 <= j < nverts for j in ids)
                a, b, c = [vertices[j] for j in ids]
                face_normal = cross(sub(b, a), sub(c, a))
                assert dot(face_normal, face_normal) > 0, f'Degenerate triangle: {filename} {name!r} frame {f}'
                assert all(dot(face_normal, normals[j]) < 0 for j in ids), 'Inside-out face or incorrect lighting normal'
            positions.append(vertices)
        if tag_count:
            assert positions[throw['raise'][1]] == positions[throw['idle'][0]]
            assert positions[melee['raise'][1]] == positions[melee['fire'][0]]
        all_points.extend(positions[0])
        total += ntris
        offset += size
    assert offset == len(data)
    if not tag_count:
        tip = [p for p in all_points if p[0] == max(v[0] for v in all_points)]
        assert max(p[0] for p in all_points) == 6 and min(p[0] for p in all_points) < -7
        assert all(abs(p[1]) < .05 and abs(p[2]) < .05 for p in tip), 'World blade must point along +X'

icon = (BASE / 'icons/peace_throwingknife.tga').read_bytes()
assert struct.unpack_from('<HHBB', icon, 12) == (128, 128, 32, 8)
assert len(icon) == 18+128*128*4
alpha = icon[21::4]
assert 500 < sum(a > 0 for a in alpha) < 5000 and 0 in alpha and 255 in alpha
print(f'PASS: {total} triangles; all {count} hand frames, grip contact, clip seams, winding, bounds, GL1 limits, materials and icon')
