"""Extract private RM_0000 fixtures and write an independent 32-bit reference.

The output is generated from the user's ISO and belongs in the ignored build
directory. No files from the ISO are part of the Android build.
"""

import argparse
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "port/pc/tools"))
import rdx


def model_reference(data):
    _, status, top, count, tex, nodes = struct.unpack_from("<3sbHHii", data)
    table, textures = struct.unpack_from("<II", data, top + tex)
    lines = [f"MDL {status & 0xffffffff} {count} {textures}"]
    chunks = {}
    for i in range(count):
        flags, model, *transform, child, sibling = struct.unpack_from(
            "<Ii9Iii", data, top + nodes + i * 52
        )
        links = [-1 if x == -1 else (x - nodes) // 52 for x in (child, sibling)]
        chunk = -1 if model == -1 else chunks.setdefault(model, len(chunks))
        lines.append(" ".join(map(str, ["NODE", i, flags, chunk, *links, *transform])))
    for offset, index in chunks.items():
        v, p, x, y, z, radius = struct.unpack_from("<iiIIII", data, top + offset)
        lines.append(f"CHUNK {index} {v} {p} {x} {y} {z} {radius & 0x7fffffff}")
    for i in range(textures):
        name, attrs, texture = struct.unpack_from("<III", data, top + table + 12 * i)
        label = data[top + name:data.index(0, top + name)].hex()
        lines.append(f"TEX {i} {name} {attrs} {texture} {label}")
    return lines


def motion_reference(data):
    _, status, top, count, motion, attrs = struct.unpack_from("<3sbHHii", data)
    table, frames, kind, interp = struct.unpack_from("<IIHH", data, top + motion)
    assert (kind, interp) == (3, 2)
    lines = [f"MTN {status & 0xffffffff} {count} {frames} {attrs}"]
    for i in range(count):
        p, a = struct.unpack_from("<ii", data, top + table + i * 8)
        lines.append(f"TRACK {i} {p} {a}")
        for frame in (0, frames // 2, frames - 1):
            position = (0, 0, 0) if p == -1 else struct.unpack_from(
                "<III", data, top + p + frame * 12
            )
            angle = (0, 0, 0) if a == -1 else struct.unpack_from(
                "<hhh", data, top + a + frame * 6
            )
            mask = int(p != -1) + 2 * int(a != -1)
            lines.append(" ".join(map(str, ["KEY", i, frame, mask, *position, *angle])))
    return lines


def room_bank_reference(room, cursor):
    slots = []
    while (size := rdx.u32(room.d, cursor)) != 0xffffffff:
        cursor += 4
        if size:
            top, nodes, motion = struct.unpack_from("<HHi", room.d, cursor + 4)
            frames, = struct.unpack_from("<I", room.d, cursor + top + motion + 4)
        else:
            nodes, frames = 0, 0
        slots.append(f"{nodes} {frames}")
        cursor += size
    return "\n".join([str(len(slots)), *slots]) + "\n"

def room_matrix_reference(room):
    """Direct RDT hierarchy traversal with Ninja's Z/Y/X column rotations."""
    model = rdx.model_objects(room)
    nodes = model["objects"]
    result = [None] * len(nodes)
    base = model["obj_off"]

    def sincos(angle):
        idx = angle & 0x3fff
        mirror = 0x3fff - idx
        sign = -1 if angle & 0x8000 else 1
        if angle & 0x4000:
            return sign * math.sin(mirror * 0.0000958738), -sign * math.sin(idx * 0.0000958738)
        return sign * math.sin(idx * 0.0000958738), sign * math.sin(mirror * 0.0000958738)

    def rotate(matrix, angle, a, b):
        s, c = sincos(angle)
        first, second = matrix[a], matrix[b]
        matrix[a] = [x * c + y * s for x, y in zip(first, second)]
        matrix[b] = [y * c - x * s for x, y in zip(first, second)]

    def visit(offset, parent):
        if offset == -1:
            return
        index = (offset - base) // 52
        node = nodes[index]
        world = [list(column) for column in parent]
        world[3][:3] = [sum(parent[c][r] * node["pos"][c] for c in range(3)) + parent[3][r]
                        for r in range(3)]
        for angle, (a, b) in zip(reversed(node["ang"]), ((0, 1), (2, 0), (1, 2))):
            rotate(world, angle, a, b)
        result[index] = " ".join(f"{x:.12g}" for column in world for x in column)
        visit(node["child"], world)
        visit(node["sibling"], parent)

    identity = [[float(r == c) for r in range(4)] for c in range(4)]
    visit(base, identity)
    cameras = []
    for cut in room.cuts():
        authored = cut["cam"][0]
        transform = [list(column) for column in identity]
        for key, a, b in (("ay", 2, 0), ("ax", 1, 2), ("az", 0, 1)):
            rotate(transform, authored[key], a, b)
        direction = [-n for n in transform[2][:3]]
        eye = [authored[axis] + n * authored["ln"] for axis, n in zip(("px", "py", "pz"), direction)]
        # bhMakeCameraVector reverses the screen X and Y components.
        facing = [-direction[0], -direction[1], direction[2]]
        hidden = sum(
            node["model"] != -1 and bool(authored["hidobj"][i // 32] & (0x80000000 >> (i % 32)))
            for i, node in enumerate(nodes)
        )
        cameras.append(" ".join([str(hidden), *(f"{x:.12g}" for x in eye + facing)]))
    return "\n".join([str(len(nodes)), *result, str(len(cameras)), *cameras]) + "\n"


def fnv32(*parts):
    h = 2166136261
    for part in parts:
        for byte in part:
            h = ((h ^ byte) * 16777619) & 0xffffffff
    return h


def skin_reference(data, top, hierarchy, node_count, skin):
    """Replay the translation passes of npSkinConvert on disk offsets."""
    flags = [rdx.u32(data, top + hierarchy + 52 * i) for i in range(node_count)]
    words = iter(struct.unpack(f"<{len(skin) // 4}I", skin))
    joints, groups = [], {}

    def single(value):
        return struct.unpack("<f", struct.pack("<f", value))[0]

    def visit(offset, parent):
        if offset == -1:
            return
        pivot = parent
        index = (offset - hierarchy) // 52
        node = top + offset
        if not flags[index] & 0x20000000:
            shift = struct.unpack_from("<3f", data, node + 8)
            pivot = tuple(single(parent[k] + shift[k]) for k in range(3))
            op = next(words)
            group = next(words) if op else 0
            joints.append((op, group, pivot))
            if op & 2:
                weights = [next(words) for _ in range(next(words))]
                mesh = top + rdx.u32(data, node + 4)
                stream = top + rdx.u32(data, mesh)
                count, = struct.unpack_from("<H", data, stream + 6)
                vertices = bytearray(data[stream:stream + 64 + 32 * count])
                groups[group] = (index, pivot, weights, count, vertices)
                flags[index] |= 0x80000000
        child, sibling = struct.unpack_from("<ii", data, node + 44)
        visit(child, pivot)
        visit(sibling, parent)

    visit(hierarchy, (0., 0., 0.))
    for index, (op, group, pivot) in enumerate(joints):
        if not (op & 1) or index >= 128:
            continue
        _, origin, weights, count, vertices = groups[group]
        for vertex in range(count):
            if weights[vertex] >> 24 != index:
                continue
            offset = 64 + 32 * vertex
            xyz = struct.unpack_from("<3f", vertices, offset)
            values = (single(single(x + start) - end) for x, start, end in zip(xyz, origin, pivot))
            struct.pack_into("<3f", vertices, offset, *values)
    result = [fnv32(struct.pack(f"<{node_count}I", *flags)), len(groups)]
    for node, _, _, count, vertices in groups.values():
        result.extend((node, count, fnv32(vertices)))
    return " " + " ".join(map(str, result))


def actor_package_reference(room):
    data = room.d
    models = iter(room.model_table()[1:])
    motions = room.motion_table()
    lines = []

    def blocks(cursor):
        while (size := rdx.u32(data, cursor)) != 0xffffffff:
            cursor += 4
            yield cursor, size
            cursor += size

    def model(offset, skin):
        top, nodes, tex, hierarchy = struct.unpack_from("<HHii", data, offset + 4)
        textures = rdx.u32(data, offset + top + tex + 4) if tex != -1 else 0
        summary = f"{offset} {nodes if hierarchy != -1 else 0} {int(tex != -1)} {textures} {len(skin)} {fnv32(skin)}"
        return summary + (skin_reference(data, offset + top, hierarchy, nodes, skin) if skin else "")

    def motion(offset, size):
        if not size:
            return "0 0 0 0"
        top, nodes, header = struct.unpack_from("<HHi", data, offset + 4)
        top += offset
        tracks, frames = struct.unpack_from("<II", data, top + header)
        keys = []
        for node in range(nodes):
            position, angle = struct.unpack_from("<ii", data, top + tracks + node * 8)
            for track, stride in ((position, 12), (angle, 6)):
                if track != -1:
                    last = top + track + (frames - 1) * stride
                    keys.append(data[last:last + stride])
        return f"{offset} {nodes} {frames} {fnv32(*keys)}"

    for index, enemy in enumerate(room.records("enemies")):
        parts, mask, skin = [], b"", b""
        if enemy["flg"] & 1:
            for offset, size in blocks(next(models)):
                tag = data[offset:offset + 4]
                if not size:
                    continue
                if tag == b"MASK":
                    mask = data[offset:offset + size]
                elif tag == b"SKIN":
                    skin = data[offset + 4:offset + size]
                else:
                    parts.append(model(offset, skin))
                    skin = b""
        lines.append(f"{len(parts)} {len(mask)} {fnv32(mask)}")
        lines.extend(parts)
        if motions[index]:
            slots = [motion(offset, size) for offset, size in blocks(motions[index])]
            lines.append(str(len(slots)))
            lines.extend(slots)
        else:
            lines.append("-1")
    for _ in range(4, room.cnt["obj"]):
        start = next(models)
        skin = b""
        if data[start:start + 3] != b"MDL":
            length = rdx.u32(data, start)
            skin = data[start + 8:start + 8 + length]
            start += length + 8
        lines.append(model(start, skin))
    for _ in range(room.cnt["itm"]):
        lines.append(model(next(models), b""))
    return lines


def room_header_reference(room):
    rom = room.d[room.rom_ofs:room.rom_ofs + 1004]
    # bhSetRoom resets grand[31] and replaces the embedded model work.
    scalar = fnv32(rom[64:0x19c], bytes(4), rom[0x1b8:])
    lines = [str(scalar)]
    pointers = list(room.rom["ptr"])
    pointers[12] = room.hdr["hed"][3]
    lengths = [0] * len(pointers)
    for key, table, count, record in rdx.SECTIONS:
        lengths[table] = room.rom["cnt"][count] * record[0].size
    lengths[0] = room.cnt["cut"] * rdx.CUT_SIZE
    lengths[11] = room.cnt["rut"] ** 2
    if pointers[12]:
        end = min((o for o in room.hdr["hed"] if o > pointers[12]), default=len(room.d))
        lengths[12] = end - pointers[12]
    lengths[13] = room.cnt["evc"] * (8 + 16 * rdx.KEYF[0].size)
    lengths[14] = (room.cnt["mes"] + 1) * 4 if room.cnt["mes"] else 0
    lines.extend(f"{p} {n}" for p, n in zip(pointers, lengths))
    for index in range(room.cnt["cut"]):
        start = pointers[0] + index * rdx.CUT_SIZE
        cut = room.d[start:start + rdx.CUT_SIZE]
        areas, = struct.unpack_from("<I", cut, 4)
        size = cut[3] * rdx.CTAB[0].size
        lines.append(str(fnv32(cut[:4], cut[8:], room.d[areas:areas + size])))
    for table, count in ((1, room.cnt["lgt"]), (15, room.cnt["evl"])):
        for index in range(count):
            start = pointers[table] + index * rdx.LGT[0].size
            values = room.d[start:start + 0xd4]
            lines.append(str(fnv32(values)))
    for index in range(room.cnt["evc"]):
        start = pointers[13] + index * (8 + 16 * rdx.KEYF[0].size)
        keys = [room.d[k:k + 0x7c] for k in
                range(start + 8, start + 8 + 16 * rdx.KEYF[0].size, rdx.KEYF[0].size)]
        lines.append(str(fnv32(room.d[start:start + 8], *keys)))
    _, script_offsets = room.script_header() if pointers[12] else (0, [])
    lines.append(str(len(script_offsets)))
    lines.extend(map(str, script_offsets))
    lines.extend(actor_package_reference(room))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parent / "build/fixture")
    args = parser.parse_args()
    iso = rdx.Iso(args.iso)
    iso.find("SLUS_201.84")
    archive = rdx.Afs(iso, "RDX_LNK.AFS")
    room = rdx.Room(rdx.expand(archive.get(0)), "RM_0000")
    models = room.model_table()
    model = bytes(room.d[models[0]:min(p for p in models if p > models[0])])
    cursor = room.motion_table()[room.cnt["ene"]]
    while rdx.u32(room.d, cursor) == 0:
        cursor += 4
    size = rdx.u32(room.d, cursor)
    assert 16 <= size <= len(room.d) - cursor - 4
    motion = bytes(room.d[cursor + 4:cursor + 4 + size])
    assert model.startswith(b"MDL") and motion.startswith(b"MTN")
    model_lines, motion_lines = model_reference(model), motion_reference(motion)
    args.output.mkdir(exist_ok=True, parents=True)
    (args.output / "opening.mdl").write_bytes(model)
    (args.output / "opening.mtn").write_bytes(motion)
    (args.output / "opening.expected").write_text(
        "\n".join(model_lines + motion_lines) + "\n", encoding="ascii"
    )
    (args.output / "opening.rdt").write_bytes(room.d)
    (args.output / "common-effects.bin").write_bytes(rdx.Afs(iso, "SYSTEM.AFS").get(1))
    (args.output / "opening.bank").write_text(
        room_bank_reference(room, room.motion_table()[room.cnt["ene"]]) + room_matrix_reference(room),
        encoding="ascii"
    )
    (args.output / "opening.rom").write_text(room_header_reference(room), encoding="ascii")
    # RM_0140 exercises route adjacency and cuts with several areas each.
    routes = rdx.Room(rdx.expand(archive.get(16)), "RM_0140")
    (args.output / "routes.rdt").write_bytes(routes.d)
    (args.output / "routes.rom").write_text(room_header_reference(routes), encoding="ascii")
    # RM_0130 has nonzero PS2 addresses in the light runtime slots.
    lights = rdx.Room(rdx.expand(archive.get(15)), "RM_0130")
    (args.output / "lights.rdt").write_bytes(lights.d)
    (args.output / "lights.rom").write_text(room_header_reference(lights), encoding="ascii")
    from random_reference import generate
    generate(iso, args.output / "random.expected")
    print(model_lines[0], motion_lines[0], sep="\n")


if __name__ == "__main__":
    main()
