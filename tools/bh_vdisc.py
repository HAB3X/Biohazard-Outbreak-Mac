#!/usr/bin/env python3
"""A rebuilt disc: files and archive members of any size, written as a patch (DPAT version 3) the runtime lays over your disc image.

The games find their data by sector counts, so a patch of the old kind can only change bytes in place (tools/bh_character_patch.py: a member
can't grow). Here a file is a *rope*: a list of pieces, each either bytes of the original image (copied by the runtime, nothing written
into the patch) or new bytes. Pieces can be cut, replaced and spliced, so a member of an archive inside an archive inside the disc can be replaced
by a bigger one: the archives' tables are rewritten, the pieces after it are the same original bytes moved on, and the patch holds only
what's new.

    disc = VDisc("file2")
    big = disc.open("/NETBIO00.DAT")                 # a rope of the whole file, in the image's own place (not yet moved)
    pack = big.member("r040.afs")                    # a rope for an archive's member, cut out of it
    pack.set_member("r0400300.rdt", new_bytes)       # the member (found by name) now has these bytes, of any size; tables updated
    big.set_member("r040.afs", pack)                 # and the outer archive takes the new pack
    disc.move("/NETBIO00.DAT", big)                  # the file goes to the end of the disc with its directory entry pointing there
    disc.write("work/studio.patch")

The runtime (ps2-native-runtime/runtime/src/disc.cpp: version 3 patches) reads a copied range from the image as it is, and a sector past the
image's end as zeroes, so the moved file's sectors come from the pieces. The ISO directory entry (its sector and size, both byte orders) is
what the games' file lookup reads, and the runtime's directory reads go through the patch too.
"""
import json
import struct
import sys

import bh_archive as archive
import bh_patchfile as patchfile

SECTOR = 2048


class Piece:
    """`length` bytes: of the image from `at` (data is None) or `data`"""

    __slots__ = ("at", "length", "data")

    def __init__(self, at, length, data=None):
        self.at, self.length, self.data = at, length, data

    def cut(self, start, count):
        if self.data is not None:
            return Piece(0, count, self.data[start:start + count])
        return Piece(self.at + start, count)


