#!/usr/bin/env python3
"""A scenario's items (File 2; File 1 not checked): where each one lies and what it is.

    bh_items.py GAME SCENARIO     the items of the scenario (number, room, position, kind and quantity in each variant's table)

The items' places are in the scenario's item0NN.dat (r040.afs: item040.dat): after a 24 byte head and tables, 9 word records from the head's third word + 8 (2944 in File 2's first scenario; the
count is the tbl's) with the item's number from 1: [room number byte, 0, item number halfword, x, y, z (floats), 3 words (0, a flag word, a
second flag word kept as they are), x2, 1.0 or 2.0, z2]. What each one is, is in the scenario's item0NNxx.tbl files (item04000.tbl ... item04019.tbl: one for
each of the game's variants; all twenty have the 173 records): an 8 byte head (a count 4 and 0xAD) then 12 byte records, the
item's number: a halfword (0x1C, 0x12C a herb, 0x190 a handgun ...), a quantity byte, a byte 0x64 and, in the long records (the 2084 byte tables: item04000 to 04015), two words
0xFFFF (what is inside it, as they are). The last four tables (item04016 to 04019, 700 bytes) have the same 173 records in 4 bytes: just the first four bytes. The head's second word is the
record count (173) and the stride is the table's size over it. Which table the game reads: item04016 in File 2's zoo (single play, the first character: measured by writing a different marker
into every record of every table and reading the game's item table back: only item04016's showed).
Measured in the game (File 2's zoo, the first room): an edit of item 1's x in the dat and of its kind in every tbl showed in the game's item table (0x397B40 + 0x3C * n:
+8 x, +16 z, +4 and +6 the number and the kind) as 12819 -> 13319 and 0x1C -> 0x12C. docs/research/rdt-format.md has the evidence.
"""
import struct
import sys

import bh_archive as archive

DAT_SIZE = 36
TBL_START = 8


def stride(table):
    """The size of a tbl's records: its size after the 8 byte head over the count in the head's second word (12 or 4)"""
    return (len(table) - TBL_START) // struct.unpack_from("<I", table, 4)[0]


def pack_of(game, scenario):
    disc = archive.Archive(game)
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            return disc.open(f"{dat}/r{scenario:03d}.afs")
        except FileNotFoundError:
            continue
    return None


class Item:
    def __init__(self, number, record, kinds):
        self.number = number
        self.room = record[0]
        self.position = struct.unpack_from("<3f", record, 4)
        self.words = struct.unpack_from("<5i", record, 16)
        self.kinds = kinds  # {tbl name: (kind, quantity)}


class Items:
    """The scenario's items: `items` (a list in number order from 1), `dat` the (name, member node) of the dat and `tbls` {name: member node}"""

    def __init__(self, game, scenario):
        pack = pack_of(game, scenario)
        if pack is None:
            raise FileNotFoundError(f"{game}: no r{scenario:03d}.afs")
        members = dict(pack.members())
        self.dat_name = f"item{scenario:03d}.dat"
        self.dat = members.get(self.dat_name)
        if self.dat is None:
            raise FileNotFoundError(f"{game}: r{scenario:03d}.afs has no item{scenario:03d}.dat")
        self.tbls = {n: node for n, node in members.items() if n.lower().startswith(f"item{scenario:03d}") and n.lower().endswith(".tbl")}
        data = self.dat.read()
        self.start = struct.unpack_from("<I", data, 8)[0] + 8   # the head's third word is where the item table's own head is: its records follow 8 bytes on
        tables = {name: node.read() for name, node in self.tbls.items()}
        count = max((struct.unpack_from("<I", t, 4)[0] for t in tables.values()), default=0)
        self.strides = {name: stride(table) for name, table in tables.items()}
        self.items = []
        for n in range(1, count + 1):
            at = self.start + DAT_SIZE * (n - 1)
            if at + DAT_SIZE > len(data) or struct.unpack_from("<H", data, at + 2)[0] != n:
                break
            kinds = {}
            for name, table in tables.items():
                row = TBL_START + self.strides[name] * (n - 1)
                if row + self.strides[name] <= len(table):
                    kinds[name] = (struct.unpack_from("<H", table, row)[0], table[row + 2])
            self.items.append(Item(n, data[at:at + DAT_SIZE], kinds))

    def patches(self, edits):
        """[(disc offset, bytes, 0)] for edits {item number: {"position": [x, y, z], "kind": n, "quantity": n}}: the position in the dat, the kind and quantity in every
        tbl that has the item"""
        out = []
        for number, change in edits.items():
            unknown = set(change) - {"position", "kind", "quantity"}
            if unknown:
                raise SystemExit(f"item {number}: unknown {sorted(unknown)}")
            number = int(number)
            if not 1 <= number <= len(self.items):
                raise SystemExit(f"item {number}: the scenario has {len(self.items)}")
            if "position" in change:
                at = self.dat.offset + self.start + DAT_SIZE * (number - 1) + 4
                out.append((at, struct.pack("<3f", *map(float, change["position"])), 0))
            for name, node in self.tbls.items():
                if name not in self.items[number - 1].kinds:
                    continue
                row = node.offset + TBL_START + self.strides[name] * (number - 1)
                if "kind" in change:
                    out.append((row, struct.pack("<H", int(change["kind"])), 0))
                if "quantity" in change:
                    out.append((row + 2, struct.pack("<B", int(change["quantity"])), 0))
        return out


