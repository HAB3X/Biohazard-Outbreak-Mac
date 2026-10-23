#!/usr/bin/env python3
"""Reads the games' room files (.rdt, the members of NETBIO00.DAT's and NETBIO01.DAT's r0xx.afs): the header's sections and the
cameras.

    bh_rdt.py GAME check          every room on the disc: the sections tile the file and the cameras are plausible
    bh_rdt.py GAME cams ROOM      a room's cameras (ROOM like r0300100.rdt, or just r0300100)
    bh_rdt.py GAME areas ROOM     a room's trigger areas (doors, messages, desks ...)
    bh_rdt.py GAME doors          each scenario's doors as room -> rooms

The file: 32 sections as (offset, size) pairs of int32, then the sections' contents one after another (checked: the first 31's
offsets tile the file with no gaps on every room of both discs; the 32nd pair isn't an offset and size: in File 1's r0023100 it is
(1, 6), the camera count there). Section 0 holds the cameras: an int32 tag (1), the camera count and
that many offsets (from the section's start) to 320 byte camera records. The layout was learned from a community 010 Editor
template (Dchaps' rdt_temp.bt: reference only, no licence) and then checked against the running game: File 2's classic
camera's eye and target in the zoo's first room are camera 2's of r0300100.rdt. Section 10 holds the trigger areas (same source: Dchaps' NETBIO_AOT.ms): an int32 tag, a count and that many offsets, to records
of 176 bytes (every one of the 4281 in File 2): tag, subtype, a quad as four (x, z) corners (ints 2..9), the height range (ints
10 and 11) and then data that depends on the tag. A tag 0 record is a door: int 12 its number in the room, int 13 the number of
the room it leads to (the yy of r0yyyy00.rdt: of 370 door links in File 2, 359 are returned by the room they lead to), int 14
the number of the door it joins there (381 of 382 doors of File 2 and 257 of 262 of File 1 are returned by that door: the
others are one way), int 18 the facing it gives the character arriving (65536 a full turn, direction (sin, cos) over x and z:
88% point into the room by a rough centre). A tag 1 record is an exit that names the arrival itself: int 13 the room, ints
15..18 a point and facing, ints 19..22 the point and facing the character is put at, int 24 how far along that facing from
the point. Seen live: a tag 5 record of subtype 1 shows a message ("The gate is closed with a chain" in the zoo's first room).
The other tags (2 3 4 5 6 7 10 11 12 13) are kept as they are.
The other fields of a camera are listed as
they are found, with what they were checked against; the rest of the sections
(1 a script, 2 and 3 sound banks, 7, 10 doors and trigger zones, 11 lights, 13 fog, 15, 16 sound zones) are kept as bytes.

A camera record (int32 unless said): 0 type (1 and 2 seen with the fixed cut; 7 once: what they mean is not checked),
3..6 zeros in the rooms looked at, 7 and 8 the field of view in degrees (float, horizontal then vertical: 50 and 65), 9..32 a
box as eight points (x, y up, z; the floor's four corners then the ceiling's: the region that switches to this camera), 33..38
two points, 39..41 the eye (x, y up, z), 42..44 another point (kept as `aim`: not what the game looks at), 45..47 the target,
48..56 three more points. Eye and target were read from File 2's memory in the zoo's first room (eye 12119, 116, 11709, target
11907, 177, 11260) and found at those places in camera 2 of r0300100.rdt; the field of view and the box are read off the file
only (not yet checked against the game).
"""
import struct
import sys

import bh_archive as archive

SECTIONS = 32
# A camera's fog record (section 13: a tag, the camera count, offsets, then 96 bytes for each camera), numbers of 4 bytes: what is known,
# each seen in the running game (File 2, the zoo's first room): 4 the scene's colour (bytes blue, green, red, alpha: changing it to
# blue turned the whole picture blue), 9 to 11 the characters' tint as floats red, green, blue (1, 0, 0 turned the characters red),
# 3 an ambient colour added to the whole picture (bytes blue, green, red: 0x222222 is grey 34; set to 0 the picture got 5 darker in
# each channel, to 0xFF in one byte 32 lighter in that channel only), 2 a float distance (7000: set to 0 the picture got much
# brighter and bluer, to 28000 a little darker: the fog's distance) and 1 a float (-200: x4 made the picture 1 lighter: weak, probably the
# fog's near end). Tried and no change seen (mean difference within the game's frame to frame noise, 0.5 to 1 of 255) in the one room
# and view: 0, 5 to 8 and 12 to 23, zeroed and changed.
FOG_FIELDS = {1: "fog near (float)", 2: "fog distance (float)", 3: "ambient colour", 4: "scene colour", 9: "character tint red",
              10: "character tint green", 11: "character tint blue"}
