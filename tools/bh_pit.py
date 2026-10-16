"""The "pit" files (pit_comn.dat, pit_sNNN.dat): MOMO containers of little tables; the item names and descriptions, the SP
items, the FILE documents, the place names. Their text is Shift-JIS with every byte inverted (bitwise NOT: a newline is 0xF5,
the terminator 0xFF, a tag like <c1> is C3 9C CE C1), which is why it was never found as Shift-JIS on the disc. Evidence: the
game's copy loop at 0x1C15A0 xors each byte with 0xFF when it fills the item window.

  PitFile(data).entries()          [(index, kind, bytes)] of the entries the patch handles
  SlotTable(entry, limit)          fixed slots of text (item names, descriptions, SP items, place names): each replaced in place
  DocChain(entry)                  the FILE documents: records (next, pages, kind, offsets) with the text after them

Both: texts() -> {unit id: Shift-JIS bytes}, rebuilt(new) -> the entry's new bytes ({unit id: English bytes}).
"""
import re
import struct

import bh_text as text

TEXT = re.compile(rb"(?:[\x81-\x9f\xe0-\xea][\x40-\x7e\x80-\xfc]|[\x20-\x7e\n]|<[a-z0-9]+>)+")

# Which entries of the containers hold text, and how (checked by looking at all of them: the others are pictures and tables
# whose bytes only look like text by chance). A slot table's `limit` is how far into the entry its slots go.
COMN = {0: ("slots", None), 4: ("slots", None)}
STAGE = {0: ("slots", None), 1: ("docs", None), 3: ("slots", 0x2000), 4: ("slots", None)}


def invert(b):
    return bytes(x ^ 0xFF for x in b)


def momo(data):
    """[(offset, size)] of the entries of a MOMO container."""
    if data[:4] != b"MOMO":
        raise ValueError("not a MOMO container")
    return [struct.unpack_from("<II", data, 8 + 8 * i) for i in range(struct.unpack_from("<I", data, 4)[0])]


def kinds_of(name):
    return COMN if name == "pit_comn.dat" else STAGE


def decodes(raw):
    try:
        raw.decode("cp932")
        return True
    except UnicodeDecodeError:
        return False


class SlotTable:
    def __init__(self, entry, limit):
        self.entry = entry
        sjis = invert(entry)
        end = len(entry) if limit is None else min(limit, len(entry))
        found = [(m.start(), m.group()) for m in TEXT.finditer(sjis[:end]) if len(m.group()) >= 2 and sjis[m.end()] == 0xFF]
        self.slots = []  # (start, capacity incl. the terminator, Shift-JIS bytes) of the runs with Japanese in them
        for i, (start, raw) in enumerate(found):
            room = (found[i + 1][0] if i + 1 < len(found) else len(entry)) - start
            if text.has_japanese(raw) and decodes(raw):
                self.slots.append((start, room, raw))

    def texts(self):
        return {text.unit_id(raw): raw for _, _, raw in self.slots}

    def rebuilt(self, new):
        out = bytearray(self.entry)
        for start, room, raw in self.slots:
            if text.unit_id(raw) in new:
                english = new[text.unit_id(raw)]
                if len(english) + 1 > room:
                    raise text.DoesNotFit(f"pit slot at {start:#x} (unit {text.unit_id(raw)}, {english[:30]!r}): {len(english) + 1} bytes, the slot has {room}")
                out[start:start + room] = invert(english) + bytes(room - len(english))  # the terminator: 0xFF once inverted
        return bytes(out)


class DocChain:
    """u32 count, then count records, each: u32 offset of the next record, u16 pages, u16 kind, (pages + 1) u32 offsets
    (the title, then each page), then the texts: the title, then the pages, each ending in a newline; every record ends on
    a four byte boundary, padded with zero bytes (0xFF once inverted). The last record's next is the end of the data."""

    def __init__(self, entry):
        self.entry = entry
        self.count = struct.unpack_from("<I", entry, 0)[0]
        self.records = []  # (position, kind, [Shift-JIS bytes of the title and the pages without the padding])
        pos = 4
        sjis = invert(entry)
        for _ in range(self.count):
            nxt, pages, kind = struct.unpack_from("<IHH", entry, pos)
            offs = list(struct.unpack_from("<%dI" % (pages + 1), entry, pos + 8)) + [nxt]
            if not (pos < offs[0] <= nxt <= len(entry) and offs == sorted(offs)):
                raise ValueError(f"document record at {pos:#x} is not in the format")
            self.records.append((pos, kind, [sjis[offs[i]:offs[i + 1]].rstrip(b"\xff") for i in range(pages + 1)]))
            pos = nxt

    def texts(self):
        return {text.unit_id(raw): raw for _, _, segs in self.records for raw in segs if text.has_japanese(raw)}

    def rebuilt(self, new):
        """Every record keeps its place and its next link (the game or another table may refer to the offsets: moving the
        records made the Training Ground's load crash), so a record's text must fit the record's own room."""
        out = bytearray(self.entry)
        for index, (pos, kind, segs) in enumerate(self.records):
            end = struct.unpack_from("<I", self.entry, pos)[0]
            body = [new.get(text.unit_id(raw), raw) for raw in segs]
            body = [b + b"\n" if raw.endswith(b"\n") and not b.endswith(b"\n") else b for b, raw in zip(body, segs)]
            head = 8 + 4 * len(body)
            at, offs = pos + head, []
            for b in body:
                offs.append(at)
                at += len(b)
            if at > end:
                raise text.DoesNotFit(f"document record {index} at {pos:#x} needs {at - pos} bytes, it has {end - pos}")
            out[pos + 8:pos + head] = struct.pack("<%dI" % len(offs), *offs)
            out[pos + head:end] = invert(b"".join(body)) + bytes(end - at)
        return bytes(out)
