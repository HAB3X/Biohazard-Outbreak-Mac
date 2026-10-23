#!/usr/bin/env python3
"""Builds the room patch: edits to rooms' cameras and trigger areas, laid over the disc (a --patch, the way the character patch is).

    bh_room_patch.py GAME EDITS.json OUT.patch

EDITS.json (the room editor writes it):
    {"rooms": {"r0400300.rdt": {
        "cameras": {"2": {"eye": [x, y, z], "target": [x, y, z], "fov": [h, v], "box": [[x, y, z] x8]}},
        "areas":   {"5": {"quad": [[x, z] x4], "height": [low, high], "fields": {"13": 4}}},
        "fog":     {"2": {"ints": {"4": 4278190335}, "floats": {"9": 1.0}}},
        "nodes": {"3": {"position": [x, y, z], "kind": 2, "id": 5, "words": {"8": 10}}},   (the room's entity records in the .sgl: AI navigation nodes)
        "script_words": {"67": 123},   (the script's words by number, in place)
        "lights": {"0": {"position": [x, y, z], "range": 600, "attenuation": 0.001, "colour": [r, g, b], "colour2": [r, g, b]}},
        "sound_zones": {"2": {"box": [[x, y, z] x8], "sound": 8}}, "sound_points": {"0": {"position": [x, y, z]}},
        "new_cameras": [{"like": 0, "eye": [x, y, z], "target": [x, y, z], "fov": [h, v], "box": [[x, y, z] x8]}],
        "new_areas": [{"like": 0, "quad": [[x, z] x4], "height": [low, high], "fields": {"13": 3}}]}}}
    "models": {"r0400300.nbd": {"bones": {"50": {"scale": [x, y, z], "rotation": [x, y, z], "offset": [x, y, z]}}, "vertices": {"12": {"0": [x, y, z]}}}}}
        (a room model's skeleton: a bone is a number of the room's objects, the object (mesh) i standing on bone i + 2; its scale, its
        rotation in radians and its offset from its parent bone; scale of 0.0001 hides an object; vertices: the mesh's number, the
        vertex's number, its new position in the mesh's own coordinates)
        (a camera's fog record: numbers 0 to 23; new_areas add areas at the end, each a copy of area `like` with those changes,
        numbered from the room's area count on, in order: the room file grows by 180 bytes each, inside the sector it ends in)
A room is named by its file; cameras and areas by their numbers in the room (tools/bh_rdt.py lists them); every key is optional.
Only the changed bytes go into the patch, at the room's place on the disc. A room file can't change size (the archive's members
can't grow), and these edits only overwrite numbers in place, so none does. An unknown room, camera or area is an error.
"""
import json
import struct
import sys

import bh_archive as archive
import bh_nbd as nbd
import bh_rdt as rdt
from bh_patchfile import write_patch


def sgl_script_patches(game, pack, member, original_words, edits):
    """[(disc offset, bytes, 0)] writing the script word edits {index: value} into the pack's .sgl too: that copy is the one the game runs"""
    import re

    disc = archive.Archive(game)
    afs = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            afs = disc.open(f"{dat}/{pack}")
            break
        except FileNotFoundError:
            continue
    sgl_node = next((node for name, node in afs.members() if name.lower().endswith(".sgl")), None)
    if sgl_node is None:
        return [], f"{pack} has no .sgl: only the room file's copy of the script was changed"
    sgl = rdt.Sgl(sgl_node.read())
    match = re.fullmatch(r"r\d{3}(\d\d)(\d\d)\.rdt", member.lower())
    room_id = int(match.group(1)) | (int(match.group(2)) << 16)
    at = sgl.find_script(room_id, original_words)
    if at is None:
        return [], f"{member}: its script isn't in the .sgl: only the room file's copy was changed"
    return [(sgl_node.offset + at + 4 * int(i), struct.pack("<i", int(v)), 0) for i, v in edits.items()], None


def _pack_sgl(game, pack):
    disc = archive.Archive(game)
    afs = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            afs = disc.open(f"{dat}/{pack}")
            break
        except FileNotFoundError:
            continue
    return next((node for name, node in afs.members() if name.lower().endswith(".sgl")), None)