CAMERA_SIZE = 320
AREA_SIZE = 176


class Camera:
    def __init__(self, record):
        self.kind = struct.unpack_from("<I", record, 0)[0]
        self.fov = struct.unpack_from("<ff", record, 7 * 4)
        self.box = [struct.unpack_from("<3i", record, (9 + 3 * i) * 4) for i in range(8)]
        self.eye = struct.unpack_from("<3i", record, 39 * 4)
        self.aim = struct.unpack_from("<3i", record, 42 * 4)
        self.target = struct.unpack_from("<3i", record, 45 * 4)


class Area:
    """A trigger area of section 10"""

    def __init__(self, record):
        self.tag, self.subtype = struct.unpack_from("<ii", record, 0)
        self.quad = [struct.unpack_from("<ii", record, 8 + 8 * i) for i in range(4)]
        self.height = struct.unpack_from("<ii", record, 40)
        self.fields = struct.unpack_from("<30i", record, 48)

    @property
    def leads_to(self):
        """The room number a door (tag 0) or an exit (tag 1) leads to"""
        return self.fields[1] if self.tag in (0, 1) else None

    @property
    def door_number(self):
        """A door's number in its room, which the door of the room it leads to names (tag 0)"""
        return self.fields[0] if self.tag == 0 else None

    @property
    def other_door(self):
        """The number of the door, in the room this one leads to, that it joins (tag 0)"""
        return self.fields[2] if self.tag == 0 else None

    @property
    def facing(self):
        """The turn (0x10000 a full turn; the direction is (sin, cos) of it over x and z) a door (tag 0) has its player face
        arriving through it, and an exit's (tag 1) arrival facing"""
        return self.fields[6] if self.tag == 0 else self.fields[10] if self.tag == 1 else None

    def centre(self):
        return (sum(p[0] for p in self.quad) / 4, sum(p[1] for p in self.quad) / 4)

    def arrival(self, destination):
        """(x, z, facing): where the game puts a character coming through this door or exit into `destination` (its Room).
        An exit (tag 1) names the spot itself: the point in ints 19 and 21 (floats) pushed int 24 along the facing in int 22
        (measured once in File 2: the room 4 exit of the zoo's first room, predicted (11293.9, 10443.9) at 225 degrees and read
        (11293.9, 10443.9) from the game's memory). A door (tag 0) puts them in front of the door it joins in `destination`:
        facing as that door's own, 60 to 65 units along it from the door's centre and within about 20 to the side (measured on
        the zoo's first street's two doors, both ways)."""
        import math
        if self.tag == 1:
            x, z = struct.unpack("<ff", struct.pack("<ii", self.fields[7], self.fields[9]))
            turn = self.fields[10] / 65536 * 2 * math.pi
            return (x + self.fields[12] * math.sin(turn), z + self.fields[12] * math.cos(turn), self.fields[10])
        if self.tag == 0:
            for door in destination.areas:
                if door.tag == 0 and door.door_number == self.other_door:
                    turn = door.facing / 65536 * 2 * math.pi
                    cx, cz = door.centre()
                    return (cx + 62 * math.sin(turn), cz + 62 * math.cos(turn), door.facing)
        return None


class SoundZone:
    """A box (section 16, first block) that gives the character standing in it a floor sound: 104 bytes, tag 1, eight corners (x, y,
    z ints: the four at the lower height, then the same four higher) and the sound's number (int 25). Measured in File 2's zoo: the
    number is what the game keeps for the player in the halfword at 0x176 of her record (a zone given the number 0x1234 put
    0x1234 there when she stood in it and 1, the surrounding zone's, outside)"""

    def __init__(self, record):
        self.tag = struct.unpack_from("<i", record, 0)[0]
        self.box = [struct.unpack_from("<3i", record, 4 + 12 * i) for i in range(8)]
        self.sound = struct.unpack_from("<i", record, 100)[0]


class SoundPoint:
    """A positional sound (section 16, second block; 28 or 48 bytes): tag, a kind number, a position (x, y, z floats), then numbers
    not decoded. The position is in the room's coordinates; what the kind and the rest mean isn't known"""

    def __init__(self, record):
        self.tag, self.kind = struct.unpack_from("<ii", record, 0)
        self.position = struct.unpack_from("<3f", record, 8)
        self.rest = struct.unpack_from(f"<{(len(record) - 20) // 4}i", record, 20)


