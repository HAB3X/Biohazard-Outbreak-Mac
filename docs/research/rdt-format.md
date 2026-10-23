# Room files (.rdt)

Read by `tools/bh_rdt.py` (`bh_rdt.py file2 check`, `bh_rdt.py file2 cams r0300100`). The layout came from a community 010 Editor
template (Dchaps' `rdt_temp.bt`, in `~/Documents/outbreak-research/Outbreak-PS2-`: no licence, reference only) and was checked
against the discs and the running game.

**Checked**
- File 2: all 271 rooms (2068 cameras) have 32 header sections that tile the file; File 1: 165 of 166 rooms do. The odd one is
  `r002.afs/r0023100.rdt`, whose later header entries look like another layout (not looked at).
- Camera record, 320 bytes: File 2's classic camera in the zoo's first room, read from memory (`0x313548` eye, `0x313554` target,
  room number `0x3137BC`), is camera 2 of `r030.afs/r0300100.rdt` to the unit: eye (12119, 116, 11709), target
  (11907, 177, 11260). Field of view (floats, ints 7 and 8) and the box (ints 9..32) are read off the file only.

- Trigger areas (section 10, `bh_rdt.py GAME areas ROOM`): every one of the 4281 records in File 2 is 176 bytes. A record is
  tag, subtype, four (x, z) corners, a height range (ints 10 and 11), then fields by tag. For a door (tag 0) int 13 is the
  number of the room it leads to: 359 of File 2's 370 door links are returned by the room they lead to, and Wild Things' room
  graph reads right against the room names in the community wiki (1 Elephant Restaurant, 2 Back Alley and 3 In front of it
  link to each other; 4 South Concourse links to 5, 6 and 23 Service Road). Tags seen in File 2: 5 (2278 areas), 11 (559),
  0 (436 doors), 1 (384), 3 (270), 6 (126), 4 (117), 7 (48), 2 (27), 12 (26), 13 (7), 10 (3); the wiki's guess for tag 7 (desks)
  is not checked here; the data after the door fields (the spawn position and
  facing in the next room?) is decoded as below.
- **Doors, tag 0** (`Area.door_number`, `leads_to`, `other_door`, `facing`, `arrival`): int 12 the door's number in its room, int 13
  the room it leads to, int 14 the number of the door it joins there (File 2: 380 of 381 doors in variant 00 rooms are returned
  by that door, File 1: 257 of 262; the others are one way), int 18 the facing a character arriving through it takes (65536 a
  full turn, the direction is (sin, cos) of it over x and z: 0 faces +z, 16384 faces +x). Measured live in the zoo's first
  street: the player teleported into door 0 of room 3 arrived in room 1 with the heading of room 1's door 1 (16383) and 60 units
  along that facing from its centre (15 to the side); back through that door, heading 49152 and 65 units along it (20 to the
  side), so the arrival is "about 60 to 65 in front of the destination door", not an exact spot.
- **Exits, tag 1**: int 13 the room it leads to, ints 15..18 a point (floats x, y, z) and a facing, ints 19..22 another point
  and the facing the character gets, int 24 a distance (150 in the one measured). The character arrives at the second point's
  x and z pushed the distance along its facing: predicted (11293.9, 10443.9) and read (11293.9, 10443.9) from the game for the
  zoo's first room exit to room 4 (facing 40960).
- **What the other tags do**, from standing in 33 areas of seven of Wild Things' rooms (1, 2, 3, 7, 8, 12 and 23) and pressing the
  action key, with a picture of each looked at (the pictures aren't kept: they are the game's):
  - tag 5, subtype 1: a message when you stand in it ("The gate is closed with a chain", room 3). Its fields are all zero: the text is
    chosen elsewhere, probably by the scenario's script.
  - tag 5, subtype 0: a spot to act on. With the action key it shows a description ("A big table and chairs / An Asian-style
    design" in room 1, "The high wall goes on" in room 2) or offers an item ("Bug Spray - take it?" and "Handgun - take it?" in room 3,
    whose records are int 12 = 20 and 21 and three floats). Some stand in the open and show nothing (room 1, area 6).
  - tag 3, subtype 0: a lock ("A number-lock key... Unlock it? Yes / No", the zoo gate, room 3); subtype 1: an item to take
    ("First Aid Spray - take it?", int 12 = 83).
  - tag 4, subtype 2: on the same quad as that gate's lock and showing the same prompt (probably the key or item check that goes with it).
  - tag 11, subtype 1: a message shown as you stand in it ("A trailer, wrecked it seems, is blocking the road...", room 3; "A vintage
    cash register counter", room 1), in pairs of one number with 0 and 1 and values 200 to 209 (the message numbers?).
  - tag 2, subtype 4: next to doors that open and close inside the room, with no room change (the offices' doors in rooms 7 and
    8, where the door swings open as you stand in it; "Shall we try it..." beside the restroom door in room 1).
  - tag 4, subtype 2 again: "A raccoon ornament / There's a hole at the front for a medal" with a counter on screen (room 8's desk):
    a spot where an item is used or examined, as at the zoo gate.
  - tag 7, subtype 0: an item sparkle on a desk (the copier in room 8); in two spots of room 7 nothing showed (the item may be
    taken or hidden until a script puts it there).
  - tag 6 (subtypes 0, 1, 3), rooms 12 and 23: nothing happened when stood in, and both rooms show a ladder in the picture: probably
    climbing spots (not confirmed).
  - tag 13, subtype 6 (room 7): nothing showed.
  - Not looked at: tags 10 and 12 (the zoo rooms reached have none: 12 is in room 27, the Front Gate, which the survey couldn't
    reach), and what tags 6 and 13 do. Rooms reached: 1, 2, 3, 7, 8, 12 and 23 (a door in a later room didn't take the teleport).
  - None of the 33 tried changed the room; only tag 0 doors and the tag 1 exit did.

**Sections** (the header's 32 entries; the sizes are those of the zoo's first room): 0 cameras, 1 a script (668 to 3128 bytes),
2 and 3 sound banks (144896 bytes for 2 in every room looked at), 7, 10 doors and trigger areas, 11 lights, 13 fog (header, then
96 byte records per camera), 15, 16 sound zones. 4 to 6, 8, 9, 12, 14 and 17 to 31 are empty in the rooms looked at.

**Not known**: the script's opcodes (section 1), what the trigger areas' fields mean for tags 2 to 7 and 11 (which text or item: the fields after the quad), what tags 6 and 13 do, tags 10 and 12 altogether, the light records (11), what 7 and 15
hold, the camera types (1, 2, 7), the camera record's remaining fields, and how the game picks a camera from the boxes.

**For an editor**: moving or adding cameras, doors, lights and fog is a change inside the room file; the file's size can't change
(the archive's members can't grow: `tools/bh_character_patch.py`), so every edit has to fit the section it is in.

**The room models under the map** (`tools/bh_nbd.py`): a room's .nbd has two AMO chunks (the stage and an effect) with a skeleton (AHI) each; the stage's mesh i sits on bone i + 2 and a bone's chain of offsets gives the mesh's place in the shared world (the root bone of r0400300 is at 12300, 500, 13000: inside the room's own coordinates). On File 2's r0400300, 99.3% of the stage's vertices land in the floor region the room's camera boxes give. The bones' rotations are Euler angles in radians (not quaternions); the order they apply in isn't established (the rooms looked at turn about one axis). The map draws floors (triangles whose highest corner is within 120 of the room's floor height, found as the lowest height with most flat area) as grey blocks and walls as outlines, in a plan; ceilings and triangles with an edge over 3000 are left out.

**The map** (`tools/bh_rdt_map.py GAME SCENARIO [OUT.html] [--names rooms.md] [--variants]`): one self-contained page per scenario, every
room in its shared world coordinates with its cameras' eyes and aims, switch boxes, doors, exits (with the arrival spot each leads
to) and the other areas coloured by tag; the wheel zooms, a drag pans, hovering lists a shape's fields. Written into `work/` (not
in the repository: the page holds the game's room data). Checked on File 2's Wild Things (20 rooms, 496 areas, 211 cameras): the
rooms form the zoo's layout, and a message area read in the page matches the record read from the file. Two things in the data
the page has to skip: areas whose four corners are all zero, and a camera whose eye is -2147483648 (room 23).
