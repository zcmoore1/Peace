#!/usr/bin/env python3
"""Original stylized game prop and gloved hand; deterministic MD3 geometry.

No real explosive components or third-party artwork. Regenerate with Python 3.
"""
import argparse
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2] / "assets/baseq3"


def box(lo, hi):
    x,y,z = lo
    X,Y,Z = hi
    p = [(x,y,z),(X,y,z),(X,Y,z),(x,Y,z),(x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)]
    return [[p[i] for i in f] for f in
            [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]]


def body():
    rings = [(-3,1.2),(-2.6,2),(-1.5,2.5),(1.5,2.5),(2.6,2),(3,1.2)]
    faces = []
    for (z,r),(Z,R) in zip(rings,rings[1:]):
        for i in range(16):
            a,b = i*math.tau/16,(i+1)*math.tau/16
            faces.append([(r*math.cos(a),r*math.sin(a),z),
                          (r*math.cos(b),r*math.sin(b),z),
                          (R*math.cos(b),R*math.sin(b),Z),
                          (R*math.cos(a),R*math.sin(a),Z)])
    faces += box((-1.2,-1.2,-3),(1.2,1.2,3))
    return faces


def model(parts):
    surfaces = []
    for name, faces in parts:
        vertices, triangles = [], []
        for face in faces:
            u = [face[1][i]-face[0][i] for i in range(3)]
            v = [face[2][i]-face[0][i] for i in range(3)]
            normal = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            length = math.sqrt(sum(c*c for c in normal))
            normal = [c/length for c in normal]
            first = len(vertices)
            vertices.extend((p,normal) for p in face)
            triangles += [(first,first+2,first+1),(first,first+3,first+2)]
        st = 108 + len(triangles)*12 + 68
        xyz = st + len(vertices)*8
        size = xyz + len(vertices)*8
        blob = bytearray(struct.pack('<4s64s10i', b'IDP3', name.encode(),
            0,1,1,len(vertices),len(triangles),108,108+len(triangles)*12,st,xyz,size))
        blob += b''.join(struct.pack('<3i',*t) for t in triangles)
        blob += struct.pack('<64si', ('peace/semtex_'+name).encode(),0)
        blob += b''.join(struct.pack('<2f',i%4 in (1,2),i%4 >= 2) for i in range(len(vertices)))
        for point, n in vertices:
            polar = round(math.acos(max(-1,min(1,n[2])))*255/math.tau) & 255
            azimuth = round(math.atan2(n[1],n[0])*255/math.tau) & 255
            blob += struct.pack('<3hH',*(round(p*64) for p in point),(azimuth<<8)|polar)
        assert len(blob) == size
        surfaces.append(blob)
    size = 164 + sum(map(len,surfaces))
    return (struct.pack('<4si64s9i',b'IDP3',15,b'Peace Semtex original game prop',
            0,1,0,len(surfaces),0,108,164,164,size) +
            struct.pack('<10f16s',-17,-5,-8,5,5,6,0,0,0,22,b'idle') + b''.join(surfaces))


def build():
    prop = [('body',body()),
            ('band',box((-2.6,-2.6,-.45),(2.6,2.6,.45))),
            ('trim',box((-.8,-.8,3),(.8,.8,4.3)) + box((-.9,-.8,4.1),(2.9,.8,4.6))),
            ('light',box((2.56,-.6,-.3),(2.7,.6,.3)))]
    hand = [('sleeve',box((-17,-2.8,-7),(-6,2.8,-3))),
            ('glove',box((-6,-2.1,-5),(-1,2.1,-1.4)) +
             box((-2.5,-3,-2.2),(1,-1.8,.6)) +
             sum((box((-.6,-1.8+i*.85,-3),(2.5,-1.15+i*.85,-1.5)) for i in range(4)),[]))]
    shader = '// Generated original flat materials; no external textures required.\n'
    for name,color in [('body','.28 .32 .17'),('band','.12 .13 .10'),('trim','.46 .49 .45'),
                       ('light','.95 .12 .035'),('glove','.13 .14 .12'),('sleeve','.24 .27 .22')]:
        lighting = '' if name == 'light' else ' {\n  map $whiteimage\n  rgbGen lightingDiffuse\n }\n'
        blend = '' if name == 'light' else '  blendFunc GL_DST_COLOR GL_ZERO\n'
        shader += f'peace/semtex_{name}\n{{\n{lighting} {{\n  map $whiteimage\n{blend}  rgbGen const ( {color} )\n }}\n}}\n'
    shader += 'icons/peace_semtex\n{\n nopicmip\n {\n map $whiteimage\n rgbGen const ( .55 .65 .25 )\n }\n}\n'
    return {'models/weapons2/semtex/semtex.md3':model(prop),
            'models/weapons2/semtex/semtex_hand.md3':model(hand),
            'scripts/peace_semtex.shader':shader.encode()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    for name,data in build().items():
        path = ROOT/name
        if args.check:
            assert path.read_bytes() == data, f'Stale asset: {name}'
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
        print(f'{"Checked" if args.check else "Built"} {name} ({len(data)} bytes)')
