#!/usr/bin/env python3
"""Generate the original Peace G19 models, clips, materials and shared timing data."""
import argparse
import io
import json
import math
import random
import struct
import wave
from pathlib import Path

import build_m40a3 as art

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'assets/source/g19/weapon.json'
MODELS = 'assets/baseq3/models/weapons2/g19/'
GUN = (14, -3, -3.5)
THUMB = (-2.5, 1.4, -1.8)
LEVER = (-2.9, .85, .35)
NODES = [
    ('gun', -1, GUN),
    ('slide', 0, (0, 0, .8)),
    ('magazine', 0, (-1, 0, -3)),
    ('thumb', 0, THUMB),
    ('tag_flash', 0, (10, 0, .8)),
    ('selector', 0, LEVER),
]
COLORS = {
    'metal': (.19, .21, .22), 'steel': (.44, .47, .48),
    'rubber': (.065, .078, .07), 'stock': (.14, .17, .14), 'fabric': (.17, .19, .16),
}


def geometry(hands=True, fitted=False):
    m = art.Mesh(GUN)
    # A beveled slide, open muzzle, undercut polymer frame and angled grip.
    m.loft('metal', [(-3, 0, 0.8, 0.65, 0.65), (-2.6, 0, 0.8, 0.82, 0.78), (8.9, 0, 0.8, 0.82, 0.78), (9.5, 0, 0.8, 0.65, 0.65)], joint=1, sides=8)
    m.loft('rubber', [(9.5, 0, 0.8, 0.46, 0.46), (9.6, 0, 0.8, 0.46, 0.46)], sides=16)
    m.loft('steel', [(9.55, 0, 0.8, 0.48, 0.48), (9.7, 0, 0.8, 0.4, 0.4)], sides=16, caps=False)
    m.loft('stock', [(-3, 0, -0.5, 0.65, 0.5), (7.5, 0, -0.5, 0.68, 0.45)], sides=8)
    m.rod('rubber', (-1, 0, -0.8), (-2.6, 0, -5.4), 1.0, 0.85, sides=8)
    m.rod('metal', (-2.6, 0, -5.4), (-2.7, 0, -5.8), 0.9, 0.92, joint=2, sides=8)
    for x in (-2, -1.5, -1, -.5, 0):
        m.rod('rubber', (x, -0.83, 0.3), (x, -0.83, 1.15), 0.04, sides=6)
    for a, b in [((1.4, 0, -.8), (3, 0, -1.6)), ((3, 0, -1.6), (2.6, 0, -3)), ((2.6, 0, -3), (-.5, 0, -3))]:
        m.rod('metal', a, b, 0.17, sides=8)
    m.rod('steel', (1.3, 0, -1), (1.1, 0, -2.3), 0.13, sides=8)
    # An open rear notch and a narrower front post; all ride the slide.
    for x, y, r in [(-2.5, -.45, .16), (-2.5, .45, .16), (8.6, 0, .12)]:
        m.loft('rubber', [(x, y, 1.8, r, 0.24), (x + 0.35, y, 1.8, r, 0.24)], joint=1, sides=8)
    if hands:
        m.rod('fabric', (-10, -1, -13), (-3, -0.3, -3), 1.5, 1.0, sides=12)
        m.rod('rubber', (-3, -0.3, -3), (-1, -0.1, -1.3), 1.0, 0.9, sides=12)
        for z in (-1.9, -2.55, -3.2):
            m.rod('rubber', (-2, -0.95, z), (0.4, -0.7, z), 0.32, sides=10)
        # The support thumb is its own joint so the sights stay still during a flip.
        m.rod('fabric', (-8, 4, -10), (-2, 2, -3), 1.3, 0.9, sides=12)
        m.rod('rubber', (-2, 2, -3), (-1.2, 1.15, -1.6), 0.9, 0.72, sides=10)
        m.rod('rubber', THUMB, (-1.4, 1.05, -1.45), 0.36, 0.28, joint=3, sides=10)
    if fitted:
        m.rod('steel', LEVER, (-3.7, 1.03, 0.35), 0.22, 0.27, joint=5, sides=8)
    return m


def draw_pose(keys, t):
    for start, end in zip(keys, keys[1:]):
        if t <= end['t']:
            amount = art.ease((t - start['t']) / (end['t'] - start['t']))
            return [
                tuple(a + (b - a) * amount for a, b in zip(start[field], end[field]))
                for field in ('gun_offset', 'gun_angles')
            ]
    raise ValueError(f'Draw keyframes do not cover time {t}')