class Light:
    """A character light (section 11): 52 bytes of 13 floats: position x, y, z, an attenuation (0.001 to 0.007 in File 2's rooms), a
    range, a colour (red, green, blue), a pad, a second colour (red, green, blue) and a pad. Found by their shape (the section's
    header isn't decoded), 1849 in File 2's rooms, 99% of them within 3000 of their room's trigger areas. Measured in File 2's zoo
    (the characters' colours in the picture, the surroundings unchanged): moving all eight next to the characters lit them (+1.3
    in red, green and blue), the colour's red set to 4 added +1.7 in red only, the second colour's red added more (+2.5 against 1.3 with
    the move alone), and a zero attenuation or a range of 4 put the light out; the surroundings never changed: they light characters only"""

    def __init__(self, floats, start):
        self.start = start
        self.position = tuple(floats[0:3])
        self.attenuation = floats[3]
        self.range = floats[4]
        self.colour = tuple(floats[5:8])
        self.colour2 = tuple(floats[9:12])


# What the script ops do, from reading the game's handlers (the table of handlers at 0x74CCE0 in File 2's memory, handler addresses in the game
# overlay; docs/research/script-ops.md has the evidence). Each is (name, what the words after the op word are). Only the ops read in the game's code
# are here; an op not in the table is listed as "op 0x.." and its handler is the one in the dump.
OPS = {
    0x0C: ("SetFlag", "type, flag number, value (0 clears, else sets; type 0 is bank B at 0x490030)"),
    0x0B: ("FlagOp", "mode (0, 1, 2), number, byte: sets or clears flags in other banks by mode"),
    0xE6: ("IfFlagB", "flag number, value: runs the following block when bank B's flag equals value, else skips to its end"),
    0xEC: ("IfFlagAB", "flag number, value: the block runs when (bank A's flag and not bank B's) equals value"),
    0xE3: ("IfGlobal", "number, value: the block runs when the flag read by function 0x6d74a0 equals value"),
    0xED: ("IfGlobal2", "number, value: as IfGlobal with the second function 0x6d7510 as well"),
    0xEF: ("IfGlobalB", "number, value: the block runs when function 0x6d7510's flag equals value"),
    0xEE: ("IfByte", "byte, byte: compares against a table at 0x49135a"),
    0x08: ("IfPlayer", "number, byte: a conditional block (reads the thread)"),
    0x113: ("IfCounter", "three numbers: a conditional block"),
    0xB0: ("ShowObject", "object number (through the script's 32 byte table): clears bit 7 of the object's flag at 0x475080 + n"),
    0xB1: ("HideObject", "object number: sets bit 7 of the object's flag"),
    0xB5: ("SetActorState", "actor id, byte: stores the byte at +113 of every actor with that id"),
    0x55: ("SetExamine", "area id, message number, parameter: activates the examine areas (tag 5) with that id and gives them the message and the parameter"),
    0x56: ("ResetExamine", "area id: calls the game's function 0x59a7c0 for the examine areas with that id"),
    0x58: ("EnableItem", "area id, value: activates the item and lock areas (tag 3 subtypes 1 and 3, tag 4) with that id; tag 3's value goes into its field 18"),
    0x59: ("DisableItem", "area id: deactivates the same areas"),
    0x5C: ("DisableExit", "area id: deactivates the exit areas (tag 1) with that id"),
    0x5F: ("SetDoorLock", "door id, value: stores the value in field 22 of the door (tag 0) and room door (tag 2) areas with that id"),
    0x54: ("SetDoor", "door id, number, byte: stores them in the door areas' runtime fields"),
    0x104: ("Activate", "area tag, area id, value: turns on the areas of that tag with that id (and value)"),
    0x105: ("Deactivate", "area tag, area id, value: turns them off"),
    0xD0: ("SpawnEnemy", "kind, id, then five words copied into the new enemy's record (a position and angles)"),
    0x45: ("SetOption", "flag, value, index (below 8), number: two small tables at 0x491258 and 0x491268"),
    0x110: ("SetVar", "index (below 8), value: stores the value in the table at 0x491268"),
    0x20: ("SelectCharacter", "slot number: stored at 0x3134CE (the current character) only when it is below 4 and the character record 0x3B1780 + 1672 * n (halfword +0) is non-zero; measured: 0 is taken, 7 leaves 255"),
    0x13: ("SetHudState", "byte, halfword: stored at 0x3B31D5 and 0x3B31D6 (with two more bytes), by game_005952d0"),
    0x10: ("EnableSlot", "slot number: writes the byte at the start of the 536 byte record 0x3B3200 + 536 * n (op 0x11 writes 0 there)"),
    0x11: ("DisableSlot", "slot number: writes 0 to the byte at the start of that 536 byte record"),
    0x7E: ("MarkEntry", "entry number (below 32): sets bit 0 of the flag byte of the 104 byte entry at 0x395890 + 104 * n, when the entry is in use"),
    0x7F: ("UnmarkEntry", "entry number: reads the same 104 byte table as MarkEntry (the clearing partner, not traced further)"),
    0xA0: ("SetFlagC", "flag number: touches bank C, the word at 0x4900FC (op 0xA1 clears the bit; this one is the same word's partner)"),
    0xA1: ("ClearFlagC", "flag number: clears that bit of the word at 0x4900FC"),
    0x1A5: ("SetCharacterMode", "value: for each of the eight character records in use (0x47BD30 + 0x1100 * n, byte +0 and byte +2996 non-zero) writes 1 (value 1) or 0 to +4176; measured: 1 to 0 cleared slot 1's"),
    0x155: ("IfCharacter", "character byte, mode: compares it with a byte of the current character's record (0x47C960 + 0x1100 * slot); mode 0 runs the block when they differ, any other mode when they are the same"),
    0x180: ("IfSinglePlay", "value: the block runs when the cheat_single_play() result differs from the value (counted per thread like IfCharacter)"),
    0x73: ("Aim", "id, mode, x, y, z: mode 2 uses the position given, mode 1 the position of a character record (4 means the current one), 0 none; passed to f_001b8170 (which also sets 0x3134..: the camera or effect's point of interest)"),
    0x94: ("SpawnEffect", "kind byte, two bytes: allocates an effect record and fills it (kind, 1, 5 and the two bytes)"),
    0x131: ("SetExamineVariant", "area id, message, parameter, message: sets the examine areas' (tag 5) fields 16 and 17 (a message and the number chosen by the character mode) and activates them"),
    0x135: ("SetExamineVariant5", "area id and four words: activates the examine areas with that id and gives them their fields (the same loop as SetExamineVariant)"),
    0x70: ("Op70", "number: calls f_001b9800(0, number), a long routine of the executable (seen with values up to 47)"),
    0x120: ("Op120", "number: game_006957f0(number, ...) (its partner op 0x121 passes 0; not read further)"),
    0x121: ("Op121", "number: game_006957f0(number, 0): the partner of op 0x120"),
    0x1A0: ("CallOverlay", "three words: handed to the dynamic function at 0x7D6340"),
}


