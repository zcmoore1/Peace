#!/usr/bin/env python3
"""Build Peace's original basic M16 and mounted Masterkey game assets.

Deterministic, stdlib only; --check compares without writing. The geometry is
stylized game art, not a mechanical drawing. No external game assets are used.
The IQM/MD3 layout follows the existing build_m40a3.py exporter.
"""
import argparse
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'models/weapons2/m16/'
ORIGIN = (12.0, -4.8, -6.0)
NODES = [('Base', -1, ORIGIN), ('magazine', 0, (1.6, 0, -2.5)),
         ('right_hand', 0, (-4, 0, -4.0)), ('left_hand', 0, (16, 0, -5.3)),
         ('pump', 0, (16, 0, -3.8)), ('charging_handle', 0, (-4.5, 0, 1.3)),
         ('tag_flash', 0, (31.8, 0, .45)),
         ('tag_underbarrel', 0, (25.5, 0, -3.8)),
         ('shell', 0, (8, 0, -6))]
MATERIALS = {'metal': (0, 0), 'polymer': (1, 0), 'fabric': (0, 1),
             'rubber': (1, 1), 'steel': (0, 0), 'shell': (0, 0), 'flash': (0, 0)}


def nodes(underbarrel=False):
    result=list(NODES)
    if underbarrel:
        result[6]=('tag_flash',0,NODES[7][2])
        result[7]=('tag_rifle',0,NODES[6][2])
    return result


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
    cx, cy, cz = math.cos(x), math.cos(y), math.cos(z)
    sx, sy, sz = math.sin(x), math.sin(y), math.sin(z)
    return (sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz,
            cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz)
def channels(p, angles=(0, 0, 0), s=1): return (*p, *quat(angles), s, s, s)