def grown(items, new, edits=None):
    """{member name: new bytes} for the scenario's item files with `new` items added at the end ([{"room": n, "position": [x, y, z], "kind": n, "quantity": n, "like": item number}]):
    the dat's item table gets a 36 byte record for each (a copy of item `like`'s, set to the room, place and next number) and its count and sizes are rewritten, every tbl gets a row.
    Needs a rebuilt disc (tools/bh_vdisc.py): the files are bigger"""
    dat = bytearray(items.dat.read())
    head = list(struct.unpack_from("<6I", dat, 0))       # (offset, size) of three sections
    start = items.start - 8                              # the item table's own head: [3, count]
    count = struct.unpack_from("<I", dat, start + 4)[0]
    records = bytearray()
    kinds = {name: bytearray(node.read()) for name, node in items.tbls.items()}
    for number, change in (edits or {}).items():
        number = int(number)
        if "position" in change:
            struct.pack_into("<3f", dat, items.start + DAT_SIZE * (number - 1) + 4, *map(float, change["position"]))
        for name, table in kinds.items():
            if name in items.items[number - 1].kinds:
                row = TBL_START + items.strides[name] * (number - 1)
                if "kind" in change:
                    struct.pack_into("<H", table, row, int(change["kind"]))
                if "quantity" in change:
                    table[row + 2] = int(change["quantity"])
    for k, spec in enumerate(new):
        number = count + k + 1
        like = items.items[int(spec.get("like", 1)) - 1]
        rec = bytearray(dat[items.start + DAT_SIZE * (like.number - 1):items.start + DAT_SIZE * like.number])
        x, y, z = spec["position"]
        struct.pack_into("<BBH", rec, 0, int(spec["room"]), 0, number)
        struct.pack_into("<3f", rec, 4, float(x), float(y), float(z))
        struct.pack_into("<f", rec, 24, float(x) - 1.0)
        struct.pack_into("<f", rec, 32, float(z) + 1.0)
        records += rec
        for name, table in kinds.items():
            stride_ = items.strides[name]
            row = bytearray(table[TBL_START + stride_ * (like.number - 1):TBL_START + stride_ * like.number])
            struct.pack_into("<H", row, 0, int(spec["kind"]))
            row[2] = int(spec["quantity"])
            table += row
    for name, table in kinds.items():
        struct.pack_into("<I", table, 4, count + len(new))
    insert = len(records)
    at = items.start + DAT_SIZE * count
    dat[at:at] = records
    struct.pack_into("<I", dat, start + 4, count + len(new))
    # the sections: the item table (the head's second pair) grows, the one after it moves
    head[3] += insert
    head[4] += insert
    struct.pack_into("<6I", dat, 0, *head)
    out = {items.dat_name: bytes(dat)}
    out.update({name: bytes(table) for name, table in kinds.items()})
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    items = Items(argv[1], int(argv[2]))
    for item in items.items:
        first = next(iter(item.kinds.values()), None)
        print(f"item {item.number:3d} room {item.room:2d} at {tuple(round(v) for v in item.position)} kind {first[0]:#x} quantity {first[1]} ({len(item.kinds)} tables)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
