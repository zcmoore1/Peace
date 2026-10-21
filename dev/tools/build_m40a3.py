#!/usr/bin/env python3
"""Build Peace's original M40A3-inspired game art, rig and timing data.

Stdlib only. JSON is the authoring source; --check never writes. Materials.jpg
is an original generated diffuse atlas, not extracted CoD artwork. Dimensions
below are stylized game-space coordinates, not manufacturing specifications.
"""
import argparse
import io
import json
import math
from pathlib import Path
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'assets/baseq3'
PREFIX = 'models/weapons2/m40a3/'
RECIPE = ROOT / 'assets/source/m40a3/weapon.json'
ORIGIN = (13, -5, -6)
NODES = [('Base', -1, ORIGIN), ('bolt', 0, (2, 0, .5)),
         ('right_hand', 0, (1, -.3, -2.5)), ('left_hand', 0, (13, 0, -2)),
         ('round', 2, (0, 0, 0)), ('tag_flash', 0, (34, 0, .5))]
MATERIALS = {'stock': (0, 0), 'metal': (1, 0), 'fabric': (0, 1),
             'rubber': (1, 1), 'steel': (1, 0), 'brass': (1, 0), 'glass': (1, 1), 'flash': (0,0)}


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return scale(a, 1/math.sqrt(dot(a, a)))
def ease(t):
    t = max(0, min(1, t))
    return t*t*(3-2*t)
def window(t, a, b): return ease((t-a)/(b-a))
def quat(angles):
    x, y, z = (math.radians(a)/2 for a in angles)
    cx, cy, cz, sx, sy, sz = math.cos(x), math.cos(y), math.cos(z), math.sin(x), math.sin(y), math.sin(z)
    return (sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz, cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz)
def channels(p, angles=(0, 0, 0), s=1): return (*p, *quat(angles), s, s, s)


