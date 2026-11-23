#!/usr/bin/env python3
"""Inspect the shipped M16/Masterkey files independently of their exporter."""
from pathlib import Path
import json
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
    return (names, idle, switch), frames


rifle, rifle_frames = validate_model('view')
shotgun, _ = validate_model('masterkey')
# The mode switch flows: each mode's switch clip starts on the other mode's rest
# pose, joint for joint (the two variants name their muzzle tags differently).
for (names, idle, switch), (other_names, other_idle, _) in [(rifle, shotgun), (shotgun, rifle)]:
    for index, name in enumerate(names):
        if name.startswith('tag_'): continue
        there = other_idle[other_names.index(name)]
        assert max(abs(a-b) for a, b in zip(switch[0][index], there)) < .001, 'Mode switch pops at its start: '+name
print('PASS: each mode switch starts on the other mode\'s rest pose')
# MW2's fireType as its weapon files name it: m16_shotgun_attach_mp is Single
# Shot and the rifle, m16_shotgun_mp, a 3-Round Burst (it was Peace's
# placeholder automatic until the burst was ported). The generated header
# hands pmove each (bg_fireType_t).
recipe = json.loads((ROOT / 'assets/source/m16/weapon.json').read_text())
header = (ROOT / 'code/game/bg_m16_data.h').read_text()
assert recipe['masterkey']['fire_type'] == 'Single Shot' and recipe['fire_type'] == '3-Round Burst'
assert '#define MASTERKEY_FIRE_TYPE BG_FIRE_SINGLESHOT\n' in header
assert '#define M16_FIRE_TYPE BG_FIRE_BURST3\n' in header

# The rifle's numbers are m16_shotgun_mp's, and every one of them reaches the
# game through this file: the header's macros, the clips' lengths and the sight
# file all say what weapon.json says. (MW2's file gives seconds; these are ms.)
mw2 = {'fire_ms': 64, 'reload_loop_ms': 2029, 'mag_in_ms': 1100, 'raise_ms': 750, 'drop_ms': 449,
       'first_raise_ms': 850, 'start_ammo': 90, 'reserve': 180,	# firstRaiseTime, startAmmo, maxAmmo
       'alt_raise_ms': 730, 'alt_drop_ms': 0, 'sprint_in_ms': 300, 'sprint_out_ms': 300, 'sprint_loop_ms': 699,
       'ads_in_ms': 250, 'ads_out_ms': 250, 'ads_zoom_fov': 50, 'mag_size': 30,
       'damage': 40, 'min_damage': 30, 'max_damage_range': 1500, 'min_damage_range': 2000,
       'hip_view_kick_pitch_min': -30, 'hip_view_kick_pitch_max': 60, 'hip_view_kick_yaw_min': 45,
       'hip_view_kick_yaw_max': -45, 'hip_view_kick_center_speed': 1500,
       'ads_view_kick_pitch_min': -30, 'ads_view_kick_pitch_max': 60, 'ads_view_kick_yaw_min': 45,
       'ads_view_kick_yaw_max': -45, 'ads_view_kick_center_speed': 1500}
for key, value in mw2.items():
    assert recipe[key] == value, 'weapon.json no longer carries m16_shotgun_mp\'s '+key
macros = dict(line.split()[1:3] for line in header.splitlines() if line.startswith('#define M16_'))
for macro, key in {'FIRE_MS': 'fire_ms', 'RELOAD_MS': 'reload_loop_ms', 'MAG_IN_MS': 'mag_in_ms',
                   'MAG_OUT_MS': 'mag_out_ms', 'MAG_SIZE': 'mag_size', 'RESERVE': 'reserve',
                   'START_AMMO': 'start_ammo', 'FIRST_RAISE_MS': 'first_raise_ms',
                   'SPRINT_IN_MS': 'sprint_in_ms', 'SPRINT_OUT_MS': 'sprint_out_ms',
                   'ADS_ZOOM_FOV': 'ads_zoom_fov', 'DAMAGE': 'damage', 'MIN_DAMAGE': 'min_damage',
                   'MAX_DAMAGE_RANGE': 'max_damage_range', 'MIN_DAMAGE_RANGE': 'min_damage_range',
                   **{(aim+'_view_kick_'+part).upper(): aim+'_view_kick_'+part for aim in ('hip', 'ads')
                      for part in ('pitch_min', 'pitch_max', 'yaw_min', 'yaw_max', 'center_speed')}}.items():
    value = recipe[key]
    # a negative value is written in parentheses (MSVC pastes a bare sign into one token)
    assert macros['M16_'+macro] == (f'({value})' if value < 0 else str(value)), macro