def sgl_node_patches(game, pack, member, edits):
    """[(disc offset, bytes, 0)] for edits {node number: {"position": [x, y, z], "kind": n, "id": n, "words": {word number: value}}} of the room's
    entity records (AI navigation nodes) in the pack's .sgl"""
    import re

    sgl_node = _pack_sgl(game, pack)
    if sgl_node is None:
        raise SystemExit(f"{pack} has no .sgl: its entity records can't be edited")
    sgl = rdt.Sgl(sgl_node.read())
    match = re.fullmatch(r"r\d{3}(\d\d)(\d\d)\.rdt", member.lower())
    nodes = sgl.nodes(int(match.group(1)) | (int(match.group(2)) << 16))
    out = []
    for number, change in edits.items():
        unknown = set(change) - {"position", "kind", "id", "words"}
        if unknown:
            raise SystemExit(f"{member} node {number}: unknown {sorted(unknown)}")
        if not 0 <= int(number) < len(nodes):
            raise SystemExit(f"{member}: node {number} (the room has {len(nodes)})")
        base = sgl_node.offset + nodes[int(number)].offset
        if "position" in change:
            out.append((base + 4, struct.pack("<3f", *map(float, change["position"])), 0))
        if "kind" in change:
            out.append((base + 1, struct.pack("<B", int(change["kind"])), 0))
        if "id" in change:
            out.append((base + 2, struct.pack("<H", int(change["id"])), 0))
        for word, value in change.get("words", {}).items():
            if not 0 <= int(word) < 12:
                raise SystemExit(f"{member} node {number}: word {word} is outside the record's 12 (0 to 11)")
            out.append((base + 16 + 4 * int(word), struct.pack("<i", int(value)), 0))
    return out


def size_entries(game, pack, member, size):
    """The two places the archive keeps a member's size (its table's entry and its name record), written as `size`: a member
    may be given any size inside the sectors it already has (measured: one byte less boots; a sector more doesn't)"""
    disc = archive.Archive(game)
    afs = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            afs = disc.open(f"{dat}/{pack}")
            break
        except FileNotFoundError:
            continue
    names = [name for name, _ in afs.members()]
    index = names.index(member)
    count = struct.unpack("<I", afs.read(4, 4))[0]
    name_offset = struct.unpack("<I", afs.read(8 + 8 * count, 4))[0]
    old = afs.members()[index][1].size
    if (size + 2047) // 2048 != (old + 2047) // 2048:
        raise SystemExit(f"{member}: {size} bytes need a different number of sectors than the {old} it has")
    packed = struct.pack("<I", size)
    return [(afs.offset + 8 + 8 * index + 4, packed, 0), (afs.offset + name_offset + 48 * index + 44, packed, 0)]


def model_patches(game, models):
    """([(disc offset, bytes, 0)], report lines) for the room models' skeleton edits"""
    wanted = {name.lower(): spec for name, spec in models.items()}
    patches, report = [], []
    disc = archive.Archive(game)
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            top = disc.open(dat)
        except FileNotFoundError:
            continue
        for pack, node in top.members():
            if not pack.lower().endswith(".afs") or pack == "romdata.afs":
                continue
            for member, model in node.members():
                spec = wanted.pop(member.lower(), None)
                if spec is None:
                    continue
                data = bytearray(model.read())
                table = nbd.chunks(bytes(data))
                ahi = next((c for c in table if c[0] == "AHI"), None)
                if ahi is None:
                    raise SystemExit(f"{member}: no skeleton")
                records = nbd.bone_records(bytes(data), ahi[1], ahi[2])
                original = bytes(data)
                for number, change in spec.get("bones", {}).items():
                    unknown = set(change) - {"scale", "rotation", "offset"}
                    if unknown:
                        raise SystemExit(f"{member} bone {number}: unknown {sorted(unknown)}")
                    if not 0 <= int(number) < len(records):
                        raise SystemExit(f"{member}: bone {number} (the model has {len(records)})")
                    nbd.set_bone(data, records[int(number)], **change)
                if spec.get("vertices"):
                    amo = next((c for c in table if c[0] == "AMO"), None)
                    if amo is None:
                        raise SystemExit(f"{member}: no meshes")
                    found = nbd.meshes(bytes(data), amo[1], amo[2])
                    for mesh_number, moved in spec["vertices"].items():
                        if not 0 <= int(mesh_number) < len(found):
                            raise SystemExit(f"{member}: mesh {mesh_number} (the model has {len(found)})")
                        for vertex, position in moved.items():
                            if len(position) != 3:
                                raise SystemExit(f"{member} mesh {mesh_number} vertex {vertex}: a position has three numbers")
                            nbd.set_vertex(data, found[int(mesh_number)], int(vertex), position)
                runs, start = [], None
                for i, (a, b) in enumerate(zip(original, data)):
                    if a != b and start is None:
                        start = i
                    elif a == b and start is not None:
                        runs.append((start, bytes(data[start:i])))
                        start = None
                if start is not None:
                    runs.append((start, bytes(data[start:])))
                patches += [(model.offset + o, b, 0) for o, b in runs]
                report.append(f"{pack}/{member}: {sum(len(b) for _, b in runs)} bytes in {len(runs)} runs")
    if wanted:
        raise SystemExit(f"models not on this disc: {', '.join(sorted(wanted))}")
    return patches, report


