#!/usr/bin/env python3
"""Builds the character patch: the costume models the launcher's character mods replace, laid over the disc (a second --patch).

    bh_character_patch.py GAME CHARACTERS_TXT OUT.patch

CHARACTERS_TXT (the launcher writes it into the game's settings folder) has a line per costume replaced: romdata.afs's member
("P06_00.nbd"), a tab, the model file; a later line for the same costume wins. Each model is written over the costume's member
of NETBIO00.DAT's romdata.afs. The archive's table is left as it is: the game reads the member's original size (the old
model's tail after the new one is never used), and a table whose size was changed crashes File 2 two seconds into its boot
(f_001d8e00 on a pointer of 0x1B bytes; seen with Kevin's P00_00 given a smaller model's size; why is open). So a model
bigger than the costume it replaces can't be used: it's left out, said, and listed in OUT.skipped (a line each: the member, a
tab, the model, a tab, its size, a tab, the costume's), which the launcher shows on the mod. An empty list makes an empty
patch.
"""
import sys
from pathlib import Path

import bh_archive as archive
from bh_patchfile import write_patch


def read_list(path):
    """{member: model path} (the last line for a member wins)"""
    chosen = {}
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        member, sep, model = line.partition("\t")
        if not sep:
            raise SystemExit(f"{path}:{n}: no tab between the costume and the model")
        chosen[member] = Path(model)
    return chosen


def build(game, chosen):
    """(patches, skipped): the ranges, and [(member, model, its size, the costume's size)] too big to use"""
    members = {name.lower(): node for name, node in archive.Archive(game).open("/NETBIO00.DAT/romdata.afs").members()}
    patches, skipped = [], []
    for member, model in sorted(chosen.items()):
        node = members.get(member.lower())
        if node is None:
            raise SystemExit(f"{member}: not a member of romdata.afs on this disc")
        data = model.read_bytes()
        if len(data) > node.size:
            print(f"{member}: {model.name} left out: {len(data)} bytes, bigger than the costume's {node.size}")
            skipped.append((member, model, len(data), node.size))
            continue
        patches.append((node.offset, data, 0))
        print(f"{member}: {model.name}, {len(data)} bytes")
    return patches, skipped


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 1
    game, listing, out = argv[1:]
    patches, skipped = build(game, read_list(listing))
    write_patch(out, patches)
    Path(out + ".skipped").write_text("".join(f"{m}\t{p}\t{size}\t{room}\n" for m, p, size, room in skipped))
    print(f"{len(patches)} costumes -> {out}" + (f"; {len(skipped)} too big" if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
