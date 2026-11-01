#!/usr/bin/env python3
"""Bake original sprint clips into the SPAS IQM using only Python's stdlib.

Geometry, materials, bind skeleton and original clip ranges are retained.
All animation channels are rebuilt from the source SMDs, so repeated bakes do
not accumulate quantization error. Sprint authoring lives in sprint.json.
The synthetic holster resamples the draw backwards with an earlier departure
from ready. No Blender installation is needed; the Blender importer invokes
this too.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/source/spas12"
OUTPUT = ROOT / "assets/baseq3/models/weapons2/spas12"
SOURCE_CLIPS = [
    ("idle", "idle.smd", True), ("raise", "draw.smd", False),
    ("fire", "Shoot.smd", False), ("reload_start", "start_reload.smd", False),
    ("reload_loop", "insert.smd", False), ("reload_end", "after_reload.smd", False),
]
SPRINT_NAMES = ("sprint_in", "sprint_loop", "sprint_out")
BAKED_NAMES = SPRINT_NAMES + ("first_raise",)


def mul(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)


def rotate(q, v):
    result = mul(mul(q, (*v, 0)), (-q[0], -q[1], -q[2], q[3]))
    return result[:3]


def unit(q):
    scale = (1 if q[3] >= 0 else -1) / math.sqrt(sum(v*v for v in q))
    return tuple(v*scale for v in q)


def euler(x, y, z):
    return unit(mul(mul((0, 0, math.sin(z/2), math.cos(z/2)),
                        (0, math.sin(y/2), 0, math.cos(y/2))),
                    (math.sin(x/2), 0, 0, math.cos(x/2))))


def skeleton(path):
    nodes, frames = [], []
    section, current = None, None
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line in ("nodes", "skeleton", "end"):
            section = None if line == "end" else line
        elif section == "nodes":
            m = re.fullmatch(r'(\d+)\s+"(.*)"\s+(-?\d+)', line)
            assert m and int(m[1]) == len(nodes)
            nodes.append((m[2], int(m[3])))
        elif section == "skeleton":
            if line.startswith("time "):
                assert int(line.split()[1]) == len(frames)
                current = list(frames[-1]) if frames else [None]*len(nodes)
                frames.append(current)
            else:
                fields = line.split()
                p = tuple(map(float, fields[1:4]))
                q = euler(*map(float, fields[4:7]))
                # Same reflected axis conversion as import_spas.py:
                # translation (-y,-x,z), quaternion (qy,qx,-qz,qw).
                current[int(fields[0])] = (-p[1], -p[0], p[2],
                                          q[1], q[0], -q[2], q[3], 1., 1., 1.)
    assert nodes and frames and all(all(c is not None for c in f) for f in frames)
    return nodes, frames


def holster_samples(count):
    """Keep the draw's reverse path, with a quicker departure from ready.

    The source draw settles gently at its end. Simply reversing those samples
    stretched that near-still tail over the beginning of the 829ms holster.
    Ease-out sampling puts more of the existing movement near the beginning,
    without changing the endpoints, number of frames or gameplay duration.
    The Blender preview and stdlib export use the same sampling rule.
    """
    assert count >= 2
    for i in range(count):
        f = i/(count-1)
        sample = f*(2-f)*(count-1)
        first = int(sample)
        yield first, min(first+1, count-1), sample-first


def holster_clip(draw):
    reverse = list(reversed(draw))
    result = []
    for first, second, fraction in holster_samples(len(reverse)):
        if fraction == 0:
            result.append(reverse[first])
            continue
        frame = []
        for a, b in zip(reverse[first], reverse[second]):
            # Shortest-path quaternion interpolation keeps the mechanical
            # joints rigid while sampling between the original authored poses.
            dot = sum(x*y for x, y in zip(a[3:7], b[3:7]))
            sign = -1 if dot < 0 else 1
            angle = math.acos(min(1, abs(dot)))
            wa, wb = 1-fraction, fraction
            if angle > .000001:
                wa = math.sin((1-fraction)*angle)/math.sin(angle)
                wb = math.sin(fraction*angle)/math.sin(angle)
            rotation = unit(tuple(a[k]*wa+b[k]*wb*sign for k in range(3, 7)))
            position = tuple(a[k]+(b[k]-a[k])*fraction for k in range(3))
            scale = tuple(a[k]+(b[k]-a[k])*fraction for k in range(7, 10))
            frame.append((*position, *rotation, *scale))
        result.append(frame)
    return result


def sprint_clips(nodes, idle, recipe):
    pivot = recipe["pivot"]
    stowed = recipe.get("stowed_offsets", {})
    assert all(any(name == key and parent >= 0 for name, parent in nodes) for key in stowed)
    pitch, yaw, roll = map(math.radians, recipe["pitch_yaw_roll"])
    carry = euler(roll, pitch, yaw)
    angle = math.acos(min(1, max(-1, carry[3])))

    def turn_forearms(frame, fraction):
        # Swing an elbow around its wrist, then compensate the wrist's local
        # transform. The hand/fingers keep exactly the same world-space grip.
        # This is authored pose data, not a runtime IK or gameplay rule.
        indices = {name: i for i, (name, _) in enumerate(nodes)}
        for adjustment in recipe.get("forearm_turns", []):
            arm, hand = (indices[adjustment[key]] for key in ("forearm", "hand"))
            assert nodes[hand][1] == arm and nodes[arm][1] >= 0
            world = []
            for (_, parent), c in zip(nodes, frame):
                if parent < 0:
                    world.append((c[:3], c[3:7]))
                else:
                    p, q = world[parent]
                    world.append((tuple(a+b for a, b in zip(p, rotate(q, c[:3]))), mul(q, c[3:7])))
            wrist, wrist_q = world[hand]
            elbow, elbow_q = world[arm]
            pitch, yaw, roll = [math.radians(v)*fraction for v in adjustment["pitch_yaw_roll"]]
            turn = euler(roll, pitch, yaw)
            elbow = tuple(a+b for a, b in zip(wrist, rotate(turn, tuple(a-b for a, b in zip(elbow, wrist)))))
            elbow_q = unit(mul(turn, elbow_q))
            for index, position, rotation, parent in (
                    (arm, elbow, elbow_q, world[nodes[arm][1]]),
                    (hand, wrist, wrist_q, (elbow, elbow_q))):
                p, q = parent
                inverse = (-q[0], -q[1], -q[2], q[3])
                local = rotate(inverse, tuple(a-b for a, b in zip(position, p)))
                frame[index] = (*local, *unit(mul(inverse, rotation)), *frame[index][7:])
        return frame

    def pose(fraction, phase=0):
        # Quaternion interpolation keeps the gun and both hands as one rig.
        f = fraction*fraction*(3-2*fraction)
        scale = math.sin(angle*f)/math.sin(angle) if angle else 0
        q = (*[v*scale for v in carry[:3]], math.cos(angle*f))
        wave = math.sin(phase)
        sway = [math.radians(v)*wave for v in recipe["sway_pitch_yaw_roll"]]
        q = unit(mul(euler(sway[2], sway[0], sway[1]), q))
        rp = rotate(q, pivot)
        offset = [pivot[k]-rp[k]+f*recipe["offset"][k]+recipe["bob"][k]*wave
                  for k in range(3)]
        result = []
        for (name, parent), c in zip(nodes, idle):
            if parent >= 0:
                # The loose reload shell and its holder normally sit behind
                # the eye. Keep them behind the gun as it turns across the
                # view; the hands and working gun joints retain their grip.
                delta = stowed.get(name, (0, 0, 0))
                result.append((*[c[k]+f*delta[k] for k in range(3)], *c[3:]))
            else:
                p = rotate(q, c[:3])
                result.append((*[p[k]+offset[k] for k in range(3)],
                               *unit(mul(q, c[3:7])), *c[7:10]))
        return turn_forearms(result, f)

    result = []
    for name, duration in zip(SPRINT_NAMES, (recipe["in_ms"], recipe["loop_ms"], recipe["out_ms"])):
        count = max(2, round(duration*recipe["fps"]/1000))
        if name == "sprint_loop":
            frames = [pose(1, 2*math.pi*i/count) for i in range(count)]
        else:
            frames = [pose(i/(count-1) if name == "sprint_in" else 1-i/(count-1))
                      for i in range(count)]
        result.append((name, frames, count*1000/duration, name == "sprint_loop"))
    return result


def build(model=None, config=None):
    model = (OUTPUT / "view.iqm").read_bytes() if model is None else model
    config = (OUTPUT / "view.cfg").read_text() if config is None else config
    h = list(struct.unpack_from("<27I", model, 16))
    assert model[:16] == b"INTERQUAKEMODEL\0" and h[0] == 2 and h[1] == len(model)
    assert h[12] == h[23] == h[24] == h[25] == h[26] == 0  # this export has no extensions
    text = model[h[4]:h[4]+h[3]]

    def label(offset):
        return text[offset:].split(b"\0", 1)[0].decode()

    joints = [struct.unpack_from("<Ii10f", model, h[14]+48*i) for i in range(h[13])]
    metadata = [struct.unpack_from("<3IfI", model, h[18]+20*i) for i in range(h[17])]
    rates = {label(a[0]): a[3] for a in metadata}
    configured = {}
    for line in config.splitlines():
        fields = line.split("//", 1)[0].split()
        if fields:
            name, first, count, fps, loop = fields
            configured[name] = (int(first), int(count), float(fps), int(loop))
    old_sprint = [a[0] for a in metadata if label(a[0]) in BAKED_NAMES]
    text = bytearray(text[:min(old_sprint)] if old_sprint else text)
    clips = []
    for name, filename, loop in SOURCE_CLIPS:
        nodes, frames = skeleton(SOURCE / filename)
        assert nodes == [(label(j[0]), j[1]) for j in joints[:-1]]
        clips.append((name, frames, rates[name], loop))
    clips.append(("drop", holster_clip(clips[1][1]), rates["drop"], False))
    clips.extend(sprint_clips(nodes, clips[0][1][0], json.loads((SOURCE / "sprint.json").read_text())))
    recipe = json.loads((SOURCE / "first_raise.json").read_text())
    deployment = []
    for segment in recipe["segments"]:
        segment_nodes, poses = skeleton(SOURCE / segment["source"])
        assert segment_nodes == nodes
        deployment.extend(poses[segment["start"]:segment.get("end")])
    clips.append(("first_raise", deployment, len(deployment)*1000/recipe["duration_ms"], False))
    assert label(joints[-1][0]) == "tag_flash" and joints[-1][1] == 0
    frames, anims, rows = [], [], []
    for name, poses, fps, loop in clips:
        if name not in BAKED_NAMES:
            # Preserve hand-tuned rates and comments in the existing table.
            # A changed source frame layout needs an explicit table review,
            # not a silent overwrite of the author's non-sprint clip ranges.
            first, count, fps, loop = configured[name]
            assert (first, count) == (len(frames), len(poses)), \
                f"Review {name} in view.cfg against view.generated.cfg before rebaking"
        assert math.isfinite(fps) and fps > 0
        encoded = name.encode()+b"\0"
        name_offset = text.find(encoded)
        if name_offset < 0:
            name_offset = len(text)
            text.extend(encoded)
        anims.append((name_offset, len(frames), len(poses), fps, int(loop)))
        rows.append(f"{name} {len(frames)} {len(poses)} {fps:.8g} {int(loop)}")
        frames.extend([list(pose)+[joints[-1][2:]] for pose in poses])

    offsets, scales, masks = [], [], []
    for bone in range(len(joints)):
        lo = [min(f[bone][k] for f in frames) for k in range(10)]
        hi = [max(f[bone][k] for f in frames) for k in range(10)]
        scale = [(b-a)/65535 if b-a > 1e-7 else 0 for a, b in zip(lo, hi)]
        offsets.append(lo)
        scales.append(scale)
        masks.append(sum(1 << k for k in range(10) if scale[k]))
    packed = bytearray()
    for frame in frames:
        for bone, c in enumerate(frame):
            for k in range(10):
                if scales[bone][k]:
                    value = round((c[k]-offsets[bone][k])/scales[bone][k])
                    packed.extend(struct.pack("<H", max(0, min(65535, value))))

    # The largest bind-local point plus the largest posed joint displacement
    # bounds every weighted vertex (all source scales are unit). This is a
    # conservative viewmodel box, including all clips and their layer blends.
    def world(frame):
        result = []
        for j, c in zip(joints, frame):
            assert all(abs(v-1) < .0001 for v in c[7:10])
            if j[1] < 0:
                result.append((c[:3], c[3:7]))
            else:
                pp, pq = result[j[1]]
                t = rotate(pq, c[:3])
                result.append((tuple(pp[k]+t[k] for k in range(3)), mul(pq, c[3:7])))
        return result

    def length(v):
        return math.sqrt(sum(x*x for x in v))

    bind_radius = max(length(p) for p, _ in world([j[2:] for j in joints]))
    posed_radius = max(length(p) for f in frames for p, _ in world(f))
    for i in range(h[7]):
        kind, flags, fmt, size, offset = struct.unpack_from("<5I", model, h[9]+20*i)
        if kind == 0:
            assert fmt == 7 and size == 3
            vertex_radius = max(length(v) for v in struct.iter_unpack("<3f", model[offset:offset+12*h[8]]))
    radius = math.ceil(bind_radius+posed_radius+vertex_radius+1)

    # Retain the export's immutable geometry/bind prefix. Do not append to an
    # earlier generated animation section: repeated runs must be byte-identical.
    data = bytearray(model[:h[14]+48*h[13]])

    def append(blob):
        data.extend(b"\0" * (-len(data) % 4))
        offset = len(data)
        data.extend(blob)
        return offset

    h[3], h[4] = len(text), append(text)
    h[15], h[16] = len(joints), append(b"".join(
        struct.pack("<iI20f", j[1], masks[i], *offsets[i], *scales[i]) for i, j in enumerate(joints)))
    h[17], h[18] = len(anims), append(b"".join(struct.pack("<3IfI", *a) for a in anims))
    h[19], h[20], h[21] = len(frames), sum(m.bit_count() for m in masks), append(packed)
    h[22] = append(struct.pack("<8f", *([-radius]*3+[radius]*3+[radius, radius]))*len(frames))
    h[1] = len(data)
    data[:124] = struct.pack("<16s27I", b"INTERQUAKEMODEL\0", *h)
    by_name = {r.split()[0]: r for r in rows}
    lines, used = [], set()
    for line in config.splitlines():
        fields = line.split("//", 1)[0].split()
        if fields and fields[0] in BAKED_NAMES:
            line = by_name[fields[0]]
            used.add(fields[0])
        lines.append(line)
    lines.extend(by_name[name] for name in BAKED_NAMES if name not in used)
    return bytes(data), "\n".join(lines)+"\n"


def write(check=False):
    model, config = build()
    for path, data in ((OUTPUT / "view.iqm", model), (OUTPUT / "view.cfg", config.encode())):
        if check:
            assert path.read_bytes() == data, f"Stale sprint export: {path}"
        else:
            path.write_bytes(data)
    print("SPAS sprint entry/carry/exit: " + ("verified" if check else "baked"))
    return model, config


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    write(parser.parse_args().check)