class Instruction:
    """A script record: `cls` (1, 2 or 3: the top byte of its first word), `op` (the low 24 bits), its words (2 ** cls of them, the
    first being the op's own), and where it starts in the script, in words"""

    def __init__(self, at, words):
        self.at = at
        self.cls = words[0] >> 24
        self.op = words[0] & 0xFFFFFF
        self.words = list(words)
        self.args = list(words[1:])

    @property
    def name(self):
        return OPS[self.op][0] if self.op in OPS else f"op {self.op:#x}"


class Room:
    def __init__(self, data):
        self.original = bytes(data)
        self.data = bytearray(data)
        self.sections = [struct.unpack_from("<ii", data, 8 * i) for i in range(SECTIONS)]
        offset, size = self.sections[0]
        tag, count = struct.unpack_from("<ii", data, offset)
        if tag != 1 or not 0 <= count <= 64:
            raise ValueError(f"the camera section starts with tag {tag} and count {count}")
        offsets = struct.unpack_from(f"<{count}i", data, offset + 8)
        self.cameras = [Camera(data[offset + o:offset + o + CAMERA_SIZE]) for o in offsets]
        self.camera_offsets = offsets
        self.areas = self._areas()

    def _areas(self):
        offset, size = self.sections[10]
        if not size:
            return []
        tag, count = struct.unpack_from("<ii", self.data, offset)
        offsets = struct.unpack_from(f"<{count}i", self.data, offset + 8)
        for at in offsets:
            if at < 0 or at + AREA_SIZE > size:
                raise ValueError(f"a trigger area at {at} leaves its section")
        self.area_offsets = offsets
        self._sounds()
        self._lights()
        return [Area(bytes(self.data[offset + at:offset + at + AREA_SIZE])) for at in offsets]

    def _lights(self):
        self.lights = []
        offset, size = self.sections[11]
        last = -52
        for start in range(0, max(0, size - 52) + 1, 4):
            f = struct.unpack_from("<13f", self.data, offset + start)
            if (all(abs(v) < 1e5 for v in f) and 0 < f[3] < 0.2 and 5 < f[4] < 50000 and abs(f[0]) > 100 and abs(f[2]) > 100
                    and all(0 <= v <= 4 for v in f[5:8]) and f[8] == 0 and f[12] == 0 and start >= last + 52):
                self.lights.append(Light(f, start))
                last = start

    def set_light(self, index, position=None, attenuation=None, range=None, colour=None, colour2=None):
        base = self.sections[11][0] + self.lights[index].start
        if position is not None:
            self._put(base, "<3f", *map(float, position))
        if attenuation is not None:
            self._put(base + 12, "<f", float(attenuation))
        if range is not None:
            self._put(base + 16, "<f", float(range))
        if colour is not None:
            self._put(base + 20, "<3f", *map(float, colour))
        if colour2 is not None:
            self._put(base + 36, "<3f", *map(float, colour2))
        self._lights()

    SCRIPT_HEADER = 16  # words

    def script_words(self):
        """Section 1 (the room's script) as signed 32 bit words"""
        offset, size = self.sections[1]
        return list(struct.unpack_from(f"<{size // 4}i", self.data, offset))

    def script_header(self):
        """The script's first 16 words: [0] the offset of its tail table, [1] of the table after it (32 bytes indexed by a number below
        32: the game reads it through a function that returns the byte for an index), [8] and [9] where two blocks of records start"""
        return self.script_words()[:self.SCRIPT_HEADER]

    def script_instructions(self):
        """The records of the script's blocks (from header word 8 to the tail table) as Instruction objects, read by the width rule
        found on the data: a word whose top byte is 1, 2 or 3 starts a record of 2, 4 or 8 words; the words between records
        (zeros, and lists of numbers such as 33 37 162 400) are not instructions. The rule isn't confirmed by the game's own reader"""
        words = self.script_words()
        if len(words) <= self.SCRIPT_HEADER:
            return []
        start, end = words[8] // 4, min(words[0] // 4, len(words))
        out, i = [], start
        while i < end:
            top = (words[i] & 0xFFFFFFFF) >> 24
            if top in (1, 2, 3):
                width = 2 ** top
                out.append(Instruction(i, words[i:i + width]))
                i += width
            else:
                i += 1
        return out

    def set_script_word(self, index, value):
        offset, size = self.sections[1]
        if not 0 <= index < size // 4:
            raise IndexError(f"script word {index}: the script has {size // 4}")
        self._put(offset + 4 * index, "<i", int(value))

    def _sounds(self):
        """The sound zones and sound points of section 16 (empty lists when the section is empty)"""
        self.sound_zones, self.sound_points, self.sound_zone_offsets, self.sound_point_offsets = [], [], [], []
        offset, size = self.sections[16]
        if size < 8:
            return
        zones, points = struct.unpack_from("<ii", self.data, offset)
        offsets = struct.unpack_from(f"<{zones + points}i", self.data, offset + 8)
        ends = list(offsets[1:]) + [size]
        for k, (start, end) in enumerate(zip(offsets, ends)):
            record = bytes(self.data[offset + start:offset + end])
            if k < zones and len(record) >= 104:
                self.sound_zones.append(SoundZone(record))
                self.sound_zone_offsets.append(start)
            elif k >= zones and len(record) >= 20:
                self.sound_points.append(SoundPoint(record))
                self.sound_point_offsets.append(start)

    def set_sound_zone(self, index, box=None, sound=None):
        """box the eight corners (x, y, z), sound the floor sound's number"""
        base = self.sections[16][0] + self.sound_zone_offsets[index]
        if box is not None:
            if len(box) != 8:
                raise ValueError("a sound zone's box has eight corners")
            for i, point in enumerate(box):
                self._put(base + 4 + 12 * i, "<3i", *map(int, point))
        if sound is not None:
            self._put(base + 100, "<i", int(sound))
        self._sounds()

    def set_sound_point(self, index, position=None, kind=None):
        base = self.sections[16][0] + self.sound_point_offsets[index]
        if position is not None:
            self._put(base + 8, "<3f", *map(float, position))
        if kind is not None:
            self._put(base + 4, "<i", int(kind))
        self._sounds()

    # Editing: the file's size never changes (the archive's members can't grow), so an edit overwrites numbers where they are
    def _put(self, at, fmt, *values):
        struct.pack_into(fmt, self.data, at, *values)

    def _camera_base(self, index):
        if not 0 <= index < len(self.cameras):
            raise IndexError(f"camera {index}: the room has {len(self.cameras)}")
        return self.sections[0][0] + self.camera_offsets[index]

    def _area_base(self, index):
        if not 0 <= index < len(self.areas):
            raise IndexError(f"area {index}: the room has {len(self.areas)}")
        return self.sections[10][0] + self.area_offsets[index]

    def set_camera(self, index, eye=None, target=None, fov=None, box=None):
        """eye and target (x, y, z), fov (horizontal, vertical degrees), box the eight points (x, y, z) of the switch box"""
        base = self._camera_base(index)
        if eye is not None:
            self._put(base + 39 * 4, "<3i", *map(int, eye))
        if target is not None:
            self._put(base + 45 * 4, "<3i", *map(int, target))
        if fov is not None:
            self._put(base + 7 * 4, "<2f", *map(float, fov))
        if box is not None:
            if len(box) != 8:
                raise ValueError("a camera's box has eight points")
            for i, point in enumerate(box):
                self._put(base + (9 + 3 * i) * 4, "<3i", *map(int, point))
        self._reparse()

    def set_area(self, index, quad=None, height=None, fields=None):
        """quad four (x, z) corners, height (low, high), fields {field number from int 12: value} for the integers after them"""
        base = self._area_base(index)
        if quad is not None:
            if len(quad) != 4:
                raise ValueError("an area's quad has four corners")
            for i, (x, z) in enumerate(quad):
                self._put(base + (2 + 2 * i) * 4, "<2i", int(x), int(z))
        if height is not None:
            self._put(base + 10 * 4, "<2i", *map(int, height))
        for number, value in (fields or {}).items():
            if not 12 <= number < 12 + 30:
                raise ValueError(f"field {number} is outside the record's data (12 to 41)")
            self._put(base + number * 4, "<i", int(value))
        self._reparse()

    def _fog_base(self, camera):
        """Where fog record `camera` is (a record number: the cameras name theirs in int 61)"""
        offset, size = self.sections[13]
        tag, count = struct.unpack_from("<ii", self.data, offset)
        if not 0 <= camera < count:
            raise IndexError(f"fog {camera}: the room has {count}")
        return offset + struct.unpack_from("<i", self.data, offset + 8 + 4 * camera)[0]

    def fog(self, camera):
        """The 24 numbers of camera `camera`'s fog record as raw ints (see FOG_FIELDS)"""
        return struct.unpack_from("<24i", self.data, self._fog_base(camera))

    def set_fog(self, camera, ints=None, floats=None):
        """ints {number: int} and floats {number: float} overwrite numbers of a camera's fog record (FOG_FIELDS says which)"""
        base = self._fog_base(camera)
        for number, value in (ints or {}).items():
            if not 0 <= number < 24:
                raise ValueError(f"fog number {number} is outside the record (0 to 23)")
            self._put(base + 4 * number, "<i", int(value))
        for number, value in (floats or {}).items():
            if not 0 <= number < 24:
                raise ValueError(f"fog number {number} is outside the record (0 to 23)")
            self._put(base + 4 * number, "<f", float(value))

    # Growing: a room can take up to the end of its last sector (see capacity()), by moving the sections after the one that grows
    def capacity(self):
        """The bytes the room file can still grow by: the member sits in whole 2048 byte sectors and the archive's table can say
        any size inside the last sector (measured: a member given one byte less boots, one given a sector more doesn't)"""
        return (-len(self.original)) % 2048 + (len(self.original) - len(self.data))

    def _replace_section(self, index, content):
        """Section `index`'s bytes replaced by `content` (any length), the later sections moved after it, the header's offsets
        and sizes rewritten; the file's size changes by the difference"""
        table = list(self.sections)
        old_offset, old_size = table[index]
        delta = len(content) - old_size
        if delta > self.capacity():
            raise ValueError(f"the room has {self.capacity()} bytes to grow by, this needs {delta}")
        data = bytearray(self.data[:old_offset]) + content + self.data[old_offset + old_size:]
        for i in range(SECTIONS - 1):
            offset, size = table[i]
            if i == index:
                table[i] = (offset, len(content))
            elif offset >= old_offset + old_size and (size or offset > old_offset):
                table[i] = (offset + delta, size)
        for i, (offset, size) in enumerate(table[:SECTIONS - 1]):
            struct.pack_into("<ii", data, 8 * i, offset, size)
        self.data = data
        self.sections = [struct.unpack_from("<ii", data, 8 * i) for i in range(SECTIONS)]
        self._reparse()

    def add_area(self, like, quad=None, height=None, fields=None):
        """A new trigger area at the end: a copy of area `like`'s record with the given changes; returns its number. The
        section grows by the record and its offset (180 bytes). A new door (a copy of a door, tag 0) has to keep a door number the room
        already has: the game ties a door to the room model's door object by that number, and a number with no object crashes the
        game on loading the room (measured); a second area with the same number shares the object and works"""
        offset, size = self.sections[10]
        if self.areas[like].tag == 0 and fields and 12 in fields:
            numbers = {a.fields[0] for a in self.areas if a.tag == 0}
            if fields[12] not in numbers:
                raise ValueError(f"a new door can only take a door number the room has ({sorted(numbers)}), not {fields[12]}: no door object")
        base = self._area_base(like)
        record = bytes(self.data[base:base + AREA_SIZE])
        tag, count = struct.unpack_from("<ii", self.data, offset)
        old = struct.unpack_from(f"<{count}i", self.data, offset + 8)
        header = struct.pack("<ii", tag, count + 1) + struct.pack(f"<{count}i", *[o + 4 for o in old]) + struct.pack("<i", size + 4)
        content = header + bytes(self.data[offset + 8 + 4 * count:offset + size]) + record
        self._replace_section(10, content)
        index = len(self.areas) - 1
        self.set_area(index, quad=quad, height=height, fields=fields)
        return index

    def add_camera(self, like, **changes):
        """A new camera at the end: a copy of camera `like` with the changes (set_camera's: eye, target, fov, box); returns its
        number. The camera section grows by 324 bytes. The copy keeps the fog record the original uses (the record's int 61 is an
        index into the fog section, which has a record for each distinct fog, not for each camera)"""
        base = self._camera_base(like)
        record = bytes(self.data[base:base + CAMERA_SIZE])
        offset, size = self.sections[0]
        tag, count = struct.unpack_from("<ii", self.data, offset)
        old = struct.unpack_from(f"<{count}i", self.data, offset + 8)
        header = struct.pack("<ii", tag, count + 1) + struct.pack(f"<{count}i", *[o + 4 for o in old]) + struct.pack("<i", size + 4)
        self._replace_section(0, header + bytes(self.data[offset + 8 + 4 * count:offset + size]) + record)
        index = len(self.cameras) - 1
        if changes:
            self.set_camera(index, **changes)
        return index

    def _reparse(self):
        offset = self.sections[0][0]
        tag, count = struct.unpack_from("<ii", self.data, offset)
        offsets = struct.unpack_from(f"<{count}i", self.data, offset + 8)
        self.camera_offsets = offsets
        self.cameras = [Camera(bytes(self.data[offset + o:offset + o + CAMERA_SIZE])) for o in offsets]
        self.areas = self._areas()

    def changes(self):
        """[(offset in the file, the new bytes)] of the runs of bytes that differ from the file as read"""
        runs = []
        start = None
        if len(self.data) != len(self.original):
            # sections moved: everything from the first difference to the new end
            first = next((i for i, (a, b) in enumerate(zip(self.original, self.data)) if a != b), min(len(self.original), len(self.data)))
            return [(first, bytes(self.data[first:]))]
        for i, (a, b) in enumerate(zip(self.original, self.data)):
            if a != b and start is None:
                start = i
            elif a == b and start is not None:
                runs.append((start, bytes(self.data[start:i])))
                start = None
        if start is not None:
            runs.append((start, bytes(self.data[start:])))
        return runs

    def check(self):
        """The problems found (an empty list when the sections tile the file and the cameras fit)"""
        problems = []
        at = self.sections[0][0]
        for index, (offset, size) in enumerate(self.sections[:SECTIONS - 1]):
            if size and offset != at:
                problems.append(f"section {index} starts at {offset}, not where the one before ends ({at})")
            if size:
                at = offset + size
        if at != len(self.data):
            problems.append(f"the sections end at {at}, the file at {len(self.data)}")
        base = self.sections[0][0]
        for index, offset in enumerate(self.camera_offsets):
            if offset < 0 or base + offset + CAMERA_SIZE > base + self.sections[0][1]:
                problems.append(f"camera {index} at {offset} leaves the camera section")
        return problems