class Mesh:
    def __init__(self): self.parts = {}

    def face(self, mat, joint, points, uv=None):
        # Ear clipping preserves concave receiver outlines. A triangle fan can
        # put triangles outside the face or reverse them at a re-entrant corner.
        n = norm(tuple(sum((a[(k+1)%3]-b[(k+1)%3]) *
                           (a[(k+2)%3]+b[(k+2)%3])
                           for a,b in zip(points, points[1:]+points[:1]))
                       for k in range(3)))
        axis = max(range(3), key=lambda k: abs(n[k]))
        flat = [tuple(p[(axis+k)%3] for k in (1,2)) for p in points]
        direction = 1 if n[axis] > 0 else -1
        def turn(a,b,c):
            return direction*((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        remaining=list(range(len(points))); faces=[]
        while len(remaining)>3:
            for index,b in enumerate(remaining):
                a,c=remaining[index-1],remaining[(index+1)%len(remaining)]
                if turn(flat[a],flat[b],flat[c]) <= 1e-9: continue
                if any(all(turn(flat[i],flat[j],flat[p]) >= -1e-9
                           for i,j in ((a,b),(b,c),(c,a)))
                       for p in remaining if p not in (a,b,c)): continue
                faces.append((a,b,c));remaining.pop(index);break
            else: raise ValueError('Non-simple or degenerate polygon: '+repr(points))
        faces.append(tuple(remaining))
        vertices, triangles = self.parts.setdefault((mat, joint), ([], []))
        start = len(vertices)
        for i, p in enumerate(points):
            st = uv[i] if uv else ((i in (1, 2)), (i >= 2))
            if mat != 'flash': st = tuple(MATERIALS[mat][k]*.5+.025+.45*st[k] for k in range(2))
            vertices.append((add(p, ORIGIN), n, st, joint))
        triangles.extend(tuple(start+i for i in face) for face in faces)

    def prism(self, mat, outline, width, joint=0, center_y=0):
        """Extrude a simple x/z outline, with outward normals on both caps."""
        if sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(outline,outline[1:]+outline[:1])) < 0:
            outline=list(reversed(outline))
        front = [(x, center_y-width, z) for x, z in outline]
        back = [(x, center_y+width, z) for x, z in outline]
        self.face(mat, joint, front)
        self.face(mat, joint, list(reversed(back)))
        for i in range(len(front)):
            k = (i+1) % len(front)
            self.face(mat, joint, [front[i], back[i], back[k], front[k]])

    def box(self, mat, lo, hi, joint=0):
        x, y, z = lo; X, Y, Z = hi
        self.prism(mat, [(x,z), (X,z), (X,Z), (x,Z)], (Y-y)/2, joint, (Y+y)/2)

    def loft(self, mat, rings, joint=0, sides=12, caps=True):
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

    def rod(self, mat, a, b, ra, rb=None, joint=0, sides=10):
        axis = norm(sub(b, a))
        u = norm(cross(axis, (0, 0, 1) if abs(axis[2]) < .9 else (0, 1, 0)))
        v = cross(axis, u); rb = ra if rb is None else rb
        rows = [[add(p, add(scale(u, r*math.cos(math.tau*i/sides)),
                            scale(v, r*math.sin(math.tau*i/sides))))
                 for i in range(sides)] for p, r in [(a, ra), (b, rb)]]
        for i in range(sides):
            k = (i+1) % sides
            self.face(mat, joint, [rows[0][i], rows[0][k], rows[1][k], rows[1][i]])
        self.face(mat, joint, list(reversed(rows[0])))
        self.face(mat, joint, rows[1])

    def tube(self, mat, a, b, y, z, outer, inner, joint=0, sides=16):
        self.loft(mat, [(a,y,z,outer,outer),(b,y,z,outer,outer)],joint,sides,False)
        self.loft('rubber', [(b,y,z,inner,inner),(a,y,z,inner,inner)],joint,sides,False)
        # Close the two annular rims without covering the bore/aperture.
        for x, reverse in [(a,True),(b,False)]:
            for i in range(sides):
                j = (i+1) % sides
                pts = [(x,y+r*math.cos(math.tau*k/sides),z+r*math.sin(math.tau*k/sides))
                       for r,k in [(outer,i),(outer,j),(inner,j),(inner,i)]]
                self.face(mat,joint,list(reversed(pts)) if reverse else pts)


def glove(m, joint, center, elbow):
    x,y,z = center
    m.rod('fabric',elbow,add(center,(-2.1,0,-.4)),1.55,1.10,joint=joint,sides=12)
    m.rod('rubber',add(center,(-2.6,0,-.5)),add(center,(-1.35,0,-.2)),1.16,1.03,joint=joint)
    m.loft('rubber',[(x-1.5,y,z,.83,.6),(x,y,z,1,.72),(x+1.3,y,z,.82,.62)],joint)
    for k in range(4):
        X=x-.8+k*.59
        m.rod('rubber',(X,y-1,z),(X+.12,y-1.03,z+1.12),.27,.24,joint=joint,sides=8)
        m.rod('rubber',(X+.12,y-1.03,z+1.12),(X+.18,y-.36,z+1.42),.24,.2,joint=joint,sides=8)
    m.rod('rubber',(x-.8,y+.76,z+.1),(x+.8,y+.67,z+1.12),.34,.25,joint=joint,sides=8)


def geometry(hands=True):
    m=Mesh()
    # Fixed rifle stock and rubber butt pad; open receiver/stock junction.
    stock=[(-18,-3.6),(-15.5,-3.2),(-7,-1.7),(-5.8,-.6),(-6.5,1.1),(-17.5,1.7)]
    m.prism('polymer',stock,1.08)
    m.prism('rubber',[(-18.6,-3.65),(-18,-3.6),(-17.5,1.7),(-18.1,1.78)],1.15)
    m.loft('metal',[(-7,0,.2,.65,.65),(-4.5,0,.2,.65,.65)])
    # Upper and lower receiver with recognizable magwell and slanted grip.
    m.prism('metal',[(-5.3,-1.1),(7.2,-1.1),(8,.15),(6.7,1.15),(-4.8,1.15)],.86)
    m.prism('metal',[(-4.8,-2.1),(-.5,-2.1),(.1,-2.8),(4.3,-2.8),(5.7,-1.0),(-4.8,-1.0)],.76)
    m.prism('polymer',[(-6.0,-6.0),(-3.7,-5.6),(-2.7,-2.0),(-4.7,-1.7)],.65)
    for z in (-5.3,-4.6,-3.9):
        m.box('rubber',(-5.9,-.69,z),(-3.8,.69,z+.13))
    for a,b in [((-3.7,0,-2),(-3.3,0,-3.5)),((-3.3,0,-3.5),(-.4,0,-3.5)),
                ((-.4,0,-3.5),(.0,0,-2.0))]: m.rod('metal',a,b,.16)
    m.rod('steel',(-1.7,0,-1.7),(-1.0,0,-2.8),.16)
    # Curved detachable magazine: split into convex sections to keep the caps valid.
    mag=[(-.2,-2.0),(3.3,-2.0),(3.7,-5.2),(.1,-5.7)]
    m.prism('steel',mag,.68,joint=1)
    m.prism('steel',[(.1,-5.7),(3.7,-5.2),(5.0,-9.1),(2.8,-9.8),(1.5,-8.6)],.68,joint=1)
    m.prism('rubber',[(2.7,-10),(5.2,-9.3),(5,-8.95),(2.6,-9.65)],.76,joint=1)
    for side in (-1,1):
        y=side*.715
        for x in (.7,1.7,2.7):
            m.rod('metal',(x,y,-2.8),(x+.3,y,-5.4),.075,joint=1,sides=6)
            m.rod('metal',(x+.3,y,-5.4),(x+1.3,y,-8.9),.075,joint=1,sides=6)
    # Receiver hardware, ejection port and cover on the right side.
    m.box('rubber',(-.2,-.9,-.4),(4.3,-.87,.65))
    m.box('metal',(-.4,-1.02,-.55),(4.5,-.9,-.37))
    m.rod('steel',(-.4,-1.03,-.37),(4.5,-1.03,-.37),.10)
    for side in (-1,1):
        for x,z in [(-4,-.5),(4.8,-1.25)]:
            m.rod('steel',(x,side*.81,z),(x,side*.94,z),.20,sides=10)
    m.rod('metal',(-2.8,.86,-1.25),(-2.8,1.12,-1.25),.3)
    m.rod('steel',(-2.8,1.15,-1.25),(-3.4,1.15,-1.6),.1)
    m.rod('metal',(-2.6,-.8,.25),(-3.8,-1.4,.3),.28,.4)
    # Charging handle and hollow carry handle. Its aperture aligns with the front post.
    m.box('steel',(-5.5,-1.2,1.1),(-4.7,1.2,1.4),joint=5)
    m.prism('metal',[(-4,1.1),(-2.9,1.1),(-2.1,3.0),(-3.2,3.0)],.45)
    m.prism('metal',[(4.6,1.1),(6.4,1.1),(5.3,3.0),(4.4,3.0)],.45)
    m.box('metal',(-3.2,-.57,2.8),(5.3,.57,3.25))
    m.box('steel',(-3.1,-.82,2.65),(-1.6,.82,3.05))
    m.tube('metal',-2.85,-2.55,0,3.65,.6,.36,sides=20)
    m.rod('metal',(-2.6,.55,2.95),(-2.6,1.16,2.95),.36)
    # Long ribbed rifle handguard, thin exposed barrel and vented flash hider.
    m.loft('metal',[(7.0,0,.35,1.0,1),(8,0,.35,1.2,1.2)])
    m.loft('polymer',[(8,0,.35,1.20,1.18),(10,0,.35,1.17,1.15),
                      (18.8,0,.35,.90,.91),(20,0,.35,.84,.87)],sides=16)
    for i in range(13):
        x=8.65+i*.80; r=1.2-(x-8)*.031
        m.loft('rubber',[(x,0,.35,r+.035,r+.035),(x+.18,0,.35,r+.035,r+.035)],sides=16,caps=False)
    m.loft('steel',[(19.5,0,.45,.35,.35),(29.4,0,.45,.32,.32)],sides=16)
    m.tube('metal',29.4,31.8,0,.45,.52,.26,sides=16)
    for side in (-1,1):
        for x in (30.0,30.7):
            m.box('rubber',(x,side*.46-.045,.25),(x+.34,side*.46+.045,.66))
    # A-frame front sight, with two open ears around a distinct central post.
    m.loft('metal',[(24.2,0,.45,.57,.57),(25.2,0,.45,.57,.57)])
    for side in (-1,1):
        m.rod('metal',(24.7,side*.95,.5),(24.7,side*.36,3.45),.19,.14,sides=8)
        m.rod('metal',(24.7,side*.36,3.25),(24.7,side*.52,4.1),.14,.13,sides=8)
    m.rod('metal',(24.7,-.5,2.5),(24.7,.5,2.5),.21,sides=8)
    m.rod('steel',(24.7,0,2.6),(24.7,0,3.65),.10,.075,sides=8)
    # Attached Masterkey: twin mounting blocks, receiver, magazine tube and
    # larger short muzzle. Its pump is separately weighted, not decoration.
    for x in (7.8,12.1):
        m.box('steel',(x-.35,-.54,-2.5),(x+.35,.54,-.60))
        m.box('metal',(x-.58,-.95,-2.7),(x+.58,.95,-2.3))
        for side in (-1,1): m.rod('steel',(x,side*.93,-2.5),(x,side*1.08,-2.5),.17,sides=8)
    m.prism('metal',[(5.7,-4.35),(11.5,-4.35),(12.3,-3.7),(11.5,-2.5),(6.0,-2.5)],.83)
    m.box('rubber',(7.1,-.87,-3.7),(10.8,-.84,-2.95))
    m.tube('metal',11.0,25.5,0,-3.8,.55,.38,sides=16)
    m.loft('metal',[(11.3,0,-4.95,.44,.44),(22.4,0,-4.95,.44,.44),(22.7,0,-4.95,.35,.35)],sides=12)
    m.loft('polymer',[(13.2,0,-4.20,.82,1.08),(13.5,0,-4.20,1.04,1.2),
                      (18.5,0,-4.20,1.04,1.2),(18.9,0,-4.20,.82,1.08)],joint=4,sides=12)
    for i in range(8):
        x=13.7+i*.60
        m.loft('rubber',[(x,0,-4.2,1.08,1.25),(x+.17,0,-4.2,1.08,1.25)],joint=4,sides=12,caps=False)
    for a,b in [((6.3,0,-4.3),(6.2,0,-5.5)),((6.2,0,-5.5),(8.4,0,-5.5)),((8.4,0,-5.5),(8.6,0,-4.3))]:
        m.rod('metal',a,b,.14,sides=8)
    m.rod('steel',(7.4,0,-4.35),(7.1,0,-5.1),.13,sides=8)
    if hands:
        glove(m,2,(-4,0,-4.0),(-17,-5.5,-11.0))
        glove(m,3,(16,0,-5.3),(-.5,6.0,-12.0))
        m.loft('shell',[(7.2,0,-6,.27,.27),(8.8,0,-6,.27,.27)],joint=8,sides=10)
        m.loft('steel',[(7.0,0,-6,.3,.3),(7.3,0,-6,.3,.3)],joint=8,sides=10)
    return m


def pose(name,t,data,sprint):
    underbarrel=data.get('underbarrel',False)
    result=[channels(p) for _,_,p in nodes(underbarrel)]
    # A shell is shown only while being fed through the loading port.
    result[8]=channels(NODES[8][2],s=.0001)
    root=list(ORIGIN); angle=[0,0,0]
    if underbarrel:
        result[2]=channels(add(NODES[2][2],(11.2,0,-.8)))
    if name=='idle': root[2]+=.045*math.sin(math.tau*t)
    if name in ('raise','drop','first_raise'):
        deploy=ease(t if name!='drop' else 1-t)
        root=add(root,(2*(1-deploy),-1.5*(1-deploy),-15*(1-deploy)))
        angle=[8*(1-deploy),24*(1-deploy),-8*(1-deploy)]
    if name in ('fire','cycle'):
        ms=t*data['fire_ms']
        kick=math.sin(math.pi*min(ms/140,1)) if underbarrel else math.sin(math.pi*t)*(1-t*.55)
        if name=='cycle': kick=0
        root[0]-=(1.3 if underbarrel else .62)*kick
        angle[1]-=(3.2 if underbarrel else 1.35)*kick
        if underbarrel:
            pump=window(ms,data['bolt_open_ms'],360)-window(ms,430,data['bolt_close_ms'])
            result[4]=channels(add(NODES[4][2],(-3.7*pump,0,0)))
            result[3]=channels(add(NODES[3][2],(-3.7*pump,0,0)))
            angle[0]=-2*pump
    if name=='reload_loop' and not underbarrel:
        ms=t*data['reload_loop_ms']
        tilt=window(ms,0,240)-window(ms,1080,data['reload_loop_ms'])
        root=add(root,(-1.0*tilt,.6*tilt,-1.1*tilt)); angle=[-20*tilt,-4*tilt,3*tilt]
        out=window(ms,data['mag_out_ms']-160,data['mag_out_ms']+100)
        seat=window(ms,700,data['mag_in_ms'])
        travel=max(0,out-seat)
        result[1]=channels(add(NODES[1][2],(-1.2*travel,0,-8.5*travel)),(0,0,-8*travel))
        reach=window(ms,50,250)-window(ms,1080,1340)
        result[3]=channels(add(NODES[3][2],(-14.2*reach,-1.0*reach,-1.5*reach-5*travel)),(0,0,15*reach))
    if name.startswith('reload_') and underbarrel:
        reach=ease(t) if name=='reload_start' else (1-ease(t) if name=='reload_end' else 1)
        root=add(root,(-.5*reach,.5*reach,-.6*reach));angle=[-18*reach,-3*reach,0]
        travel=0
        if name=='reload_loop':
            ms=t*data['reload_loop_ms']
            travel=window(ms,0,100)-window(ms,100,data['round_in_ms'])
            visible=window(ms,30,60)-window(ms,data['round_in_ms']-30,data['round_in_ms'])
            result[8]=channels(add(NODES[8][2],(0,0,-4*travel)),s=max(.0001,visible))
        result[3]=channels(add(NODES[3][2],(-8*reach,0,-1.2*reach-4*travel)),(0,0,12*reach))
    if name.startswith('sprint_'):
        carry=ease(t) if name=='sprint_in' else (1-ease(t) if name=='sprint_out' else 1)
        wave=math.sin(math.tau*t) if name=='sprint_loop' else 0
        root=add(root,add(scale(sprint['offset'],carry),scale(sprint['bob'],wave)))
        pitch,yaw,roll=add(scale(sprint['pitch_yaw_roll'],carry),scale(sprint['sway_pitch_yaw_roll'],wave))
        angle=[roll,pitch,yaw]
    result[0]=channels(root,angle)
    return result


def clips(data,sprint):
    durations={'idle':1600,'raise':data['raise_ms'],'drop':data['drop_ms'],
               'fire':data['fire_ms'],'reload_loop':data['reload_loop_ms'],
               'sprint_in':data['sprint_in_ms'],'sprint_loop':data['sprint_loop_ms'],
               'sprint_out':data['sprint_out_ms'],'first_raise':data['raise_ms']}
    if data.get('underbarrel'):
        durations['reload_start']=data['reload_start_ms']
        durations['reload_end']=data['reload_end_ms']
        # First deployment includes the worked action, matching the shared
        # draw clock (raise + fire cycle). Ordinary mode changes only raise.
        durations['first_raise']=data['raise_ms']+data['fire_ms']
    result=[]
    for name,ms in durations.items():
        count=max(2,round(ms*data['fps']/1000)); loop=name in ('idle','sprint_loop')
        frames=[]
        for i in range(count):
            t=i/(count if loop else count-1)
            if name=='first_raise' and data.get('underbarrel'):
                ms=t*durations[name]
                frame=(pose('raise',ms/data['raise_ms'],data,sprint) if ms<data['raise_ms']
                       else pose('cycle',(ms-data['raise_ms'])/data['fire_ms'],data,sprint))
            else: frame=pose(name,t,data,sprint)
            frames.append(frame)
        result.append((name,frames,count*1000/ms,loop))
    return result


def surface_parts(mesh):
    # GL1 has a 999-vertex/1999-triangle surface limit.
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


def iqm(mesh,animations,underbarrel=False):
    rig=nodes(underbarrel)
    text=bytearray(b'\0')
    def label(s):
        offset=len(text);text.extend(s.encode()+b'\0');return offset
    joints=[label(n) for n,_,_ in rig]
    vertices=[];triangles=[];meshes=[]
    for i,(mat,joint,v,tris) in enumerate(surface_parts(mesh)):
        first=len(vertices)
        meshes.append((label(mat+'_'+str(i)),label('peace/m16_'+mat),first,len(v),len(triangles),len(tris)))
        vertices.extend(v);triangles.extend(tuple(first+k for k in tri) for tri in tris)
    frames=[];anims=[];cfg=[]
    for name,poses,fps,loop in animations:
        anims.append((label(name),len(frames),len(poses),fps,int(loop)))
        cfg.append(f'{name} {len(frames)} {len(poses)} {fps:.8g} {int(loop)}')
        frames.extend(poses)
    offsets=[];scales=[];masks=[]
    for j in range(len(NODES)):
        lo=[min(f[j][k] for f in frames) for k in range(10)]
        hi=[max(f[j][k] for f in frames) for k in range(10)]
        s=[(b-a)/65535 if b-a>1e-7 else 0 for a,b in zip(lo,hi)]
        offsets.append(lo);scales.append(s);masks.append(sum(1<<k for k in range(10) if s[k]))
    packed=bytearray()
    for frame in frames:
        for j,c in enumerate(frame):
            for k in range(10):
                if scales[j][k]: packed+=struct.pack('<H',max(0,min(65535,round((c[k]-offsets[j][k])/scales[j][k]))))
    blob=bytearray(124)
    def append(data):
        blob.extend(b'\0'*(-len(blob)%4));pos=len(blob);blob.extend(data);return pos
    def rows(fmt,values):return b''.join(struct.pack(fmt,*v) for v in values)
    ot=append(text);om=append(rows('<6I',meshes))
    tangents=[(*norm(cross(v[1],(0,0,1) if abs(v[1][2])<.9 else (0,1,0))),1.) for v in vertices]
    arrays=[]
    for kind,fmt,size,values in [(0,'3f',3,[v[0] for v in vertices]),(1,'2f',2,[v[2] for v in vertices]),
        (2,'3f',3,[v[1] for v in vertices]),(3,'4f',4,tangents),
        (4,'4B',4,[(v[3],0,0,0) for v in vertices]),(5,'4B',4,[(255,0,0,0)]*len(vertices))]:
        arrays.append((kind,0,1 if 'B' in fmt else 7,size,append(rows('<'+fmt,values))))
    oa=append(rows('<5I',arrays));otr=append(rows('<3I',[(a,c,b) for a,b,c in triangles]))
    oj=append(rows('<Ii10f',[(joints[i],parent,*channels(p)) for i,(_,parent,p) in enumerate(rig)]))
    op=append(rows('<iI20f',[(parent,masks[i],*offsets[i],*scales[i]) for i,(_,parent,_) in enumerate(rig)]))
    oan=append(rows('<3IfI',anims));of=append(packed)
    ob=append(rows('<8f',[(-80,-80,-80,80,80,80,120,140)]*len(frames)))
    h=(2,len(blob),0,len(text),ot,len(meshes),om,6,len(vertices),oa,len(triangles),otr,0,
       len(NODES),oj,len(NODES),op,len(anims),oan,len(frames),sum(m.bit_count() for m in masks),of,ob,0,0,0,0)
    blob[:124]=struct.pack('<16s27I',b'INTERQUAKEMODEL\0',*h)
    return bytes(blob),('// Generated by build_m16.py; provisional animation timing.\n'+'\n'.join(cfg)+'\n').encode()


def md3(mesh,underbarrel=False):
    surfaces=[];points=[]
    for index,(mat,joint,vertices,tris) in enumerate(surface_parts(mesh)):
        pts=[sub(v[0],add(ORIGIN,(6,0,0))) for v in vertices];points.extend(pts)
        st=108+len(tris)*12+68;xyz=st+len(vertices)*8;size=xyz+len(vertices)*8
        data=bytearray(struct.pack('<4s64s10i',b'IDP3',(mat+'_'+str(index)).encode(),0,1,1,len(vertices),len(tris),108,108+len(tris)*12,st,xyz,size))
        data+=b''.join(struct.pack('<3i',a,c,b) for a,b,c in tris)
        data+=struct.pack('<64si',('peace/m16_'+mat).encode(),0)
        data+=b''.join(struct.pack('<2f',*v[2]) for v in vertices)
        for p,vertex in zip(pts,vertices):
            n=vertex[1];polar=round(math.acos(max(-1,min(1,n[2])))*255/math.tau)&255
            azimuth=round(math.atan2(n[1],n[0])*255/math.tau)&255
            data+=struct.pack('<3hH',*(round(v*64) for v in p),(azimuth<<8)|polar)
        assert len(data)==size;surfaces.append(data)
    lo=[min(p[i] for p in points) for i in range(3)];hi=[max(p[i] for p in points) for i in range(3)]
    tags=b''.join(struct.pack('<64s12f',name.encode(),*sub(p,(6,0,0)),1,0,0,0,1,0,0,0,1)
                  for name,_,p in nodes(underbarrel) if name.startswith('tag_'))
    start=164+len(tags);size=start+sum(map(len,surfaces))
    return struct.pack('<4si64s9i',b'IDP3',15,b'Peace original M16 with Masterkey',0,1,2,len(surfaces),0,108,164,start,size)+struct.pack('<10f16s',*lo,*hi,0,0,0,45,b'idle')+tags+b''.join(surfaces)


def tga(width,height,pixel):
    data=bytearray(struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,width,height,32,8))
    for y in reversed(range(height)):
        for x in range(width):
            r,g,b,a=pixel(x,y);data+=bytes((b,g,r,a))
    return bytes(data)


