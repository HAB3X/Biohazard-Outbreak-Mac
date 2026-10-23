#!/usr/bin/env python3
"""Reads the games' room files (.rdt, the members of NETBIO00.DAT's and NETBIO01.DAT's r0xx.afs): the header's sections and the
cameras.

    bh_rdt.py GAME check          every room on the disc: the sections tile the file and the cameras are plausible
    bh_rdt.py GAME cams ROOM      a room's cameras (ROOM like r0300100.rdt, or just r0300100)
    bh_rdt.py GAME areas ROOM     a room's trigger areas (doors, messages, desks ...)
    bh_rdt.py GAME doors          each scenario's doors as room -> rooms

The file: 32 sections as (offset, size) pairs of int32, then the sections' contents one after another (checked: the offsets
tile the file with no gaps on every room of both discs). Section 0 holds the cameras: an int32 tag (1), the camera count and
that many offsets (from the section's start) to 320 byte camera records. The layout was learned from a community 010 Editor
template (Dchaps' rdt_temp.bt: reference only, no licence) and then checked against the running game: File 2's classic
camera's eye and target in the zoo's first room are camera 2's of r0300100.rdt. Section 10 holds the trigger areas (same source: Dchaps' NETBIO_AOT.ms): an int32 tag, a count and that many offsets, to records
of 176 bytes (every one of the 4281 in File 2): tag, subtype, a quad as four (x, z) corners (ints 2..9), the height range (ints
10 and 11) and then data that depends on the tag. A tag 0 record is a door: int 12 its number in the room, int 13 the number of
the room it leads to (the yy of r0yyyy00.rdt: of 370 door links in File 2, 359 are returned by the room they lead to). The
other tags (1 3 4 5 6 7 10 11 12 13) are kept as they are.
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
        """The room number a door (tag 0) leads to"""
        return self.fields[1] if self.tag == 0 else None


class Room:
    def __init__(self, data):
        self.data = data
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
        return [Area(self.data[offset + at:offset + at + AREA_SIZE]) for at in offsets]

    def check(self):
        """The problems found (an empty list when the sections tile the file and the cameras fit)"""
        problems = []
        at = self.sections[0][0]
        for index, (offset, size) in enumerate(self.sections):
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
                    extra = f" leads to room {area.leads_to}" if area.tag == 0 else ""
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