class Rope:
    """A file's contents as pieces (see the module's doc)"""

    def __init__(self, disc, pieces):
        self.disc = disc
        self.pieces = [p for p in pieces if p.length > 0]

    @property
    def size(self):
        return sum(p.length for p in self.pieces)

    # ---- reading
    def read(self, start, count):
        out = bytearray()
        position = 0
        end = start + count
        for p in self.pieces:
            lo, hi = max(start, position), min(end, position + p.length)
            if lo < hi:
                if p.data is not None:
                    out += p.data[lo - position:hi - position]
                else:
                    out += self.disc.image_bytes(p.at + lo - position, hi - lo)
            position += p.length
        if len(out) != count:
            raise ValueError(f"read {start}+{count} past the end of a {self.size} byte file")
        return bytes(out)

    def slice(self, start, count):
        """The part [start, start + count) as a rope of its own"""
        out, position, end = [], 0, start + count
        for p in self.pieces:
            lo, hi = max(start, position), min(end, position + p.length)
            if lo < hi:
                out.append(p.cut(lo - position, hi - lo))
            position += p.length
        return Rope(self.disc, out)

    def original_positions(self, start, length):
        """[(offset in this file, original image offset, length)] of the copied pieces that hold the image's bytes [start, start + length): where original
        content has gone to (a replaced piece holds none of it)"""
        out, position = [], 0
        for p in self.pieces:
            if p.data is None:
                lo, hi = max(start, p.at), min(start + length, p.at + p.length)
                if lo < hi:
                    out.append((position + lo - p.at, lo, hi - lo))
            position += p.length
        return out

    def patch_original(self, start, data):
        """The bytes of the original image at [start, start + len(data)), wherever they are in this file now, changed to `data` (the parts replaced since are left)"""
        for position, original, length in self.original_positions(start, len(data)):
            self.write(position, data[original - start:original - start + length])

    def patch_many(self, patches):
        """patch_original for many [(original offset, bytes)] at once (one pass: the English patch has thousands of ranges)"""
        import bisect

        patches = sorted(patches, key=lambda x: x[0])
        starts = [s for s, _ in patches]
        out = []
        for piece in self.pieces:
            if piece.data is not None or not patches:
                out.append(piece)
                continue
            lo, hi = piece.at, piece.at + piece.length
            cursor = lo
            first = max(0, bisect.bisect_right(starts, lo) - 1)
            for k in range(first, len(patches)):
                s, data = patches[k]
                e = s + len(data)
                if s >= hi:
                    break
                if e <= lo:
                    continue
                a, b = max(s, lo, cursor), min(e, hi)
                if a >= b:
                    continue
                if a > cursor:
                    out.append(Piece(cursor, a - cursor))
                out.append(Piece(0, b - a, bytes(data[a - s:b - s])))
                cursor = b
            if cursor < hi:
                out.append(Piece(cursor, hi - cursor))
        self.pieces = out
        self._merge()

    # ---- changing
    def splice(self, start, old_length, new):
        """Replaces [start, start + old_length) by `new` (bytes or a Rope): the file grows or shrinks and what followed moves on"""
        if isinstance(new, (bytes, bytearray)):
            new = Rope(self.disc, [Piece(0, len(new), bytes(new))])
        before, after = self.slice(0, start), self.slice(start + old_length, self.size - start - old_length)
        self.pieces = before.pieces + new.pieces + after.pieces
        self._merge()

    def write(self, start, data):
        self.splice(start, len(data), data)

    def _merge(self):
        merged = []
        for p in self.pieces:
            last = merged[-1] if merged else None
            if last is not None and last.data is None and p.data is None and last.at + last.length == p.at:
                merged[-1] = Piece(last.at, last.length + p.length)
            elif last is not None and last.data is not None and p.data is not None:
                merged[-1] = Piece(0, last.length + p.length, last.data + p.data)
            else:
                merged.append(p)
        self.pieces = merged

    def pad(self, multiple=SECTOR):
        extra = -self.size % multiple
        if extra:
            self.pieces.append(Piece(0, extra, bytes(extra)))

    # ---- archives (an AFS: "AFS\0", a count, (offset, size) for each member, then the name table's offset and size; the names 48 bytes
    # each (32 of name, the size at +44); members start on sectors and follow each other)
    def afs(self):
        if self.read(0, 4) != b"AFS\0":
            raise ValueError("not an AFS")
        count = struct.unpack("<I", self.read(4, 4))[0]
        table = self.read(8, 8 * count + 8)
        entries = [list(struct.unpack_from("<II", table, 8 * i)) for i in range(count)]
        name_at, name_size = struct.unpack_from("<II", table, 8 * count)
        names = []
        if name_at and name_size >= 48 * count:
            block = self.read(name_at, 48 * count)
            names = [block[i * 48:i * 48 + 32].split(b"\0")[0].decode("latin-1") for i in range(count)]
        return count, entries, name_at, name_size, names

    def index_of(self, name):
        names = self.afs()[4]
        matches = [i for i, n in enumerate(names) if n.lower() == name.lower()]
        if len(matches) != 1:
            raise FileNotFoundError(f"{name}: {len(matches)} members of that name")
        return matches[0]

    def member(self, name):
        """The member `name` as a rope (its bytes as the table says, not padded)"""
        _, entries, _, _, _ = self.afs()
        offset, size = entries[self.index_of(name)]
        return self.slice(offset, size)

    def set_member(self, name, data):
        """The member `name` now holds `data` (bytes or a Rope, any size): the archive grows or shrinks by whole sectors, the members after it
        move on, and the table and the name record are rewritten. Returns the archive's new size"""
        count, entries, name_at, name_size, names = self.afs()
        i = self.index_of(name)
        data = Rope(self.disc, [Piece(0, len(data), bytes(data))]) if isinstance(data, (bytes, bytearray)) else Rope(self.disc, list(data.pieces))
        new_size = data.size
        offset, old_size = entries[i]
        old_span = (old_size + SECTOR - 1) // SECTOR * SECTOR
        data.pad()
        delta = data.size - old_span
        self.splice(offset, old_span, data)
        entries[i][1] = new_size
        for j in range(i + 1, count):
            entries[j][0] += delta
        if name_at > offset:
            name_at += delta
        header = bytearray(self.read(8, 8 * count + 8))
        for j, (o, s) in enumerate(entries):
            struct.pack_into("<II", header, 8 * j, o, s)
        struct.pack_into("<II", header, 8 * count, name_at, name_size)
        self.write(8, bytes(header))
        if name_at:
            self.write(name_at + 48 * i + 44, struct.pack("<I", new_size))
        return self.size

    def add_member(self, name, data):
        """A new member `name` of `data` (bytes or a Rope) after the last one (insert_member at the end)"""
        return self.insert_member(self.afs()[0], name, data)

    def insert_member(self, index, name, data):
        """A new member `name` of `data` (bytes or a Rope) as member number `index`: its data goes where member `index`'s began (or after the last), the
        members after it move on by its sectors, and the count, the table (one entry more: it has room for a few before the first member, checked), the name
        table's place and size and a name record (a copy of a neighbour's, renamed and sized) are written. Returns the archive's new size"""
        count, entries, name_at, name_size, names = self.afs()
        if any(n.lower() == name.lower() for n in names):
            raise ValueError(f"{name} is already a member")
        if not 0 <= index <= count:
            raise IndexError(f"member {index}: the archive has {count}")
        first = min(e[0] for e in entries)
        if 8 + 8 * (count + 1) + 8 > first:
            raise ValueError("the archive's table has no room for another member")
        data = Rope(self.disc, [Piece(0, len(data), bytes(data))]) if isinstance(data, (bytes, bytearray)) else Rope(self.disc, list(data.pieces))
        size = data.size
        data.pad()
        at = entries[index][0] if index < count else name_at
        old_names = self.read(name_at, 48 * count)
        record = bytearray(old_names[48 * min(index, count - 1):48 * min(index, count - 1) + 48])
        record[:32] = name.encode("latin-1").ljust(32, b"\0")
        struct.pack_into("<I", record, 44, size)
        new_names = old_names[:48 * index] + bytes(record) + old_names[48 * index:]
        names_rope = Rope(self.disc, [Piece(0, len(new_names), new_names)])
        names_rope.pad()
        before, middle = self.slice(0, at), self.slice(at, name_at - at)
        self.pieces = before.pieces + data.pieces + middle.pieces + names_rope.pieces
        self._merge()
        for e in entries[index:]:
            e[0] += data.size
        entries.insert(index, [at, size])
        header = bytearray(self.read(0, 8 + 8 * (count + 1) + 8))
        struct.pack_into("<I", header, 4, count + 1)
        for j, (o, s) in enumerate(entries):
            struct.pack_into("<II", header, 8 + 8 * j, o, s)
        struct.pack_into("<II", header, 8 + 8 * (count + 1), name_at + data.size, 48 * (count + 1))
        self.write(0, bytes(header))
        return self.size

    # ---- the pieces as a patch's entries
    def entries(self, base):
        """[(disc offset, ("copy", source offset, length) or ("data", bytes))] for the file laid from `base`"""
        out, position = [], base
        for p in self.pieces:
            out.append((position, ("data", p.data) if p.data is not None else ("copy", p.at, p.length)))
            position += p.length
        return out