def rooms(game):
    """[(archive name, member name, Room)] of every room on the disc"""
    disc = archive.Archive(game)
    found = []
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            node = disc.open(dat)
        except FileNotFoundError:
            continue
        for name, sub in node.members():
            if not name.lower().endswith(".afs") or name == "romdata.afs":
                continue
            for member, room in sub.members():
                if member.lower().endswith(".rdt"):
                    found.append((name, member, room))
    return found


def main(argv):
    if len(argv) < 3 or argv[2] not in ("check", "cams", "areas", "doors"):
        print(__doc__)
        return 1
    game = argv[1]
    if argv[2] == "check":
        count = bad = cameras = 0
        for pack, member, node in rooms(game):
            count += 1
            try:
                room = Room(node.read())
                problems = room.check()
                cameras += len(room.cameras)
            except (ValueError, struct.error) as error:
                problems = [str(error)]
            if problems:
                bad += 1
                print(f"{pack}/{member}: {'; '.join(problems)}")
        print(f"{game}: {count} rooms, {cameras} cameras, {bad} with problems")
        return 1 if bad else 0
    if argv[2] == "doors":
        links = {}
        for pack, member, node in rooms(game):
            number = int(member[4:6])
            room = Room(node.read())
            links.setdefault(pack, {}).setdefault(number, set()).update(a.leads_to for a in room.areas if a.tag == 0)
        for pack, rooms_of in sorted(links.items()):
            print(pack, {n: sorted(d) for n, d in sorted(rooms_of.items())})
        return 0
    wanted = argv[3].lower().removesuffix(".rdt") + ".rdt"
    for pack, member, node in rooms(game):
        if member.lower() == wanted:
            room = Room(node.read())
            if argv[2] == "areas":
                print(f"{pack}/{member}: {len(room.areas)} trigger areas")
                for index, area in enumerate(room.areas):
                    extra = f" leads to room {area.leads_to}" if area.leads_to is not None else ""
                    if area.tag == 0:
                        extra += f" (its door {area.other_door}, door number {area.door_number}, facing {area.facing})"
                    print(f"  {index}: tag {area.tag} subtype {area.subtype} quad {area.quad} height {area.height}{extra}")
                return 0
            print(f"{pack}/{member}: {len(room.cameras)} cameras")
            for index, cam in enumerate(room.cameras):
                print(f"  {index}: type {cam.kind} fov {cam.fov[0]:g}/{cam.fov[1]:g} eye {cam.eye} target {cam.target}")
            return 0
    print(f"{wanted}: not on this disc")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))


