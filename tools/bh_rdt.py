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
# blue turned the whole picture blue), 9 to 11 the characters' tint as floats red, green, blue (1, 0, 0 turned the characters red).
# Not tried: 1 (a float, -200 in the room looked at), 2 (a float, 7000: a distance?), 3 (0x222222), 5 to 8 (0xFF808080, then three numbers).
FOG_FIELDS = {4: "scene colour", 9: "character tint red", 10: "character tint green", 11: "character tint blue"}
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
        return [Area(bytes(self.data[offset + at:offset + at + AREA_SIZE])) for at in offsets]

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
        offset, size = self.sections[13]
        tag, count = struct.unpack_from("<ii", self.data, offset)
        if not 0 <= camera < count:
            raise IndexError(f"fog {camera}: the room has {count} (one for each camera)")
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

    def _reparse(self):
        offset = self.sections[0][0]
        tag, count = struct.unpack_from("<ii", self.data, offset)
        offsets = struct.unpack_from(f"<{count}i", self.data, offset + 8)
        self.cameras = [Camera(bytes(self.data[offset + o:offset + o + CAMERA_SIZE])) for o in offsets]
        self.areas = self._areas()

    def changes(self):
        """[(offset in the file, the new bytes)] of the runs of bytes that differ from the file as read"""
        runs = []
        start = None
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