class VDisc:
    def __init__(self, game):
        self.game = game
        self.arc = archive.Archive(game)
        self.moved = {}       # path -> (new offset, rope)
        self.literals = []    # (offset, bytes, skip): changes made in the image's own place
        self.extra = []       # entries laid after the rest (a patch's ranges for a keyboard's or a pad's prompts, moved with their file)
        self.end = self._image_sectors() * SECTOR

    def _image_sectors(self):
        import os

        raw = os.path.getsize(archive.disc_path(self.game))
        return raw // (2352 if self.arc.disc.sector_size == 2352 else 2048) if hasattr(self.arc.disc, "sector_size") else raw // 2048

    def image_bytes(self, offset, count):
        first, last = offset // SECTOR, (offset + count + SECTOR - 1) // SECTOR
        raw = self.arc.disc.read_sectors(first, last - first)
        skip = offset - first * SECTOR
        return raw[skip:skip + count]

    def open(self, path):
        """A rope of the file `path` (an ISO file), in the image's own place"""
        node = self.arc.open(path)
        return Rope(self, [Piece(node.offset, node.size)])

    def move(self, path, rope):
        """The file `path` is `rope` from now on, laid at the end of the disc (a free place nothing else uses), its directory entry changed to say so"""
        rope = Rope(self, list(rope.pieces))
        offset = (self.end + SECTOR - 1) // SECTOR * SECTOR
        self.moved[path] = (offset, rope)
        self.end = offset + (rope.size + SECTOR - 1) // SECTOR * SECTOR
        record_at, record = self._record_location(path)
        patched = bytearray(record)
        struct.pack_into("<I", patched, 2, offset // SECTOR)
        struct.pack_into(">I", patched, 6, offset // SECTOR)
        struct.pack_into("<I", patched, 10, rope.size)
        struct.pack_into(">I", patched, 14, rope.size)
        self.literals.append((record_at, bytes(patched[2:18]), 2))
        return offset // SECTOR

    def _record_location(self, path):
        """(the byte offset in the image of the file's directory record, the record's bytes)"""
        entry = self.arc.disc.root
        parts = [p for p in path.split("/") if p]
        for n, part in enumerate(parts):
            data = self.arc.disc.read_extent(entry["lba"], entry["size"])
            pos = 0
            found = None
            while pos < len(data):
                length = data[pos]
                if length == 0:
                    pos = (pos // SECTOR + 1) * SECTOR
                    continue
                rec = self.arc.disc._parse_record(data[pos:pos + length])
                if rec["name"].upper() == part.upper():
                    found = (entry["lba"] * SECTOR + pos, data[pos:pos + length], rec)
                    break
                pos += length
            if found is None:
                raise FileNotFoundError(path)
            if n == len(parts) - 1:
                return found[0], found[1]
            entry = found[2]
        raise FileNotFoundError(path)

    def write(self, path):
        """The patch file: the moved files' pieces and the directory entries, as version 3 entries"""
        entries = []
        for offset, rope in self.moved.values():
            entries += rope.entries(offset)
        for offset, data, skip in self.literals:
            entries.append((offset + skip, ("data", data), 0))
        patchfile.write_patch_v3(path, [(e[0], e[1], e[2] if len(e) > 2 else 0) for e in entries] + self.extra)


def main(argv):
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
