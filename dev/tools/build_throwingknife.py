#!/usr/bin/env python3
"""Build original Peace throwing-knife art with Python's standard library.

The meshes, grip pose, materials and silhouette are authored here, not imported
from another game. Coordinates are stylized engine-space art, not fabrication
dimensions. GPL-2.0-or-later, like the repository. --check writes nothing.
"""
import argparse
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'assets/baseq3'
PREFIX = 'models/weapons2/throwingknife/'
COLORS = {
    'coat': (.23, .26, .28), 'edge': (.67, .70, .72),
    'tang': (.36, .39, .40), 'grip': (.10, .13, .12),
    'rib': (.17, .20, .18), 'pin': (.51, .49, .43),
    'glove': (.26, .28, .24), 'pad': (.12, .14, .12),
    'sleeve': (.22, .26, .22), 'seam': (.34, .36, .29),
}


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a): return mul(a, 1/math.sqrt(dot(a, a)))


# The world model points along +X, as the server's apos does. The hand's
# tag_weapon alone turns it upright for the first-person grip. It never changes
# the missile's origin, angles, hit box or placement.
ANGLE = math.radians(58)
GRIP_ORIGIN = (0, 0, 2.2)
GRIP_AXIS = ((math.cos(ANGLE), 0, math.sin(ANGLE)), (0, 1, 0),
             (-math.sin(ANGLE), 0, math.cos(ANGLE)))


def held(p):
    return tuple(GRIP_ORIGIN[i]+sum(p[j]*GRIP_AXIS[j][i] for j in range(3)) for i in range(3))


class Mesh:
    def __init__(self, transform=None):
        self.parts = {}
        self.transform = transform or (lambda p: p)

    def face(self, material, points):
        # Outward CCW in source. MD3's exporter reverses triangles for idTech3.
        points = [self.transform(p) for p in points]
        normal = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        vertices, triangles = self.parts.setdefault(material, ([], []))
        start = len(vertices)
        vertices.extend((p, normal) for p in points)
        triangles.extend((start, start+i, start+i+1) for i in range(1, len(points)-1))

    def loft(self, material, rings, sides=12, caps=True):
        # x, y, z, radius-y, radius-z. Elliptical sections sculpt a solid shape.
        rows = [[(x, y+ry*math.cos(i*math.tau/sides), z+rz*math.sin(i*math.tau/sides))
                 for i in range(sides)] for x, y, z, ry, rz in rings]
        for a, b in zip(rows, rows[1:]):
            for i in range(sides):
                k = (i+1) % sides
                self.face(material, [a[i], a[k], b[k], b[i]])
        if caps:
            self.face(material, list(reversed(rows[0])))
            self.face(material, rows[-1])

    def rod(self, material, a, b, ra, rb=None, sides=10):
        axis = unit(sub(b, a))
        u = unit(cross(axis, (0, 0, 1) if abs(axis[2]) < .9 else (0, 1, 0)))
        v = cross(axis, u)
        rb = ra if rb is None else rb
        rows = [[add(p, add(mul(u, r*math.cos(i*math.tau/sides)), mul(v, r*math.sin(i*math.tau/sides))))
                 for i in range(sides)] for p, r in ((a, ra), (b, rb))]
        for i in range(sides):
            k = (i+1) % sides
            self.face(material, [rows[0][i], rows[0][k], rows[1][k], rows[1][i]])
        self.face(material, list(reversed(rows[0])))
        self.face(material, rows[1])