class Node:
    """An entity record of a room's .sgl block (64 bytes): byte 0 active (1), byte 1 a kind (1 to 4 in the zoo's first room), a halfword id at +2,
    a position (x, y, z floats at +4, +8, +12; the y of some kinds isn't a float) and 12 more words, kept as they are (neighbour numbers and
    costs, from their shape). What the game does with them: a function in the single-player overlay (single_007d70e0) walks the room's records,
    takes the ones with a given id (or any) and finds the nearest to a position (within 50), and the partners' AI code (single_007e3560) calls it:
    they are the AI's navigation nodes"""

    SIZE = 64

    def __init__(self, record, offset):
        self.offset = offset
        self.active, self.kind, self.id = struct.unpack_from("<BBH", record, 0)
        self.position = struct.unpack_from("<3f", record, 4)
        self.words = struct.unpack_from("<12i", record, 16)


class Sgl:
    """A scenario's .sgl (r0xx.sgl in the pack): the rooms' scripts and entity tables as the game runs them. Header: a magic word, a count, the offset
    of an index (5728 in File 2's r040.sgl), ...; the index is a count then (room id, offset of the room's block, 0) for each room, the id being
    the room number with the variant in the high half (65540: room 4, variant 1); a room's block runs to the next block's offset and holds, from
    its start, two offsets (5232 and 5752 for room 3), a count and the room's entity records, then the script, the same words as the room file's
    section 1 (found whole in the pack's .sgl for 249 of File 2's 271 rooms and 164 of File 1's 166). Measured in the zoo's first room: the game
    runs THIS copy: changing a SetExamine's message number here changed the text on screen, changing the room file's copy did not."""

    def __init__(self, data):
        self.data = data
        header = struct.unpack_from("<4I", data, 0)
        index = header[2]
        count = struct.unpack_from("<I", data, index)[0]
        self.index = [struct.unpack_from("<3I", data, index + 4 + 12 * i) for i in range(count)]
        self.starts = sorted(offset for _, offset, _ in self.index)

    def block(self, room_id):
        """(start, end) in bytes of the room's block, or None"""
        for found, offset, _ in self.index:
            if found == room_id:
                later = [o for o in self.starts if o > offset]
                return offset, (later[0] if later else len(self.data))
        return None

    def nodes(self, room_id):
        """[Node] of the room's entity records (the variant's block first, then the room's), each with its byte offset in the file"""
        for candidate in (room_id, room_id & 0xFFFF):
            block = self.block(candidate)
            if block is None:
                continue
            start = block[0]
            count = struct.unpack_from("<I", self.data, start + 8)[0]
            if not 0 < count < 1000 or start + 12 + count * Node.SIZE > block[1]:
                continue
            return [Node(self.data[start + 12 + i * Node.SIZE:start + 12 + (i + 1) * Node.SIZE], start + 12 + i * Node.SIZE) for i in range(count)]
        return []

    def find_script(self, room_id, words):
        """The byte offset of the room's script (`words`, the room file's section 1) in its block, or None; the variant's own block first
        and then the room's (the variants without a block of their own)"""
        needle = struct.pack(f"<{len(words)}i", *words)
        for candidate in (room_id, room_id & 0xFFFF):
            block = self.block(candidate)
            if block is None:
                continue
            at = self.data.find(needle, block[0], block[1])
            if at >= 0:
                return at
        return None