def material_pixel(x,y):
    colors=[(65,70,75),(32,36,39),(72,79,65),(24,28,29)]
    tile=(x//256)+(y//256)*2
    grain=((x*13+y*37+(x*y)%17)%11)-5
    weave=2 if tile==2 and (x+y)%4==0 else 0
    edge=3 if tile==0 and x%29==0 else 0
    return (*[max(0,min(255,c+grain+weave+edge)) for c in colors[tile]],255)


def icon(mesh):
    silhouette=set()
    for (_,_),(vertices,triangles) in mesh.parts.items():
        for tri in triangles:
            pts=[((vertices[i][0][0]-ORIGIN[0]+19)*2.43,47-(vertices[i][0][2]-ORIGIN[2])*3.2) for i in tri]
            lo=[max(0,math.floor(min(p[k] for p in pts))) for k in range(2)]
            hi=[min((127,63)[k],math.ceil(max(p[k] for p in pts))) for k in range(2)]
            for y in range(lo[1],hi[1]+1):
                for x in range(lo[0],hi[0]+1):
                    signs=[(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]) for a,b in zip(pts,pts[1:]+pts[:1])]
                    if min(signs)>=0 or max(signs)<=0:silhouette.add((x,y))
    return tga(128,64,lambda x,y:(222,228,218,255 if (x,y) in silhouette else 0))