def knife():
    mesh = Mesh()
    # Broad coated flats and a narrow bright bevel on each
    # face of the spear-point silhouette. Six vertices per cross-section.
    rings = [(-.45, .65, .18), (.2, .91, .21), (3.1, .83, .19),
             (4.6, .46, .12), (6.0, .025, .025)]
    rows = [[(x, -w, 0), (x, -.67*w, -z), (x, .67*w, -z),
             (x, w, 0), (x, .67*w, z), (x, -.67*w, z)] for x, w, z in rings]
    for a, b in zip(rows, rows[1:]):
        for i in range(6):
            k = (i+1) % 6
            mesh.face('coat' if i in (1, 4) else 'edge', [a[i], a[k], b[k], b[i]])
    mesh.face('tang', list(reversed(rows[0])))
    mesh.face('edge', rows[-1])
    # Full tang, low-profile guard, rounded grip scales and shallow grip ribs.
    mesh.loft('tang', [(-6.5, 0, 0, .63, .20), (-.35, 0, 0, .68, .20)], sides=8)
    mesh.loft('tang', [(-.65, 0, 0, .73, .23), (-.50, 0, 0, 1.05, .31),
                       (-.20, 0, 0, 1.05, .31), (-.08, 0, 0, .72, .23)], sides=8)
    mesh.loft('grip', [(-6.4, 0, 0, .46, .31), (-5.9, 0, 0, .60, .40),
                      (-4.8, 0, 0, .63, .44), (-2.3, 0, 0, .62, .44),
                      (-1.0, 0, 0, .57, .38), (-.72, 0, 0, .48, .30)], sides=12)
    for x in (-5.7, -5.15, -4.6, -4.05, -3.5, -2.95, -2.4, -1.85, -1.3):
        for side in (-1, 1):
            mesh.rod('rib', (x-.10, -.46, side*.31), (x+.10, .46, side*.31), .055, sides=6)
    for x in (-5.6, -1.55):
        for side in (-1, 1):
            mesh.rod('pin', (x, 0, side*.40), (x, 0, side*.46), .17, sides=12)
            mesh.rod('grip', (x-.09, 0, side*.465), (x+.09, 0, side*.465), .025, sides=6)
    # An actual open lanyard eye, not a painted hole.
    for i in range(16):
        a, b = i*math.tau/16, (i+1)*math.tau/16
        p = lambda r, angle, z: (-6.65+r*math.cos(angle), r*math.sin(angle), z)
        for side in (-1, 1):
            face = [p(.54, a, side*.17), p(.54, b, side*.17), p(.26, b, side*.17), p(.26, a, side*.17)]
            mesh.face('tang', face if side > 0 else list(reversed(face)))
        mesh.face('tang', [p(.54, a, -.17), p(.54, b, -.17), p(.54, b, .17), p(.54, a, .17)])
        mesh.face('tang', [p(.26, b, -.17), p(.26, a, -.17), p(.26, a, .17), p(.26, b, .17)])
    return mesh


def hand():
    mesh = Mesh(held)
    # The palm and four separately bent fingers wrap the handle. The thumb
    # crosses the fingers; knuckle pads and seams remain original geometry.
    mesh.loft('glove', [(-6.5, .12, -.78, .72, .60), (-5.8, .12, -.72, 1.02, .69),
                       (-2.1, .12, -.70, 1.10, .65), (-1.65, .12, -.65, .68, .50)])
    for i, x in enumerate((-5.55, -4.55, -3.55, -2.55)):
        radius = .36 if i else .32
        points = [(x, .95, -.55), (x+.06, 1.03, .34), (x+.12, .55, .79), (x+.18, -.40, .67)]
        for j, (a, b) in enumerate(zip(points, points[1:])):
            mesh.rod('glove', a, b, radius*(1-.08*j), radius*(.95-.08*j))
        mesh.rod('pad', (x-.16, 1.0, -.35), (x+.15, 1.05, .19), .38, .34)
        mesh.rod('seam', (x, .74, .78), (x+.1, .31, .91), .035, sides=6)
    for a, b, r, R in [((-1.85, -.95, -.75), (-2.1, -1.05, .20), .49, .43),
                        ((-2.1, -1.05, .20), (-3.28, -.56, .82), .43, .32)]:
        mesh.rod('glove', a, b, r, R)
    mesh.loft('pad', [(-6.9, .15, -.85, .80, .65), (-6.25, .15, -.85, .86, .70)])
    # Continue the sleeve below the screen edge without cutting across the eye.
    wrist = held((-6.6, .15, -.85))
    mesh.transform = lambda p: p
    mesh.rod('sleeve', (-14, 2.8, -21), add(wrist, (-.35, 0, -.3)), 2.05, 1.08, sides=16)
    mesh.rod('seam', (-12.1, 2.9, -18.1), add(wrist, (-.65, .67, -.35)), .075, .055, sides=6)
    return mesh


def surfaces(mesh):
    # GL1 has strict per-surface limits. Split on triangle boundaries.
    for material, (vertices, triangles) in mesh.parts.items():
        remap, out, faces = {}, [], []
        for tri in triangles:
            if len(out)+sum(i not in remap for i in tri) > 900:
                yield material, out, faces
                remap, out, faces = {}, [], []
            for i in tri:
                if i not in remap:
                    remap[i] = len(out)
                    out.append(vertices[i])
            faces.append(tuple(remap[i] for i in tri))
        if faces:
            yield material, out, faces


