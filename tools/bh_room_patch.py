#!/usr/bin/env python3
"""Builds the room patch: edits to rooms' cameras and trigger areas, laid over the disc (a --patch, the way the character patch is).

    bh_room_patch.py GAME EDITS.json OUT.patch

EDITS.json (the room editor writes it):
    {"rooms": {"r0400300.rdt": {
        "cameras": {"2": {"eye": [x, y, z], "target": [x, y, z], "fov": [h, v], "box": [[x, y, z] x8]}},
        "areas":   {"5": {"quad": [[x, z] x4], "height": [low, high], "fields": {"13": 4}}},
        "fog":     {"2": {"ints": {"4": 4278190335}, "floats": {"9": 1.0}}},
        "new_cameras": [{"like": 0, "eye": [x, y, z], "target": [x, y, z], "fov": [h, v], "box": [[x, y, z] x8]}],
        "new_areas": [{"like": 0, "quad": [[x, z] x4], "height": [low, high], "fields": {"13": 3}}]}}}
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
import bh_rdt as rdt
from bh_patchfile import write_patch


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
