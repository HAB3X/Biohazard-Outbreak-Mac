#!/usr/bin/env python3
"""A scenario's enemy placements (File 2; the format of File 1's file wasn't looked at): where each enemy stands, which room it is in and what kind it is.

    bh_enemies.py GAME SCENARIO     the enemies of the scenario (number, room, variant, position, facing, kind)

The placements are in the scenario's sgl_eNNN.emd (r040.afs: sgl_e040.emd, the one the single-player game reads; e040.emd, the other, has the same records
at other offsets and is edited with it). Its head is a table of (offset, size) pairs, one for each group of enemies (19 in sgl_e040.emd, the last
one a 4 byte end mark): each group is a room's (and variant's) enemies, a record 96 bytes: 24 words:
  w0  bytes: 1, the enemy's number in its group, its id (byte 2: the game's enemy id, 0x20 = 32 for the one standing in the zoo's room 4, which the game's record shows at +3), a class byte (room 1's first: 0x10, and the room's SpawnEnemy script op names class 16)
  w3  the facing (16 bit, 65536 to a turn)     w4, w5, w6  the position x, y, z (integers)
  w7  low byte the room number, the next byte its variant (1: a room's second variant), then 0x0F and a counter; w8 and the others kept as they are
  (the enemy's own parameters: not decoded)
Measured in the game (File 2's zoo, room 4, reached by the exit in room 3): the enemy there (kind 32, 7200 HP) stood at (10671, 0, 9710) with the facing 28236, as the record says;
an edit of the record's x in sgl_e040.emd (10671 -> 10171) moved it to 10171 (an edit of e040.emd alone changed nothing in single play).
"""
import struct
import sys

import bh_archive as archive

RECORD = 96


class Enemy:
    def __init__(self, number, group, index, record, offset):
        self.number = number
        self.group = group
        self.index = index
        self.offset = offset  # in the file
        w = struct.unpack_from("<24i", record, 0)
        self.words = w
        self.id = (w[0] >> 16) & 0xFF      # the game's enemy id (its record's +3)
        self.kind = (w[0] >> 24) & 0xFF    # the class byte: room 1's first enemy has 0x10, the SpawnEnemy script op's class there is 16
        self.facing = w[3] & 0xFFFF
        self.position = (w[4], w[5], w[6])
        self.room = w[7] & 0xFF
        self.variant = (w[7] >> 8) & 0xFF


def groups(data):
    """[(offset, size)] of the file's groups of enemies (not the 4 byte end mark)"""
    head = struct.unpack_from("<I", data, 0)[0]
    return [g for g in (struct.unpack_from("<II", data, 8 * i) for i in range(head // 8)) if g[1] >= RECORD and g[1] % RECORD == 0]


class Enemies:
    """The scenario's enemies from sgl_eNNN.emd: `enemies` (in file order, numbered from 0), `files` {name: member node} (the sgl_ file and the plain one)"""

    def __init__(self, game, scenario):
        disc = archive.Archive(game)
        pack = None
        for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
            try:
                pack = disc.open(f"{dat}/r{scenario:03d}.afs")
                break
            except FileNotFoundError:
                continue
        if pack is None:
            raise FileNotFoundError(f"{game}: no r{scenario:03d}.afs")
        members = dict(pack.members())
        self.files = {n: node for n, node in members.items() if n.lower() in (f"sgl_e{scenario:03d}.emd", f"e{scenario:03d}.emd")}
        main = self.files.get(f"sgl_e{scenario:03d}.emd")
        if main is None:
            raise FileNotFoundError(f"{game}: r{scenario:03d}.afs has no sgl_e{scenario:03d}.emd")
        self.name = main
        data = main.read()
        self.enemies = []
        for g, (offset, size) in enumerate(groups(data)):
            for k in range(size // RECORD):
                at = offset + RECORD * k
                self.enemies.append(Enemy(len(self.enemies), g, k, data[at:at + RECORD], at))

    def patches(self, edits):
        """[(disc offset, bytes, 0)] for edits {enemy number: {"position": [x, y, z], "facing": n, "id": n, "class": n}}: in the sgl_ file, and in the plain file at the record that has the
        same first words and position (when it has one)"""
        out = []
        other_name = next((n for n in self.files if not n.lower().startswith("sgl_")), None)
        other = None
        if other_name:
            data = self.files[other_name].read()
            other = [(offset + RECORD * k, data[offset + RECORD * k:offset + RECORD * (k + 1)]) for offset, size in groups(data) for k in range(size // RECORD)]
        for number, change in edits.items():
            unknown = set(change) - {"position", "facing", "id", "class"}
            if unknown:
                raise SystemExit(f"enemy {number}: unknown {sorted(unknown)}")
            number = int(number)
            if not 0 <= number < len(self.enemies):
                raise SystemExit(f"enemy {number}: the scenario has {len(self.enemies)}")
            enemy = self.enemies[number]
            targets = [(self.name.offset, enemy.offset)]
            if other_name:
                match = [at for at, rec in other if struct.unpack_from("<4i3i", rec, 0)[:7] == enemy.words[:7]]
                if len(match) == 1:
                    targets.append((self.files[other_name].offset, match[0]))
            for base, at in targets:
                if "position" in change:
                    out.append((base + at + 16, struct.pack("<3i", *map(int, change["position"])), 0))
                if "facing" in change:
                    out.append((base + at + 12, struct.pack("<i", int(change["facing"]) & 0xFFFF), 0))
                if "id" in change:
                    out.append((base + at + 2, struct.pack("<B", int(change["id"])), 0))
                if "class" in change:
                    out.append((base + at + 3, struct.pack("<B", int(change["class"])), 0))
        return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    enemies = Enemies(argv[1], int(argv[2]))
    for e in enemies.enemies:
        print(f"enemy {e.number:2d} room {e.room:2d} variant {e.variant} at {e.position} facing {e.facing} id {e.id:#x} class {e.kind:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
