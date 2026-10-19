"""File 1's item and message files (the game's data\\rom\\scene\\item_msg and load_msg folders): romdata.afs's itmsg.dat (item
names and descriptions), spitem.dat (SP items), evelist.dat (the scenario goals), newfact.dat (the "new element" notices);
each stage's itmsgNNN.dat (its own items), fileNNN.dat (its FILE documents), dlmsgNNN.dat (the loading screens' "Meanwhile"
captions) and mapNNN.dat (the map's room, area and floor names). Their text is Shift-JIS with every byte inverted, like File
2's pit files (bh_pit); it sits in fixed fields, each its text then zero bytes. The "e" files (itmsge.dat, fileNNNe.dat,
itmsgNNNe.dat) and the fields next to the Japanese ones hold a plain or inverted English the Japanese game doesn't show; they
are left as they are.

  tables(name, data)   [(offset in data, SlotTable or DocChain)] of a file, by its name; [] for a file with no text

Evidence for each layout is in its function; Fields checks that every field it takes is text followed by zero bytes only, so
a layout that is wrong fails loudly instead of writing over other data.
"""
import re
import struct

import bh_pit as pit
import bh_text as text


class Fields(pit.SlotTable):
    """Inverted text in fixed fields, [(offset, room)] (the room counts the terminator). The fields holding Japanese are the
    units; texts() and rebuilt() are SlotTable's."""

    def __init__(self, entry, fields):
        self.entry = entry
        sjis = pit.invert(entry)
        self.slots = []
        for start, room in fields:
            raw = sjis[start:start + room].split(b"\xff")[0]
            if not raw or len(raw) == room or not pit.TEXT.fullmatch(raw) or not text.has_japanese(raw) or not pit.decodes(raw):
                continue
            if any(entry[start + len(raw):start + room]):
                raise ValueError(f"field at {start:#x}: bytes after its text, the layout is not this file's")
            self.slots.append((start, room, raw))


def itmsg_fields(data):
    """itmsg.dat, itmsgNNN.dat: records of 0x40 bytes, a u32 (0 or 0xFF00) then a 60 byte text field; the names' records, then
    the descriptions' (the Japanese descriptions run up to 60 bytes)."""
    return [(p, 0x3C) for p in range(4, len(data) - 0x3B, 0x40)]


def spitem_fields(data):
    """spitem.dat: records of 0x108 bytes: u32 number (1, 2, ...), the name (20 bytes), the description (240)."""
    return [f for k in range(len(data) // 0x108) for f in ((0x108 * k + 4, 0x14), (0x108 * k + 0x18, 0xEC))]


def dlmsg_fields(data):
    """dlmsgNNN.dat: u32 size, u32 count N, N bytes, N bytes, then N English captions of 0x40 bytes, then N Japanese ones of
    0x80 bytes, padded to four bytes (checked against the size)."""
    size, count = struct.unpack_from("<II", data, 0)
    first = 8 + 2 * count + 0x40 * count
    if size != len(data) or (first + 0x80 * count + 3) & ~3 != size:
        raise ValueError(f"dlmsg: {count} captions don't add up to the size {size:#x}")
    return [(first + 0x80 * k, 0x80) for k in range(count)]


def map_fields(data):
    """mapNNN.dat: u32 size, u32 room count, u32 offset of the rooms (0x58 bytes each: 8 bytes, the Japanese name in 0x20,
    the English in 0x30), u32, u32 offset of the floors, u32, u32, u32 offset of what follows them. The floors are records of
    0x5C bytes starting with the area's name and the floor's name, 0x20 bytes each."""
    size, rooms, room_at, _, floor_at, _, _, after = struct.unpack_from("<8I", data, 0)
    if size != len(data):
        raise ValueError(f"map: size {size:#x} is not the file's")
    fields = [(room_at + 0x58 * k + 8, 0x20) for k in range(rooms)]
    for at in range(floor_at, after - 0x3F, 0x5C):
        fields += [(at, 0x20), (at + 0x20, 0x20)]
    return fields


LAYOUTS = [
    (r"itmsg(\d{3})?\.dat", itmsg_fields),
    (r"spitem\.dat", spitem_fields),
    (r"evelist\.dat", lambda data: [(p, 0x40) for p in range(0, len(data), 0x40)]),  # one goal per 0x40 bytes
    (r"newfact\.dat", lambda data: [(p, 0x100) for p in range(0, len(data), 0x100)]),  # one notice per 0x100 bytes
    (r"dlmsg\d{3}\.dat", dlmsg_fields),
    (r"map\d{3}\.dat", map_fields),
]


def tables(name, data):
    name = name.lower()
    if re.fullmatch(r"file\d{3}\.dat", name):  # the FILE documents: File 2's document chain
        return [(0, pit.DocChain(data))]
    for pattern, layout in LAYOUTS:
        if re.fullmatch(pattern, name):
            return [(0, Fields(data, layout(data)))]
    return []