assert all(not word.startswith('-') for word in macros.values())
clips = {}
for line in (MODEL / 'view.cfg').read_text().splitlines():
    fields = line.split('//', 1)[0].split()
    if fields: clips[fields[0]] = (int(fields[1]), int(fields[2]), float(fields[3]))
for name, key in {'fire': 'fire_ms', 'reload_loop': 'reload_loop_ms', 'raise': 'raise_ms', 'drop': 'drop_ms',
                  'sprint_in': 'sprint_in_ms', 'sprint_loop': 'sprint_loop_ms', 'sprint_out': 'sprint_out_ms',
                  'alt_raise': 'alt_raise_ms', 'first_raise': 'first_raise_ms'}.items():
    first, count, fps = clips[name]
    assert abs(count*1000/fps-recipe[key]) < .01, name+' clip is not as long as '+key
sights = dict(line.split(None, 1) for line in (MODEL / 'view.sights').read_text().splitlines()
              if line and not line.startswith('//'))
assert sights['timing'].split() == [str(recipe['ads_in_ms']), str(recipe['ads_out_ms'])]
assert float(sights['zoomfov']) == recipe['ads_zoom_fov']
# The rifle's reload is as long as MW2's and every part of it hangs on the
# keys: it leaves from and settles to the ready pose, the magazine is out of
# the well between mag_out_ms and mag_in_ms and home from mag_in_ms on (MW2's
# reloadAddTime, when the rounds go in), and the tail after it still moves:
# the support hand comes back and the gun levels, to the last frame.
rifle_names, rifle_idle, _ = rifle
first, count, fps = clips['reload_loop']
reload = rifle_frames[first:first+count]
magazine = rifle_names.index('magazine'); hand = rifle_names.index('left_hand')
def at(ms): return reload[min(count-1, round(ms*fps/1000))]
def moved(a, b, joint): return max(abs(x-y) for x, y in zip(a[joint], b[joint]))
def pops(a, b): return max(abs(x-y) for c, d in zip(a, b) for x, y in zip(c, d))
assert pops(reload[0], rifle_idle) < .001 and pops(reload[-1], rifle_idle) < .001, 'Rifle reload must leave from and settle to ready'
away = (recipe['mag_out_ms']+recipe['mag_in_ms'])/2
assert moved(at(away), rifle_idle, magazine) > 5, 'The magazine must leave the well'
assert all(moved(frame, rifle_idle, magazine) < .001 for frame in reload[math.ceil(recipe['mag_in_ms']*fps/1000):])
tail = recipe['reload_loop_ms']-recipe['mag_in_ms']
assert tail > 0 and moved(at(recipe['mag_in_ms']), rifle_idle, hand) > 5, 'The support hand is still off the gun as the magazine seats'
assert pops(at(recipe['mag_in_ms']+.75*tail), rifle_idle) > .01, 'The reload\'s tail must not stand still'
# startAmmo fills the magazine first: the reserve the rifle is given with is the rest, below maxAmmo
assert recipe['mag_size'] < recipe['start_ammo'] < recipe['mag_size']+recipe['reserve']
print('PASS: the rifle is m16_shotgun_mp: 3-Round Burst, its times, ammunition, damage, zoom and kick, in the header, the clips and the sight file')

world = (MODEL / 'world.md3').read_bytes()
assert world[:4] == b'IDP3' and struct.unpack_from('<i', world, 104)[0] == len(world)
assert (MODEL / 'materials.tga').stat().st_size > 18
assert (ROOT / 'assets/baseq3/scripts/peace_m16.shader').is_file()
