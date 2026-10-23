#!/usr/bin/env python3
"""A scenario's enemy placements (File 2; the format of File 1's file wasn't looked at): where each enemy stands, which room it is in and what kind it is.

    bh_enemies.py GAME SCENARIO     the enemies of the scenario (number, room, variant, position, facing, kind)

The placements are in the scenario's sgl_eNNN.emd (r040.afs: sgl_e040.emd, the one the single-player game reads; e040.emd, the other, has the same records
at other offsets and is edited with it). Its head is a table of (offset, size) pairs, one for each group of enemies (19 in sgl_e040.emd, the last
one a 4 byte end mark): each group is a room's (and variant's) enemies, a record 96 bytes: 24 words:
  w0  bytes: 1, the enemy's number in its group, its id (byte 2: the game's enemy id, 0x20 = 32 for the one standing in the zoo's room 4, which the game's record shows at +3), a class byte (room 1's first: 0x10, and the room's SpawnEnemy script op names class 16)
  w3  the facing (16 bit, 65536 to a turn)     w4, w5, w6  the position x, y, z (integers)
  w7  low byte the room number, the next byte its variant (1: a room's second variant), then a mask of the four modes it is placed in (0x0F: all; a bit cleared took the enemy out of the mode the
      zoo was played in: the record's runtime enemy was not created), then a byte read at runtime +0xBBF
  w1  flags that change the runtime record's +0xBB4 (the AI control flag) and +0xC49;  w2  a non-zero value turned the room-4 boss (id 32, 7200 HP) into a plain zombie (id 1, 450 HP)
  w12 byte 3  the hit points' tier: File 1's zombie (id 1, copy 1 of the bar) read 600, 650, 700, 750, 780, 800, 820, 850 for the tiers 0 to 7 and 32767 for 8, 9 and 12 (measured by writing the byte);
              File 2's boss (id 32) read 5800 for tiers 0 and 2, 7200 for 4 and 6 (its own table; tier 7 is the record's), 32767 for 8; the other bytes of w12 are among the AI block's (w10..w16)
  w10..w16  one parameter each of the AI block (runtime +0xBB9 .. +0xCAE); w17  runtime +0xB4 .. +0xBE; w8 and w18: changing either stopped the enemy being created at all; w9 and w19 to w23: no effect found
  (measured by changing each word's bytes in the zoo's room-4 record and comparing the game's whole enemy record against an unchanged run: docs/research/rdt-format.md)
File 1's scenarios repeat a room's group for each difficulty (4 copies in the first), so `mode` counts the copies before a group's: the bar of File 1's first scenario (normal) read the
second (mode 1): an edit of enemy 18 (the second copy's) moved the enemy standing at (8368, 0, 13722) to x 8568, and the same record's other copies (8, 28, 38) did not. Measured in the game (File 2's zoo, room 4, reached by the exit in room 3): the enemy there (kind 32, 7200 HP) stood at (10671, 0, 9710) with the facing 28236, as the record says;
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
        self.hp_tier = (w[12] >> 24) & 0xFF   # an index into the kind's table of hit points (File 1's zombie: 600, 650, 700, 750, 780, 800, 820, 850; File 2's room-4 boss, id 32: 5800 for 0 and 2, 7200 for 4 and 6; 8 and up: 32767)
        self.modes = (w[7] >> 16) & 0xFF   # the modes the enemy is placed in (a bit for each of the four: 0x0F in all of File 2's)
        self.mode = 0  # which copy of the room's enemies this is (File 1 keeps one for each difficulty), set by Enemies


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
        seen = {}
        for g, (offset, size) in enumerate(groups(data)):
            rooms_here = set()
            for k in range(size // RECORD):
                at = offset + RECORD * k
                enemy = Enemy(len(self.enemies), g, k, data[at:at + RECORD], at)
                rooms_here.add(enemy.room)
                self.enemies.append(enemy)
            # File 1 repeats a room's group once for each difficulty (4 in its first scenario): a group's mode is how many groups of that room came before it
            for room in rooms_here:
                for enemy in self.enemies:
                    if enemy.group == g and enemy.room == room:
                        enemy.mode = seen.get(room, 0)
                seen[room] = seen.get(room, 0) + 1

    def patches(self, edits):
        """[(disc offset, bytes, 0)] for edits {enemy number: {"position": [x, y, z], "facing": n, "id": n, "class": n, "modes": mask, "hp_tier": n}}: in the sgl_ file, and in the plain file at the record that has the
        same first words and position (when it has one)"""
        out = []
        other_name = next((n for n in self.files if not n.lower().startswith("sgl_")), None)
        other = None
        if other_name:
            data = self.files[other_name].read()
            other = [(offset + RECORD * k, data[offset + RECORD * k:offset + RECORD * (k + 1)]) for offset, size in groups(data) for k in range(size // RECORD)]
        for number, change in edits.items():
            unknown = set(change) - {"position", "facing", "id", "class", "modes", "hp_tier"}
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
                if "hp_tier" in change:
                    out.append((base + at + 51, struct.pack("<B", int(change["hp_tier"])), 0))
                if "modes" in change:
                    out.append((base + at + 30, struct.pack("<B", int(change["modes"])), 0))
                if "class" in change:
                    out.append((base + at + 3, struct.pack("<B", int(change["class"])), 0))
        return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    enemies = Enemies(argv[1], int(argv[2]))
    for e in enemies.enemies:
        print(f"enemy {e.number:2d} room {e.room:2d} variant {e.variant} mode {e.mode} at {e.position} facing {e.facing} id {e.id:#x} class {e.kind:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