def pose(name, t, data, auto):
    gun_offset = gun_angles = thumb_offset = thumb_angles = (0, 0, 0)
    slide = magazine = 0
    detent = float(auto)
    if name in ('raise', 'drop'):
        gun_offset, gun_angles = draw_pose(data['draw'], 1 - t if name == 'drop' else t)
    elif name == 'fire':
        kick = math.sin(math.pi * t) ** 2
        gun_offset = (-.6 * kick, 0, .35 * kick)
        gun_angles = (0, -7 * kick, 0)
        slide = -.8 * kick
    elif name == 'reload_loop':
        tilt = math.sin(math.pi * t) ** 2
        gun_angles = (0, -22 * tilt, 13 * tilt)
        magazine = -5 * math.sin(math.pi * min(1, t / .73)) ** 2
    elif name.startswith('sprint'):
        if name == 'sprint_loop':
            carry = 1
            bob = .25 * math.sin(math.tau * t)
        else:
            carry = art.ease(t if name == 'sprint_in' else 1 - t)
            bob = 0
        gun_offset = (0, carry, -2 * carry + bob)
        gun_angles = (12 * carry, -18 * carry, 55 * carry)
    elif name in ('select_auto', 'select_semi'):
        click = data['click_ms'] / data['switch_ms']
        if t < click:
            reach = art.ease(min(1, t / (click - .1)))
        else:
            reach = 1 - art.ease((t - click) / (1 - click))
        thumb_offset = (-.38 * reach, -.25 * reach, 1.9 * reach)
        thumb_angles = (0, -23 * reach, 0)
        detent = art.ease(max(0, min(1, (t - click + .1) / .1)))
        if name == 'select_semi':
            detent = 1 - detent

    return [
        art.channels(art.add(GUN, gun_offset), gun_angles),
        art.channels((slide, 0, .8)),
        art.channels((-1, 0, -3 + magazine)),
        art.channels(art.add(THUMB, thumb_offset), thumb_angles),
        art.channels((10, 0, .8)),
        art.channels(LEVER, (0, 58 * detent, 0)),
    ]


def fire_sound():
    rng = random.Random(915)
    samples = []
    for i in range(8000):
        t = i / 22050
        noise = rng.uniform(-1, 1) * math.exp(-45 * t)
        tone = .5 * math.sin(620 * t) * math.exp(-25 * t)
        samples.append(round(32767 * max(-1, min(1, (noise + tone) * .7))))
    sound = io.BytesIO()
    with wave.open(sound, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(struct.pack(f'<{len(samples)}h', *samples))
    return sound.getvalue()


def icon_pixel(x, y):
    visible = (20 < x < 112 and 15 < y < 29) or (24 < x < 42 and 25 < y < 53)
    return (205, 216, 202, 255 if visible else 0)


def build():
    data = json.loads((ROOT / SOURCE).read_text())
    clips = {
        'idle': 1000, 'raise': data['raise_ms'], 'fire': data['fire_ms'],
        'drop': data['drop_ms'], 'reload_loop': data['reload_ms'],
        'sprint_in': data['sprint_in_ms'], 'sprint_loop': data['sprint_loop_ms'],
        'sprint_out': data['sprint_out_ms'],
        'select_auto': data['switch_ms'], 'select_semi': data['switch_ms'],
    }
    outputs = {}
    for variant in ('view', 'semi', 'auto'):
        animations = []
        for name, duration in clips.items():
            count = max(2, round(duration * 60 / 1000))
            loop = name in ('idle', 'sprint_loop')
            intervals = count if loop else count - 1
            frames = [pose(name, i / intervals, data, variant == 'auto') for i in range(count)]
            animations.append((name, frames, 1000 * count / duration, loop))
        model, config = art.iqm(geometry(fitted=variant != 'view'), animations, NODES, 'peace/g19_', SOURCE)
        outputs[MODELS + variant + '.iqm'] = model
        if variant == 'view':
            outputs[MODELS + 'view.cfg'] = config

    shader = '// Original Peace G19 materials.\n'
    for name, color in COLORS.items():
        rgb = ' '.join(str(v) for v in color)
        shader += (
            f'peace/g19_{name}\n{{\n {{ map $whiteimage\n rgbGen lightingDiffuse\n }}\n'
            f' {{ map $whiteimage\n blendFunc GL_DST_COLOR GL_ZERO\n rgbGen const ( {rgb} )\n }}\n}}\n'
        )
    defines = ('mag_size', 'reserve', 'damage', 'raise_ms', 'drop_ms', 'quick_raise_ms',
               'fire_ms', 'reload_ms', 'mag_in_ms', 'sprint_in_ms', 'sprint_out_ms',
               'click_ms', 'switch_ms')
    header = f'/* Generated from {SOURCE}. */\n'
    header += ''.join(f'#define G19_{key.upper()} {data[key]}\n' for key in defines)
    outputs.update({
        MODELS + 'world.md3': art.md3(geometry(False), 'peace/g19_', GUN, (10, 0, .8), 'Peace original G19'),
        MODELS + 'view.sights': (
            b'// Eye behind the rear notch, clear of the near plane.\n'
            b'iron 5 -3 -1.46 0 0 0\ntiming 150 150\nzoomfov 50\n'
        ),
        'assets/baseq3/scripts/peace_g19.shader': shader.encode(),
        'assets/baseq3/icons/peace_g19.tga': art.tga(128, 64, icon_pixel),
        'assets/baseq3/sound/weapons/g19/fire.wav': fire_sound(),
        'code/game/bg_g19_data.h': header.encode(),
    })
    return outputs


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, data in build().items():
        path = ROOT / name
        if args.check:
            if path.read_bytes() != data:
                raise SystemExit(f'Stale generated asset: {name}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print('PASS: G19 source and assets agree' if args.check else 'Built original G19')