def build():
    data=json.loads((ROOT/'assets/source/m16/weapon.json').read_text())
    sprint=json.loads((ROOT/'assets/source/m16/sprint.json').read_text())
    mesh=geometry();model,cfg=iqm(mesh,clips(data,sprint));world=geometry(False)
    shotgun=dict(data['masterkey'],fps=data['fps'],underbarrel=True)
    altmodel,altcfg=iqm(mesh,clips(shotgun,sprint),True)
    shaders='// Original basic M16 + Masterkey materials.\n'
    for mat in MATERIALS:
        if mat=='flash':
            shaders+=f'peace/m16_flash\n{{\n cull none\n {{ map {PREFIX}flash.tga\n blendFunc add\n rgbGen identity\n }}\n}}\n'
            continue
        shaders+=f'peace/m16_{mat}\n{{\n {{ map {PREFIX}materials.tga\n rgbGen lightingDiffuse\n }}\n'
        if mat=='steel':shaders+=' { map $whiteimage\n blendFunc GL_ONE GL_ONE\n rgbGen const ( .045 .05 .055 )\n }\n'
        if mat=='shell':shaders+=' { map $whiteimage\n blendFunc GL_DST_COLOR GL_ZERO\n rgbGen const ( .75 .15 .10 )\n }\n'
        shaders+='}\n'
    shaders+='icons/peace_m16\n{\n nopicmip\n { map icons/peace_m16.tga\n blendFunc blend\n }\n}\n'
    # Leave the rear aperture beyond the engine's 4-unit near plane in ADS.
    eye=add(ORIGIN,(-8.5,0,3.65))
    def sights(source):
        return (f'// Eye behind the modeled rear aperture, on the front-post axis.\n'
                f'iron {eye[0]} {eye[1]} {eye[2]} 0 0 0\n'
                f'timing {source["ads_in_ms"]} {source["ads_out_ms"]}\n'
                f'zoomfov {source["ads_zoom_fov"]}\n').encode()
    flash=Mesh()
    flash.face('flash',0,[(6,-1.2,0),(9,-1.2,0),(9,1.2,0),(6,1.2,0)])
    flash.face('flash',0,[(6,0,-1.2),(9,0,-1.2),(9,0,1.2),(6,0,1.2)])
    def flare(x,y):
        r=math.hypot((x-31.5)/32,(y-31.5)/32);v=max(0,1-r)**2
        return round(255*v),round(205*v),round(90*v),255
    header=['/* Generated by dev/tools/build_m16.py from assets/source/m16/weapon.json. */',
            '#ifndef BG_M16_DATA_H','#define BG_M16_DATA_H']
    names={'MAG_SIZE':'mag_size','RESERVE':'reserve','FIRE_MS':'fire_ms','DROP_MS':'drop_ms',
           'RAISE_MS':'raise_ms','SPRINT_IN_MS':'sprint_in_ms','SPRINT_OUT_MS':'sprint_out_ms'}
    for prefix,source,extra in [('M16',data,{'RELOAD_MS':'reload_loop_ms','MAG_OUT_MS':'mag_out_ms','MAG_IN_MS':'mag_in_ms'}),
        ('MASTERKEY',shotgun,{'BOLT_OPEN_MS':'bolt_open_ms','BOLT_CLOSE_MS':'bolt_close_ms',
         'RELOAD_START_MS':'reload_start_ms','RELOAD_LOOP_MS':'reload_loop_ms','RELOAD_END_MS':'reload_end_ms',
         'ROUND_IN_MS':'round_in_ms','RELOAD_CLOSE_MS':'reload_close_ms'})]:
        for macro,key in dict(names,**extra).items():header.append(f'#define {prefix}_{macro} {source[key]}')
    header+=['#endif','']
    return {'assets/baseq3/'+PREFIX+'view.iqm':model,'assets/baseq3/'+PREFIX+'view.cfg':cfg,
            'assets/baseq3/'+PREFIX+'view.sights':sights(data),
            'assets/baseq3/'+PREFIX+'masterkey.iqm':altmodel,
            'assets/baseq3/'+PREFIX+'masterkey.cfg':altcfg,
            'assets/baseq3/'+PREFIX+'masterkey.sights':sights(shotgun),
            'assets/baseq3/'+PREFIX+'world.md3':md3(world),
            'assets/baseq3/'+PREFIX+'masterkey_world.md3':md3(world,True),
            'assets/baseq3/'+PREFIX+'world_flash.md3':md3(flash),
            'assets/baseq3/'+PREFIX+'masterkey_world_flash.md3':md3(flash),
            'assets/baseq3/'+PREFIX+'materials.tga':tga(512,512,material_pixel),
            'assets/baseq3/'+PREFIX+'flash.tga':tga(64,64,flare),
            'assets/baseq3/scripts/peace_m16.shader':shaders.encode(),
            'assets/baseq3/icons/peace_m16.tga':icon(world),
            'code/game/bg_m16_data.h':'\n'.join(header).encode()}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for name,data in build().items():
        path=ROOT/name
        if args.check:
            actual=path.read_text().encode() if path.suffix in ('.cfg','.sights','.shader','.h') else path.read_bytes()
            assert actual==data,'Stale generated asset: '+name
        else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        print(('Checked' if args.check else 'Built')+f' {name} ({len(data)} bytes)')
    parts=list(surface_parts(geometry()))
    print(f'View mesh: {sum(len(v) for _,_,v,_ in parts)} vertices, '
          f'{sum(len(t) for _,_,_,t in parts)} triangles, {len(parts)} surfaces, {len(NODES)} joints')