def md3(mesh, name, grip=False):
    blobs, points = [], []
    for index, (material, vertices, triangles) in enumerate(surfaces(mesh)):
        points.extend(p for p, _ in vertices)
        st = 108+12*len(triangles)+68
        xyz = st+8*len(vertices)
        size = xyz+8*len(vertices)
        blob = bytearray(struct.pack('<4s64s10i', b'IDP3', f'{material}_{index}'.encode(),
            0, 1, 1, len(vertices), len(triangles), 108, st-68, st, xyz, size))
        blob += b''.join(struct.pack('<3i', a, c, b) for a, b, c in triangles)
        blob += struct.pack('<64si', ('peace/throwingknife_'+material).encode(), 0)
        blob += b''.join(struct.pack('<2f', 0, 0) for _ in vertices)
        for p, n in vertices:
            polar = round(math.acos(max(-1, min(1, n[2])))*255/math.tau) & 255
            azimuth = round(math.atan2(n[1], n[0])*255/math.tau) & 255
            blob += struct.pack('<3hH', *(round(x*64) for x in p), (azimuth << 8) | polar)
        assert len(blob) == size
        blobs.append(blob)
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    radius = max(math.sqrt(dot(p, p)) for p in points)+.03
    tag = struct.pack('<64s12f', b'tag_weapon', *GRIP_ORIGIN, *(x for axis in GRIP_AXIS for x in axis)) if grip else b''
    offset = 164+len(tag)
    return (struct.pack('<4si64s9i', b'IDP3', 15, name.encode(), 0, 1, int(grip), len(blobs),
            0, 108, 164, offset, offset+sum(map(len, blobs))) +
            struct.pack('<10f16s', *(x-.03 for x in lo), *(x+.03 for x in hi), 0, 0, 0, radius, b'idle') +
            tag+b''.join(blobs))


def icon(mesh):
    # Orthographic silhouette from the actual mesh, not a grenade stand-in.
    # Supersampling keeps the small HUD icon readable without external artwork.
    size, scale = 128, 7.2
    mask = bytearray(size*2*size*2)
    def project(p):
        x, y = p[0]+.6, p[1]
        return ((64+(x-y)*scale*.7071)*2, (64-(x+y)*scale*.7071)*2)
    for vertices, triangles in mesh.parts.values():
        for tri in triangles:
            a, b, c = [project(vertices[i][0]) for i in tri]
            def edge(p, q, r): return (q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0])
            area = edge(a, b, c)
            if abs(area) < .001: continue
            for y in range(max(0, math.floor(min(a[1], b[1], c[1]))), min(size*2, math.ceil(max(a[1], b[1], c[1])))):
                for x in range(max(0, math.floor(min(a[0], b[0], c[0]))), min(size*2, math.ceil(max(a[0], b[0], c[0])))):
                    p = (x+.5, y+.5)
                    if all(v*area >= 0 for v in (edge(a, b, p), edge(b, c, p), edge(c, a, p))):
                        mask[y*size*2+x] = 255
    pixels = bytearray()
    for y in reversed(range(size)):
        for x in range(size):
            alpha = sum(mask[(y*2+dy)*size*2+x*2+dx] for dy in range(2) for dx in range(2))//4
            pixels += bytes((244, 244, 244, alpha))
    return struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, size, size, 32, 8)+pixels


def build():
    mesh = knife()
    shader = '// Original Peace materials. Generated by dev/tools/build_throwingknife.py.\n'
    for name, color in COLORS.items():
        shader += f'peace/throwingknife_{name}\n{{\n {{ map $whiteimage\n  rgbGen lightingDiffuse\n }}\n'
        shader += ' { map $whiteimage\n  blendFunc GL_DST_COLOR GL_ZERO\n  rgbGen const ( '+ ' '.join(map(str, color))+' )\n }\n'
        if name in ('edge', 'tang', 'pin'):
            shader += ' { map $whiteimage\n  blendFunc GL_SRC_ALPHA GL_ONE\n  rgbGen const ( .18 .18 .18 )\n  alphaGen lightingSpecular\n }\n'
        shader += '}\n'
    shader += 'icons/peace_throwingknife\n{\n nopicmip\n { map icons/peace_throwingknife.tga\n  blendFunc blend\n  rgbGen vertex\n  alphaGen vertex\n }\n}\n'
    return {PREFIX+'knife.md3': md3(mesh, 'Peace original throwing knife'),
            PREFIX+'knife_hand.md3': md3(hand(), 'Peace original knife grip', True),
            'scripts/peace_throwingknife.shader': shader.encode(),
            'icons/peace_throwingknife.tga': icon(mesh)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, data in build().items():
        path = BASE/name
        if args.check:
            assert path.read_bytes() == data, f'Stale asset: {name}'
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f'{"Checked" if args.check else "Built"} {name} ({len(data)} bytes)')
