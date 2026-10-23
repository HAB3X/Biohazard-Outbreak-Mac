#!/usr/bin/env python3
"""Builds the room patch: edits to rooms' cameras and trigger areas, laid over the disc (a --patch, the way the character patch is).

    bh_room_patch.py GAME EDITS.json OUT.patch

EDITS.json (the room editor writes it):
    {"rooms": {"r0400300.rdt": {
        "cameras": {"2": {"eye": [x, y, z], "target": [x, y, z], "fov": [h, v], "box": [[x, y, z] x8]}},
        "areas":   {"5": {"quad": [[x, z] x4], "height": [low, high], "fields": {"13": 4}}}}}}
A room is named by its file; cameras and areas by their numbers in the room (tools/bh_rdt.py lists them); every key is optional.
Only the changed bytes go into the patch, at the room's place on the disc. A room file can't change size (the archive's members
can't grow), and these edits only overwrite numbers in place, so none does. An unknown room, camera or area is an error.
"""
import json
import sys

import bh_rdt as rdt
from bh_patchfile import write_patch


def build(game, edits):
    """([(disc offset, bytes, 0)], a line per room changed) for the edits of a loaded EDITS.json"""
    wanted = {name.lower(): spec for name, spec in edits.get("rooms", {}).items()}
    patches, report = [], []
    for pack, member, node in rdt.rooms(game):
        spec = wanted.pop(member.lower(), None)
        if spec is None:
            continue
        room = rdt.Room(node.read())
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
        runs = room.changes()
        if len(room.data) != len(room.original):
            raise SystemExit(f"{member}: the edit changed the file's size")
        patches += [(node.offset + offset, data, 0) for offset, data in runs]
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