class Mesh:
    def __init__(self): self.parts = {}

    def face(self, mat, joint, points, uv=None):
        # The authoring geometry is outward CCW; only the exporter reverses it.
        n = norm(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        vertices, triangles = self.parts.setdefault((mat, joint), ([], []))
        start = len(vertices)
        tile = MATERIALS[mat]
        for i, p in enumerate(points):
            st = uv[i] if uv else ((i in (1, 2)), (i >= 2))
            if mat!='flash': st = tuple(tile[k]*.5+.025+.45*st[k] for k in range(2))
            vertices.append((add(p, ORIGIN), n, st, joint))
        triangles.extend((start, start+i, start+i+1) for i in range(1, len(points)-1))

    def loft(self, mat, rings, joint=0, sides=16, caps=True):
        # x, y, z, horizontal radius, vertical radius; elliptical cross sections.
        rows = [[(x, y+ry*math.cos(math.tau*i/sides), z+rz*math.sin(math.tau*i/sides))
                 for i in range(sides)] for x, y, z, ry, rz in rings]
        for j, (a, b) in enumerate(zip(rows, rows[1:])):
            for i in range(sides):
                k = (i+1) % sides
                self.face(mat, joint, [a[i], a[k], b[k], b[i]],
                          [(j/(len(rows)-1), i/sides), (j/(len(rows)-1), (i+1)/sides),
                           ((j+1)/(len(rows)-1), (i+1)/sides), ((j+1)/(len(rows)-1), i/sides)])
        if caps:
            self.face(mat, joint, list(reversed(rows[0])))
            self.face(mat, joint, rows[-1])

    def rod(self, mat, a, b, ra, rb=None, joint=0, sides=12):
        axis = norm(sub(b, a))
        u = norm(cross(axis, (0, 0, 1) if abs(axis[2]) < .9 else (0, 1, 0)))
        v = cross(axis, u)
        rb = ra if rb is None else rb
        rows = [[add(p, add(scale(u, r*math.cos(math.tau*i/sides)), scale(v, r*math.sin(math.tau*i/sides))))
                 for i in range(sides)] for p, r in [(a, ra), (b, rb)]]
        for i in range(sides):
            k = (i+1) % sides
            self.face(mat, joint, [rows[0][i], rows[0][k], rows[1][k], rows[1][i]])
        self.face(mat, joint, list(reversed(rows[0])))
        self.face(mat, joint, rows[1])


def geometry(hands=True):
    m = Mesh()
    # Sculpted composite stock, tapered fore-end and separate cheek saddle.
    m.loft('stock', [(-18,0,-1.8,.95,2.7),(-17,0,-1.8,1.15,2.8),
        (-11,0,-1.3,1.15,2.5),(-6,0,-.8,.85,1.4),(-3,0,-.8,.7,1.1),
        (0,0,-.9,.95,1.25),(5,0,-1,1.15,1.25),(12,0,-1,1.15,1.15),
        (17,0,-.65,1,.7),(18,0,-.5,.85,.5)])
    m.loft('rubber', [(-18.8,0,-1.8,.85,2.5),(-18.5,0,-1.8,1.08,2.8),(-18,0,-1.8,1.08,2.8)])
    m.loft('stock', [(-15,0,.8,.85,.45),(-14,0,.9,1.2,.7),(-7,0,.9,1.1,.7),(-6,0,.7,.75,.4)])
    m.rod('stock',(-3,0,-.6),(-4.5,0,-4.6),1,.85)
    m.rod('rubber',(-3.5,0,-2.4),(-4.5,0,-4.7),1.03,.88)
    # Receiver and heavy barrel; an open muzzle crown instead of a flat cap.
    m.loft('metal', [(-2,0,.35,.72,.72),(-1,0,.35,.9,.9),(8,0,.35,.9,.9),(10,0,.35,.72,.72)])
    m.loft('metal', [(8,0,.5,.75,.75),(10,0,.5,.68,.68),(32,0,.5,.48,.48),(34,0,.5,.48,.48)], sides=24, caps=False)
    m.loft('steel', [(33.9,0,.5,.48,.48),(34,0,.5,.4,.4),(33.6,0,.5,.25,.25)], sides=24, caps=False)
    m.loft('rubber', [(32,0,.5,.26,.26),(33.6,0,.5,.25,.25)], sides=24)
    # Rail, mounts, mounting screws, trigger guard and sling studs.
    m.loft('metal', [(-2,0,1.3,.6,.22),(8,0,1.3,.6,.22)], sides=8)
    for x in (0, 7):
        m.rod('metal',(x,0,1.2),(x,0,3.2),.55,.55)
        m.loft('rubber', [(x-.3,0,3.5,.88,.88),(x+.3,0,3.5,.88,.88)], caps=False)
        m.rod('steel',(x,-.8,1.6),(x,-1.1,1.6),.23,.23)
    for x in (-14, 15): m.rod('steel',(x,0,-2),(x,0,-2.7),.18,.23)
    for a,b in [((-2,0,-1.7),(-2,0,-3.5)),((-2,0,-3.5),(2,0,-3.5)),((2,0,-3.5),(2,0,-1.5))]:
        m.rod('metal',a,b,.14)
    m.rod('steel',(0,0,-1.3),(-.5,0,-2.8),.15)
    # Full hollow scope body: ocular, adjustment ring, tube, objective bell.
    scope = [(-3,0,3.5,1.22,1.22),(-2.7,0,3.5,1.28,1.28),(-.8,0,3.5,1.12,1.12),
             (0,0,3.5,.8,.8),(7,0,3.5,.8,.8),(10,0,3.5,1.5,1.5),(14,0,3.5,1.6,1.6)]
    m.loft('metal', scope, sides=32, caps=False)
    m.loft('rubber', [(x,y,z,r-.16,h-.16) for x,y,z,r,h in reversed(scope)], sides=32, caps=False)
    for x,r in [(-3,1.22),(-1.5,1.22),(14,1.6)]:
        m.loft('rubber',[(x-.15,0,3.5,r,r),(x+.15,0,3.5,r,r)],sides=32,caps=False)
    m.loft('glass',[(-3.04,0,3.5,1.04,1.04),(-3,0,3.5,1.04,1.04)],sides=32)
    m.loft('glass',[(14,0,3.5,1.42,1.42),(14.04,0,3.5,1.42,1.42)],sides=32)
    for a,b in [((4,0,4),(4,0,5.3)),((4,-.4,3.5),(4,-1.7,3.5))]:
        m.rod('metal',a,b,.65,.65,sides=20)
    for i in range(16):
        angle = math.tau*i/16
        y,z = 1.23*math.cos(angle),3.5+1.23*math.sin(angle)
        m.rod('metal',(-2.3,y,z),(-1.1,y,z),.065,.065,sides=6)
    # Actual moving bolt and right-hand handle, separate from the receiver.
    m.loft('steel',[(-2,0,.5,.45,.45),(4,0,.5,.45,.45)],joint=1,sides=16)
    m.rod('steel',(2,-.3,.5),(2,-1.8,-.1),.17,.2,joint=1)
    m.loft('rubber',[(1.5,-2,-.2,.3,.3),(1.7,-2,-.2,.55,.55),
                     (2.3,-2,-.2,.55,.55),(2.5,-2,-.2,.3,.3)],joint=1)
    if hands:
        for j,p,end in [(2,(1,-.3,-2.5),(-16,-5,-11)),(3,(13,0,-2),(-2,8,-12))]:
            m.rod('fabric',end,add(p,(-2,0,-.5)),1.9,1.3,joint=j,sides=16)
            m.rod('rubber',add(p,(-2.8,0,-.7)),add(p,(-1.3,0,-.3)),1.38,1.25,joint=j,sides=16)
            m.loft('rubber',[(p[0]-1.6,p[1],p[2],1, .7),(p[0],p[1],p[2],1.2,.85),
                            (p[0]+1.5,p[1],p[2],.9,.7)],joint=j)
            for k in range(4):
                x = p[0]-.8+k*.65
                m.rod('rubber',(x,p[1]-1,p[2]),(x+.2,p[1]-1.1,p[2]+1.25),.34,.28,joint=j)
                m.rod('rubber',(x+.2,p[1]-1.1,p[2]+1.25),(x+.3,p[1]-.35,p[2]+1.6),.28,.23,joint=j)
            m.rod('rubber',add(p,(-.7,.85,.1)),add(p,(.9,.85,1.3)),.45,.3,joint=j)
        m.loft('brass',[(.4,-.3,-2,.18,.18),(1.8,-.3,-2,.18,.18),(2.4,-.3,-2,.03,.03)],joint=4,sides=10)
    return m


def pose(clip, t, data):
    result = [channels(p) for _,_,p in NODES]
    root, angle, bolt, hand, cartridge = list(ORIGIN), [0,0,0], 0., 0., .001
    if clip == 'idle': root[2] += .055*math.sin(math.tau*t)
    if clip in ('raise','drop','first_raise'):
        if clip == 'first_raise':
            ms = t*(data['raise_ms']+data['fire_ms'])
            deploy = ease(ms/data['raise_ms'])
            action = (ms-data['raise_ms'])/data['fire_ms']
            if action > 0: bolt = window(action,.1,.4)-window(action,.55,.82); hand = window(action,0,.18)-window(action,.82,1)
        else: deploy = ease(t if clip == 'raise' else 1-t)
        root = add(root,(4*(1-deploy),-2*(1-deploy),-20*(1-deploy)))
        angle = [15*(1-deploy),30*(1-deploy),-10*(1-deploy)]
    if clip == 'fire':
        ms = t*data['fire_ms']
        recoil = math.exp(-ms/75)*math.sin(min(1,ms/35)*math.pi/2)
        root[0] -= 1.8*recoil; angle[1] -= 5*recoil
        bolt = window(ms,data['bolt_open_ms'],390)-window(ms,560,data['bolt_close_ms'])
        hand = window(ms,80,220)-window(ms,data['bolt_close_ms'],data['fire_ms'])
    if clip.startswith('reload_'):
        fraction = ease(t) if clip == 'reload_start' else (1-ease(t) if clip == 'reload_end' else 1)
        root = add(root,(-2*fraction,1*fraction,-2*fraction)); angle=[-22*fraction,-8*fraction,0]
        bolt = ease(t) if clip == 'reload_start' else (1-window(t,0,data['reload_close_ms']/data['reload_end_ms']) if clip == 'reload_end' else 1)
        hand = fraction
        if clip == 'reload_loop':
            seat=data['round_in_ms']/data['reload_loop_ms']
            keys=[(0,(-1.6,-1.8,.6)),(.25,(0,-3,-2)),(seat,(2,-.6,1.2)),(1,(-1.6,-1.8,.6))]
            for (a,p),(b,q) in zip(keys,keys[1:]):
                if a<=t<=b:
                    result[2]=channels(add(p,scale(sub(q,p),ease((t-a)/(b-a)))),(-25,0,-10)); break
            cartridge = 1 if .25 < t < seat else .001
    if clip.startswith('sprint_'):
        fraction = ease(t) if clip == 'sprint_in' else (1-ease(t) if clip == 'sprint_out' else 1)
        bob = math.sin(math.tau*t) if clip == 'sprint_loop' else 0
        root = add(root,(-3*fraction,2*fraction,-3*fraction+.18*bob))
        angle = [-25*fraction,-12*fraction+1.2*bob,32*fraction]
    result[0] = channels(root,angle)
    result[1] = channels((2-2.6*bolt,0,.5),(-65*bolt,0,0))
    if clip != 'reload_loop':
        result[2] = channels((1-2.6*bolt,-.3-1.5*hand,-2.5+3.1*hand),(-25*hand,0,-10*hand))
    result[4] = channels((0,0,0),s=cartridge)
    return result


def clips(data):
    durations = {'idle':1600, 'raise':data['raise_ms'], 'drop':data['drop_ms'],
        'fire':data['fire_ms'], 'first_raise':data['raise_ms']+data['fire_ms']}
    for n in ('reload_start','reload_loop','reload_end','sprint_in','sprint_loop','sprint_out'):
        durations[n] = data[n+'_ms']
    result = []
    for name,ms in durations.items():
        count = max(2,round(ms*data['fps']/1000)); loop = name in ('idle','sprint_loop')
        frames = [pose(name,i/(count if loop else count-1),data) for i in range(count)]
        result.append((name,frames,count*1000/ms,loop))
    return result


def surface_parts(mesh):
    # Both MD3 and IQM must fit GL1's 999-vertex/1999-triangle draw surfaces.
    # Split at triangle boundaries without changing materials or bone weights.
    for (mat,joint),(vertices,triangles) in mesh.parts.items():
        remap={}; out=[]; faces=[]
        for tri in triangles:
            if len(out)+sum(i not in remap for i in tri)>900:
                yield mat,joint,out,faces
                remap={}; out=[]; faces=[]
            face=[]
            for i in tri:
                if i not in remap: remap[i]=len(out); out.append(vertices[i])
                face.append(remap[i])
            faces.append(tuple(face))
        if faces: yield mat,joint,out,faces


def iqm(mesh, animations):
    text = bytearray(b'\0')
    def label(s):
        start = len(text); text.extend(s.encode()+b'\0'); return start
    joints = [label(n) for n,_,_ in NODES]
    vertices, triangles, meshes = [], [], []
    for index,(mat,joint,v,tris) in enumerate(surface_parts(mesh)):
        first = len(vertices)
        meshes.append((label(mat+'_'+str(index)),label('peace/m40_'+mat),first,len(v),len(triangles),len(tris)))
        vertices.extend(v); triangles.extend(tuple(first+i for i in tri) for tri in tris)
    frames, anims, cfg = [], [], []
    for name,poses,fps,loop in animations:
        anims.append((label(name),len(frames),len(poses),fps,int(loop)))
        cfg.append(f'{name} {len(frames)} {len(poses)} {fps:.8g} {int(loop)}')
        frames.extend(poses)
    offsets, scales, masks = [], [], []
    for j in range(len(NODES)):
        lo = [min(f[j][k] for f in frames) for k in range(10)]
        hi = [max(f[j][k] for f in frames) for k in range(10)]
        s = [(b-a)/65535 if b-a>1e-7 else 0 for a,b in zip(lo,hi)]
        offsets.append(lo); scales.append(s); masks.append(sum(1<<k for k in range(10) if s[k]))
    packed = bytearray()
    for frame in frames:
        for j,c in enumerate(frame):
            for k in range(10):
                if scales[j][k]: packed += struct.pack('<H',max(0,min(65535,round((c[k]-offsets[j][k])/scales[j][k]))))
    blob = bytearray(124)
    def append(data):
        blob.extend(b'\0'*(-len(blob)%4)); pos=len(blob); blob.extend(data); return pos
    def rows(fmt,values): return b''.join(struct.pack(fmt,*v) for v in values)
    ot = append(text); om = append(rows('<6I',meshes))
    tangents = [(*norm(cross(v[1],(0,0,1) if abs(v[1][2])<.9 else (0,1,0))),1.) for v in vertices]
    arrays=[]
    for kind,fmt,size,values in [(0,'3f',3,[v[0] for v in vertices]),(1,'2f',2,[v[2] for v in vertices]),
        (2,'3f',3,[v[1] for v in vertices]),(3,'4f',4,tangents),
        (4,'4B',4,[(v[3],0,0,0) for v in vertices]),(5,'4B',4,[(255,0,0,0)]*len(vertices))]:
        arrays.append((kind,0,1 if 'B' in fmt else 7,size,append(rows('<'+fmt,values))))
    oa=append(rows('<5I',arrays)); otr=append(rows('<3I',[(a,c,b) for a,b,c in triangles]))
    oj=append(rows('<Ii10f',[(joints[i],parent,*channels(p)) for i,(_,parent,p) in enumerate(NODES)]))
    op=append(rows('<iI20f',[(parent,masks[i],*offsets[i],*scales[i]) for i,(_,parent,_) in enumerate(NODES)]))
    oan=append(rows('<3IfI',anims)); of=append(packed)
    ob=append(rows('<8f',[(-80,-80,-80,80,80,80,120,140)]*len(frames)))
    h=(2,len(blob),0,len(text),ot,len(meshes),om,6,len(vertices),oa,len(triangles),otr,0,
       len(NODES),oj,len(NODES),op,len(anims),oan,len(frames),sum(m.bit_count() for m in masks),of,ob,0,0,0,0)
    blob[:124]=struct.pack('<16s27I',b'INTERQUAKEMODEL\0',*h)
    return bytes(blob), ('// Generated from assets/source/m40a3/weapon.json\n'+'\n'.join(cfg)+'\n').encode()


def md3(mesh):
    surfaces=[]; points=[]
    for index,(mat,joint,vertices,tris) in enumerate(surface_parts(mesh)):
        # World item: no arms, no transient cartridge, centered along its length.
        pts=[sub(v[0],add(ORIGIN,(8,0,0))) for v in vertices]; points.extend(pts)
        st=108+len(tris)*12+68; xyz=st+len(vertices)*8; size=xyz+len(vertices)*8
        data=bytearray(struct.pack('<4s64s10i',b'IDP3',(mat+'_'+str(index)).encode(),0,1,1,len(vertices),len(tris),108,108+len(tris)*12,st,xyz,size))
        data+=b''.join(struct.pack('<3i',a,c,b) for a,b,c in tris)
        data+=struct.pack('<64si',('peace/m40_'+mat).encode(),0)
        data+=b''.join(struct.pack('<2f',*v[2]) for v in vertices)
        for p,vertex in zip(pts,vertices):
            n=vertex[1]; polar=round(math.acos(max(-1,min(1,n[2])))*255/math.tau)&255
            azimuth=round(math.atan2(n[1],n[0])*255/math.tau)&255
            data+=struct.pack('<3hH',*(round(v*64) for v in p),(azimuth<<8)|polar)
        assert len(data)==size; surfaces.append(data)
    lo=[min(p[i] for p in points) for i in range(3)]; hi=[max(p[i] for p in points) for i in range(3)]
    # tag_flash is used by the ordinary third-person weapon path too.
    tag=struct.pack('<64s12f',b'tag_flash',26,0,.5,1,0,0,0,1,0,0,0,1)
    start=164+len(tag); size=start+sum(map(len,surfaces))
    return struct.pack('<4si64s9i',b'IDP3',15,b'Peace original M40A3 inspired rifle',0,1,1,len(surfaces),0,108,164,start,size)+struct.pack('<10f16s',*lo,*hi,0,0,0,40,b'idle')+tag+b''.join(surfaces)


def tga(width,height,pixel):
    data=bytearray(struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,width,height,32,8))
    for y in reversed(range(height)):
        for x in range(width):
            r,g,b,a=pixel(x,y); data+=bytes((b,g,r,a))
    return bytes(data)


