#!/usr/bin/env python3
"""Independently inspect shipped rifle mesh, animation channels and metadata."""
from pathlib import Path
import json
import math
import struct

ROOT=Path(__file__).resolve().parents[2]
MODEL=ROOT/'assets/baseq3/models/weapons2/m40a3'
recipe=json.loads((ROOT/'assets/source/m40a3/weapon.json').read_text())
data=(MODEL/'view.iqm').read_bytes()
h=struct.unpack_from('<27I',data,16)
assert data[:16]==b'INTERQUAKEMODEL\0' and h[0]==2 and h[1]==len(data)
text=data[h[4]:h[4]+h[3]]
def label(offset): return text[offset:].split(b'\0',1)[0].decode()
arrays={}
for i in range(h[7]):
    kind,flags,fmt,size,offset=struct.unpack_from('<5I',data,h[9]+20*i)
    word='f' if fmt==7 else 'B'
    arrays[kind]=list(struct.iter_unpack('<'+str(size)+word,data[offset:offset+h[8]*size*(4 if fmt==7 else 1)]))
for i in range(h[5]):
    mesh=struct.unpack_from('<6I',data,h[6]+24*i)
    assert label(mesh[1]).startswith('peace/m40_') and mesh[3]<1000 and mesh[5]<2000
    for tri in range(mesh[4],mesh[4]+mesh[5]):
        ids=struct.unpack_from('<3I',data,h[11]+12*tri)
        assert all(mesh[2]<=j<mesh[2]+mesh[3] for j in ids)
        a,b,c=(arrays[0][j] for j in ids)
        u,v=[b[k]-a[k] for k in range(3)],[c[k]-a[k] for k in range(3)]
        cross=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        normal=[sum(arrays[2][j][k] for j in ids) for k in range(3)]
        assert sum(cross[k]*normal[k] for k in range(3))<0, 'Inside-out or degenerate face'
for uv in arrays[1]: assert all(0<v<1 for v in uv)
for indices,weights in zip(arrays[4],arrays[5]):
    assert all(i<h[13] for i in indices) and sum(weights)==255
clips={}
for line in (MODEL/'view.cfg').read_text().splitlines():
    fields=line.split('//',1)[0].split()
    if fields:
        name,first,count,fps,loop=fields
        clips[name]=(int(first),int(count),float(fps),int(loop))
for i in range(h[17]):
    name,first,count,fps,loop=struct.unpack_from('<3IfI',data,h[18]+20*i)
    name=label(name); expected=clips[name]
    assert (first,count,loop)==(expected[0],expected[1],expected[3]) and abs(fps-expected[2])<.0001
    duration=recipe['raise_ms']+recipe['fire_ms'] if name=='first_raise' else recipe.get(name+'_ms',1600)
    assert abs(1000*count/fps-duration)<.001, 'Gameplay/picture timing mismatch'
poses=[struct.unpack_from('<iI20f',data,h[16]+88*i) for i in range(h[15])]
assert h[13]==h[15]==6 and sum(p[1].bit_count() for p in poses)==h[20]
frames=[]; offset=h[21]
for _ in range(h[19]):
    frame=[]
    for p in poses:
        c=list(p[2:12])
        for k in range(10):
            if p[1]&(1<<k):
                c[k]+=struct.unpack_from('<H',data,offset)[0]*p[12+k];offset+=2
        assert all(math.isfinite(x) for x in c)
        assert abs(sum(x*x for x in c[3:7])-1)<.0001
        assert min(c[7:])>0
        frame.append(c)
    frames.append(frame)
assert offset==h[21]+2*h[19]*h[20]
def clip(n):
    first,count,_,_=clips[n];return frames[first:first+count]
def diff(a,b):return max(abs(x-y) for c,d in zip(a,b) for x,y in zip(c,d))
idle=clip('idle')[0]; entry=clip('sprint_in'); carry=clip('sprint_loop'); leave=clip('sprint_out')
for a,b in [(idle,entry[0]),(entry[-1],carry[0]),(carry[0],leave[0]),(leave[-1],idle),
            (clip('raise')[-1],idle),(clip('first_raise')[-1],idle),
            (clip('reload_start')[-1],clip('reload_loop')[0]),
            (clip('reload_loop')[-1],clip('reload_end')[0]),(clip('reload_end')[-1],idle)]:
    assert diff(a,b)<.001, 'Authored endpoint pop'
assert diff(entry[0],entry[-1])>1 and diff(carry[0],carry[len(carry)//4])>.05
assert diff(carry[-1],carry[0])<=max(diff(a,b) for a,b in zip(carry,carry[1:]))*1.05
assert max(abs(f[1][0]-idle[1][0]) for f in clip('fire'))>2, 'Bolt does not travel'
assert max(abs(f[2][2]-idle[2][2]) for f in clip('fire'))>2, 'Hand does not work bolt'
assert (MODEL/'materials.jpg').stat().st_size>10000
scope=(MODEL/'scope.tga').read_bytes()
assert struct.unpack_from('<HH',scope,12)==(512,512)
assert scope[18+4*(100*512+100)+3]==255 # outside ocular black
assert scope[18+4*(260*512+260)+3]==0 # off the crosshair, inside clear
world=(MODEL/'world.md3').read_bytes()
assert world[:4]==b'IDP3' and struct.unpack_from('<i',world,104)[0]==len(world)
print(f'PASS: {h[10]} rifle triangles, {h[19]} poses, timing, winding, scope mask and sprint continuity')
