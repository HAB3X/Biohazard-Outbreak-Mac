#!/usr/bin/env python3
"""The editor's build: the edits (tools/bh_room_patch.py's EDITS.json) and the patches already laid (the English one) become one disc patch in which
members may be bigger than they were.

    bh_studio_build.py GAME EDITS.json OUT.patch [BASE.patch ...]

Edits that fit are patches in place as before; a room that grew goes through tools/bh_vdisc.py: its archive (and the archive's archive) is rebuilt, moved
to the end of the disc, and every other patch's ranges that fell in a moved file are laid again at the file's new places (a patch that is for a keyboard's or a
pad's prompts keeps its variant). Pass the base patches in the order the game gets them (the English patch first): they come out inside OUT.patch's
moved files, since the game no longer reads the old place.
"""
import json
import sys

import bh_archive as archive
import bh_patchfile as patchfile
import bh_room_patch as roompatch
import bh_vdisc as vdisc

CONTAINERS = ("/NETBIO00.DAT", "/NETBIO01.DAT", "/NETBIO02.DAT")


def container_of_pack(arc, pack):
    for path in CONTAINERS:
        try:
            node = arc.open(path)
        except FileNotFoundError:
            continue
        if any(name.lower() == pack.lower() for name, _ in node.members()):
            return path
    raise FileNotFoundError(pack)


def compose(game, edits, out_path, base_patches=()):
    """Builds OUT_PATH; returns the report lines"""
    replacements = {}
    patches, report = roompatch.build(game, edits, replacements)
    disc = vdisc.VDisc(game)
    ropes = {}
    for pack, _ in replacements:
        path = container_of_pack(disc.arc, pack)
        if path not in ropes:
            ropes[path] = disc.open(path)
    ranges = {path: (disc.arc.open(path).offset, disc.arc.open(path).size) for path in ropes}
    # every patch laid so far: the base patches first, then the edits'
    stream = []
    for base in base_patches:
        stream += patchfile.read_patch(base)
    stream += list(patches)
    in_place, plain = [], {path: [] for path in ropes}
    for offset, data, variant in stream:
        for path, (start, size) in ranges.items():
            if start <= offset < start + size:
                if variant == 0:
                    plain[path].append((offset, data))
                else:
                    disc.extra.append(("variant", path, offset, data, variant))
                break
        else:
            in_place.append((offset, data, variant))
    for path, items in plain.items():
        # later patches lie over earlier ones: apply in the order given, a range at a time, but in one pass when none overlap
        items.sort(key=lambda x: x[0])
        clean = all(a[0] + len(a[1]) <= b[0] for a, b in zip(items, items[1:]))
        if clean:
            ropes[path].patch_many(items)
        else:
            for offset, data in [x for x in stream if x[2] == 0 and ranges[path][0] <= x[0] < ranges[path][0] + ranges[path][1]]:
                ropes[path].patch_original(offset, data)
    by_pack = {}
    for (pack, member), data in replacements.items():
        path = container_of_pack(disc.arc, pack)
        rope = ropes[path]
        inner = rope.member(pack)
        inner.set_member(member, data)
        rope.set_member(pack, inner)
        by_pack.setdefault((pack, path), []).append(member)
    for (pack, path), members in by_pack.items():
        shown = ", ".join(members[:3]) + (f" and {len(members) - 3} more" if len(members) > 3 else "")
        report.append(f"{pack}: {len(members)} files rebuilt ({shown})")
    placed = {path: disc.move(path, rope) for path, rope in ropes.items()}
    # the variant ranges at their file's new places
    extra = []
    for item in disc.extra:
        _, path, offset, data, variant = item
        base = disc.moved[path][0]
        for position, original, length in ropes[path].original_positions(offset, len(data)):
            extra.append((base + position, ("data", data[original - offset:original - offset + length]), variant))
    disc.extra = extra
    entries_before = len(disc.literals)
    for offset, data, variant in in_place:
        disc.extra.append((offset, ("data", data), variant))
    disc.write(out_path)
    report.append(f"{len(ropes)} file(s) moved to the end of the disc: " + ", ".join(f"{p} at sector {s}" for p, s in placed.items()))
    return report


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    game, edits_path, out = argv[1], argv[2], argv[3]
    with open(edits_path) as f:
        edits = json.load(f)
    print("\n".join(compose(game, edits, out, argv[4:])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