def build(game, edits):
    """([(disc offset, bytes, 0)], a line per room changed) for the edits of a loaded EDITS.json"""
    wanted = {name.lower(): spec for name, spec in edits.get("rooms", {}).items()}
    patches, report = [], []
    for pack, member, node in rdt.rooms(game):
        spec = wanted.pop(member.lower(), None)
        if spec is None:
            continue
        room = rdt.Room(node.read())
        for new in spec.get("new_areas", []):
            room.add_area(new["like"], quad=new.get("quad"), height=new.get("height"),
                          fields={int(k): v for k, v in new.get("fields", {}).items()} or None)
        if spec.get("nodes"):
            more = sgl_node_patches(game, pack, member, spec["nodes"])
            patches += more
            report.append(f"{pack}/{member}: {len(spec['nodes'])} entity records in the .sgl")
        script_words = room.script_words()
        for index, value in spec.get("script_words", {}).items():
            room.set_script_word(int(index), value)
        if spec.get("script_words"):
            more, warning = sgl_script_patches(game, pack, member, script_words, spec["script_words"])
            patches += more
            report.append(warning or f"{pack}/{member}: {len(more)} script words in the .sgl")
        for number, change in spec.get("lights", {}).items():
            unknown = set(change) - {"position", "attenuation", "range", "colour", "colour2"}
            if unknown:
                raise SystemExit(f"{member} light {number}: unknown {sorted(unknown)}")
            room.set_light(int(number), **change)
        for number, change in spec.get("sound_zones", {}).items():
            unknown = set(change) - {"box", "sound"}
            if unknown:
                raise SystemExit(f"{member} sound zone {number}: unknown {sorted(unknown)}")
            room.set_sound_zone(int(number), **change)
        for number, change in spec.get("sound_points", {}).items():
            unknown = set(change) - {"position", "kind"}
            if unknown:
                raise SystemExit(f"{member} sound point {number}: unknown {sorted(unknown)}")
            room.set_sound_point(int(number), **change)
        for new in spec.get("new_cameras", []):
            room.add_camera(new["like"], **{k: new[k] for k in ("eye", "target", "fov", "box") if k in new})
        for number, change in spec.get("cameras", {}).items():
            unknown = set(change) - {"eye", "target", "fov", "box"}
            if unknown:
                raise SystemExit(f"{member} camera {number}: unknown {sorted(unknown)}")
            room.set_camera(int(number), **change)
        for number, change in spec.get("areas", {}).items():
            unknown = set(change) - {"quad", "height", "fields"}
            if unknown:
                raise SystemExit(f"{member} area {number}: unknown {sorted(unknown)}")
            room.set_area(int(number), quad=change.get("quad"), height=change.get("height"),
                          fields={int(k): v for k, v in change.get("fields", {}).items()} or None)
        for number, change in spec.get("fog", {}).items():
            unknown = set(change) - {"ints", "floats"}
            if unknown:
                raise SystemExit(f"{member} fog {number}: unknown {sorted(unknown)}")
            room.set_fog(int(number), ints={int(k): v for k, v in change.get("ints", {}).items()},
                         floats={int(k): v for k, v in change.get("floats", {}).items()})
        runs = room.changes()
        patches += [(node.offset + offset, data, 0) for offset, data in runs]
        if len(room.data) != len(room.original):
            patches += size_entries(game, pack, member, len(room.data))
        report.append(f"{pack}/{member}: {sum(len(d) for _, d in runs)} bytes in {len(runs)} runs")
    if wanted:
        raise SystemExit(f"not on this disc: {', '.join(sorted(wanted))}")
    if edits.get("models"):
        more, lines = model_patches(game, edits["models"])
        patches += more
        report += lines
    return patches, report


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 1
    with open(argv[2]) as f:
        patches, report = build(argv[1], json.load(f))
    write_patch(argv[3], patches)
    print("\n".join(report) or "no changes")
    print(f"{len(patches)} ranges -> {argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