def scope_pixel(x,y):
    x=(x+.5-256)/256; y=(y+.5-256)/256; radius=math.hypot(x,y)
    edge=max(0,min(1,(radius-.85)/.009))
    line=(abs(x)<.003 or abs(y)<.003)
    post=(abs(x)<.012 and y>.23) or (abs(y)<.012 and abs(x)>.23)
    mil=any(math.hypot(x,y-k*.075)<.007 or math.hypot(x-k*.075,y)<.007 for k in range(-3,4) if k)
    a=255 if (line or post or mil) and radius<.85 else round(255*edge)
    return (8,9,8,a)


def fire_sound():
    rng=random.Random(4040); rate=44100; samples=[]; low=0
    for i in range(int(rate*.65)):
        t=i/rate; noise=rng.uniform(-1,1); low=.82*low+.18*noise
        attack=min(1,t/.001)
        value=attack*(.8*(noise-low)*math.exp(-t*60)+.9*low*math.exp(-t*9)+.5*math.sin(math.tau*(95*t-32*t*t))*math.exp(-t*22))
        samples.append(round(max(-1,min(1,value))*28000))
    f=io.BytesIO()
    with wave.open(f,'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(struct.pack('<'+'h'*len(samples),*samples))
    return f.getvalue()


def build():
    data=json.loads(RECIPE.read_text()); mesh=geometry(); model,cfg=iqm(mesh,clips(data))
    header='// Generated by dev/tools/build_m40a3.py; edit assets/source/m40a3/weapon.json.\n#ifndef BG_M40A3_DATA_H\n#define BG_M40A3_DATA_H\n'
    for name,value in data.items():
        header+=f'#define M40_{name.upper()} {value}{"f" if isinstance(value,float) else ""}\n'
    header+='#endif\n'
    shader='// Original generated texture and authored game geometry.\n'
    for mat in MATERIALS:
        if mat=='flash':
            shader+=f'peace/m40_flash\n{{\n cull none\n {{ map {PREFIX}flash.tga\n blendFunc add\n rgbGen identity\n }}\n}}\n'
            continue
        if mat=='glass':
            shader+=f'peace/m40_glass\n{{\n {{ map $whiteimage\n rgbGen const ( .025 .055 .065 )\n }}\n {{ map {PREFIX}materials.jpg\n tcGen environment\n blendFunc GL_ONE GL_ONE\n rgbGen const ( .12 .2 .22 )\n }}\n}}\n'
            continue
        shader+=f'peace/m40_{mat}\n{{\n {{ map {PREFIX}materials.jpg\n rgbGen lightingDiffuse\n }}\n'
        if mat in ('steel','brass'):
            color='.14 .16 .17' if mat=='steel' else '.45 .27 .08'
            shader+=f' {{ map $whiteimage\n blendFunc GL_ONE GL_ONE\n rgbGen const ( {color} )\n }}\n'
        shader+='}\n'
    shader+='peace/m40_scope\n{\n nopicmip\n nomipmaps\n { map '+PREFIX+'scope.tga\n blendFunc blend\n rgbGen vertex\n alphaGen vertex\n }\n}\n'
    shader+='icons/peace_m40a3\n{\n nopicmip\n { map icons/peace_m40a3.tga\n blendFunc blend\n }\n}\n'
    # HUD silhouette is projected from the actual authored world mesh.
    silhouette=set()
    for (_,joint),(vertices,tris) in geometry(False).parts.items():
        for tri in tris:
            pts=[((vertices[i][0][0]+10)*2.2,40-(vertices[i][0][2]+6)*4) for i in tri]
            lo=[max(0,math.floor(min(p[k] for p in pts))) for k in range(2)]
            hi=[min((127,63)[k],math.ceil(max(p[k] for p in pts))) for k in range(2)]
            for y in range(lo[1],hi[1]+1):
                for x in range(lo[0],hi[0]+1):
                    signs=[(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]) for a,b in zip(pts,pts[1:]+pts[:1])]
                    if min(signs)>=0 or max(signs)<=0: silhouette.add((x,y))
    eye=add(ORIGIN,(-6,0,3.5))
    sights=f'// Eye behind the modeled ocular; scope is integral, not a red-dot attachment.\niron {eye[0]} {eye[1]} {eye[2]} 0 0 0\ntiming {data["ads_in_ms"]} {data["ads_out_ms"]}\nfov {data["fov_scale"]}\n'+('scope peace/m40_scope\n' if data['scope_overlay'] else '')
    flash=Mesh()
    flash.face('flash',0,[(8,-2,0),(12,-2,0),(12,2,0),(8,2,0)])
    flash.face('flash',0,[(8,0,-2),(12,0,-2),(12,0,2),(8,0,2)])
    def flare(x,y):
        r=math.hypot((x-63.5)/64,(y-63.5)/64)
        value=max(0,1-r)**2
        return (round(255*value),round(205*value),round(85*value),255)
    return {'code/game/bg_m40a3_data.h':header.encode(),
        'assets/baseq3/'+PREFIX+'view.iqm':model,'assets/baseq3/'+PREFIX+'view.cfg':cfg,
        'assets/baseq3/'+PREFIX+'view.sights':sights.encode(),
        'assets/baseq3/'+PREFIX+'world.md3':md3(geometry(False)),
        'assets/baseq3/'+PREFIX+'world_flash.md3':md3(flash),
        'assets/baseq3/'+PREFIX+'flash.tga':tga(128,128,flare),
        'assets/baseq3/'+PREFIX+'scope.tga':tga(512,512,scope_pixel),
        'assets/baseq3/scripts/peace_m40a3.shader':shader.encode(),
        'assets/baseq3/icons/peace_m40a3.tga':tga(128,64,lambda x,y:(222,230,210,255 if (x,y) in silhouette else 0)),
        'assets/baseq3/sound/weapons/m40a3/fire.wav':fire_sound()}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--check',action='store_true'); args=parser.parse_args()
    for name,data in build().items():
        path=ROOT/name
        if args.check: assert path.read_bytes()==data, 'Stale generated asset: '+name
        else: path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)
        print(('Checked' if args.check else 'Built')+f' {name} ({len(data)} bytes)')
