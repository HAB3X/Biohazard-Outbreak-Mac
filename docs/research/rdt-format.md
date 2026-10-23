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

**Editing** (`tools/bh_rdt_edit.py GAME SCENARIO`, `tools/bh_room_patch.py`, `Room.set_camera` / `set_area` in `tools/bh_rdt.py`): the map page with tools. A page on
127.0.0.1 where a click selects an area or a camera, dragging moves an area (corners or the whole), a camera's eye and aim and a
switch box's corners (the other numbers are typed), and Save writes `work/room-edits-GAME.json` and `work/rooms-GAME.patch`, which
`run.sh` (and the app's `rooms.patch` in its settings folder) put over the disc. Edits overwrite numbers in place: the file's size
never changes (the archive can't grow). Checked in the running game (File 2, the zoo's first room, r0400300):
- camera 2's eye changed in the file from (12119, 116, 11709) to (12319, 216, 11709): the game's camera read (12319, 216, 11709) and
  the picture moved (the gate seen from higher and to the right); the aim was unchanged;
- door 0 (to room 1) moved in the file from (13190, 14700) to (11970, 11550): standing at the old spot no longer left room 3 and the
  new spot did (to room 1); and again from a drag in the editor page and its Save (the door at (12530, 13700)).
Not done: editing the lights, fog and scripts (not decoded), the other fields' meaning, new areas or cameras (a room has what it has),
and undoing a saved patch other than by deleting it. An edited door's arrival spots (and the door it joins) are not rechecked.

**Fog** (section 13, `Room.fog` / `set_fog`, the editor's colour pickers): a record of 24 numbers for each camera. Measured in the zoo's first room by
editing camera 2's record: number 4 is the scene's colour (bytes blue, green, red, alpha; set to blue the whole picture turned blue,
lights and walls included) and numbers 9 to 11 are the characters' tint as floats (1, 0, 0 turned every character red; the scene
stayed as it was). Numbers 1, 2, 3 and 5 to 8 weren't tried (a float -200, a float 7000, 0x222222 and 0xFF808080 are in them).
The editor's camera panel has a colour box for each of the two; the same edit made there, saved and run, gave the same picture.

**Lights** (section 11: not editable yet). The section is 664 bytes in the zoo's first room and holds a small header (4, 2, then 16 and 548),
a block of the same kind of numbers as a fog record (-200.0, 7200.0, 0x222222, 0xFF808080, 11141135 ...) and then eight 52 byte
records that look like point lights: x, y, z (floats in the room's coordinates: 11527, 276, 11089 and seven more, near the gate's
lamps), 0.001, a range (580, 394, 440, 323, 390, 510, 623, 814), a colour (red, green, blue floats from 0.4, 0.44, 0.37) and
another colour (0.12) with a pad float after each. Overwriting the first colour of all eight with red (1, 0, 0) changed nothing
visible in the game's picture there (mean difference 0.004 of 255), so this reading says what the numbers look like, not that
they light that view. The standalone R0400300.LIG (128 bytes) holds almost nothing (4, 1, 12, 1). The game's runtime "character
light" block (File 2 0x3134A8) has a direction (-0.075, 0.788, -0.611) and the same constant 11141135 as fog number 6; where its
numbers come from in the files isn't established.

**Fog, the rest** (swept in the zoo's first room, camera 2, each number zeroed and changed, the picture compared with the unedited one; the game's own
frame to frame noise is 0.5 to 1 of 255): number 3 is a colour added to the whole picture (bytes blue, green, red: 0x000000FF gave +32
in blue only, 0x0000FF00 in green only, 0x00FF0000 in red only; 0 gave -5 in each; the room has 0x222222); number 2 is a float distance
(7000: zero made the picture brighter by 9, 12 and 18 in red, green and blue, four times it made it a little darker: the fog's reach);
number 1 is a float that lightens a little when raised (-200 to -800). Numbers 0, 5 to 8 and 12 to 23 showed no change in that view (they
may matter in other rooms, cameras or the shadows). The editor's camera panel has the ambient colour and the fog distance.

**Adding areas and cameras** (`Room.add_area`, `add_camera`, `new_areas` / `new_cameras` in the edits, "Add ... like this" in the editor). A room file can't
change the number of sectors the archive gives it, but it can grow to the end of its last sector (`Room.capacity()`: a median of
808 bytes in File 1's rooms and 1008 in File 2's; some have under 200 and a few none). A new area is a copy of an existing one at
the end of section 10 (180 bytes with its offset), a new camera a copy of a camera at the end of section 0 (324 bytes); the sections
after the one that grows move later, the header's offsets are rewritten, and the member's size is written in both places the
archive keeps it (`bh_room_patch.size_entries`). Measured in the game (File 2):
- a section made 180 bytes longer with nothing in it: the room loaded and played;
- a new exit (tag 1, a copy of room 4's exit) in room 4: standing in it led to room 3, as the copied exit does;
- a new camera in room 2 (a copy of a following camera, with its own box): the game switched to it standing in its box (the eye read
  from memory changed; it follows the player, as the copy's type 2 does), where without it the camera stayed;
- a new DOOR (tag 0) with door number 2, in a room with doors 0 and 1, crashed the game on loading the room (a wild address): the
  doors are tied to door objects in the room's model, so a door needs its object. Exits (tag 1) and the other areas need none.
  Fixed: a new door (a copy of door 1 of room 2, leads to room 1 instead of 3, at a new spot, keeping door number 1) worked in the game: standing in it led to room 1, no crash. So a new door shares an existing
  door's number (and its object); `Room.add_area` refuses a door number the room doesn't have.
A camera's fog record isn't copied: a camera's int 61 is the number of the fog record it uses (the fog section has a record for each
distinct fog: 2 in room 2 for its 5 cameras, 11 in room 3 for its 11), and the copy keeps its original's.

**Room models in 3D and editing the objects** (`tools/bh_nbd.py` `bone_records` / `set_bone`, `models` in the edits, the editor's "Open the 3D view"). A room's .nbd stage is a skeleton
(AHI) with an object (mesh) on each bone, the mesh i on bone i + 2. A bone is its parent's number, a scale, a rotation (Euler angles
in radians) and an offset from the parent, 256 byte records edited in place, so the file's size never changes. Measured in the zoo's
first room (File 2): object 48 (bone 50) is the right lamp's cage. Scaled to 0.0001 it vanished and the bulb stayed; its offset's y
raised by 300 (from the editor's "y+100" three times, saved, run) put the cage in the air above the lamp in the picture. The 3D view is a WebGL2 page of
the model (flat shaded, the bones' chain applied as the game's own placement does), the room's areas as boxes and its cameras
as crosses and lines; a click picks an object (an off-screen picture of object numbers), the panel moves it along world x, y or z (the
move is turned into the parent bone's own space), turns, scales or hides it. Not done: textures (the room's pictures are in the scenario's
.sld files by the TID chunk), editing the meshes' vertices, and moving an object's animation (doors move by other data).

**Sound zones and sound sources** (section 16; `Room.sound_zones`, `sound_points`, `set_sound_zone`, `set_sound_point`; drawn and editable in the plan and the 3D view). A header
(the number of zones, the number of sources, then their offsets), then the zones (104 bytes: tag 1, eight corners as the lower four and
the upper four (x, y, z ints), and a sound number; 51 rooms of File 2 have a few of 124 bytes, not looked at) and the sources (28 or 48
bytes: tag, a kind, a position as three floats, then more numbers: not decoded). Measured in the zoo's first room by giving a zone the number
0x1234 and reading the game's memory with the player standing inside it and outside: the number is what the game keeps for the player in the
halfword at +0x176 of her record (0x1234 inside, 1 outside, the surrounding zone's), so a zone's number is the floor sound at her feet
(the footsteps' kind). The sounds the numbers stand for (1, 8, 12 and 16648 are in the room looked at) weren't mapped to anything audible: comparing audio dumps
of two runs doesn't work (two runs of the same game differ about as much as an edited one). The sources' kind and the rest of their
numbers are undecoded; moving a source and its effect haven't been heard.

**Character lights** (section 11, now decoded in part; `Room.lights`, `set_light`, the editor's light markers with their range as a circle). The section holds 52 byte
records of 13 floats found by their shape (`bh_rdt.Light`): position x, y, z, an attenuation, a range, a colour (red, green, blue), a pad, a
second colour and a pad: 1849 in File 2's rooms, 99% of them within 3000 of their room's areas. They light the CHARACTERS only (the picture's
ground and sky never changed): in the zoo's first room, with the lights moved next to the characters their colours went up by about 1 in each
channel, a red of 4 in the first colour gave +1.7 in red only, the second colour's red gave more (+2.5), and an attenuation of 0 or a range
of 4 switched a light off; the effects are small because the room's lights are dim and a character takes a few. The section also holds
colour triples before the lights: the one at byte 132 (0.51, 0.56, 0.46 in the room looked at) and the one at 148 (0.1, 0.1, 0.1) raised the
characters' red by 1.1 and 0.7 when set to red 4; the ones at 88 and 104 did nothing in that view. The header (4, 2, 16, 548) and the
block-like numbers before them (fog-like: -200, 7200, 0x222222 ...) aren't decoded, and only the first seven of the eight light-like
records in that room fit the shape (the eighth sits where a second block starts).

**Scripts** (section 1; `Room.script_words`, `script_header`, `script_instructions`, `set_script_word`, the editor's "Script" list; NOT decoded). What is known, from the data
and from watching the game read it:
- The section is 16 header words then the records. Word 0 is where a tail table starts (1416 in the zoo's first room), word 1 where a 32 byte
  table of bytes starts (the game's function at 0x550790 returns the byte at an index below 32 from it: read through the script header's copy
  the game makes in RAM at load), and words 8 and 9 say where two blocks of records start (64 and 796 there).
- The records: a word whose top byte is 1, 2 or 3 starts a record of 2, 4 or 8 words; the low 24 bits are an operation number
  (117 distinct ones in File 2's 271 scripts; the commonest are 0xC (1412 times), 0xB (1097), 0x55 (1091), 0xE6, 0xB1, 0xED, 0xE3 ...). The
  gaps between one record's first word and the next's follow that width in most of the 14136 records (the exceptions are records followed
  by a list of numbers), but the rule is read off the data, not off the game's reader.
- Read at load: the copier (0x107ab8) and a function at 0x1dd190 take the first 16 words, 0x5975c0 reads word 0 and 0x550790 the
  byte table; the records themselves were NOT read while the room loaded, nor while standing in and pressing the action key in every area of the
  zoo's first room (a read watch on the script's copy in RAM in a trace build; a protected page of it never faulted). So these scripts run on
  other events (a pick-up, a fight, a timer, the scenario's own script) that I haven't made happen, and the interpreter wasn't found.
- Tried to read a meaning: a record 0x55 holds a number and a message number (the zoo's first room: 2 -> 127, 3 -> 128, 4 -> 122,
  5 -> 130, 6 -> 131, 7 -> 126, which are the stage's messages for the blinds, a newspaper, a service gate, a trailer ... in
  `r040.bin`'s numbering, the records of `translations/file2/rooms.tsv`); changing that 122 to 101, 105 and 111 changed
  nothing in what the area at (11630, 13225) showed ("The high wall goes on"), so what the record is for isn't shown by that.
The editor lists the records with their numbers editable (the file's size stays the same) and saves the changed words: a way to experiment,
not a decoded script language.

**Scripts, a longer try**: with a read watch (a trace build; the instrumentation was reverted) on the zoo's first room's script copy in RAM, four minutes of the
game played by a script (teleports into every area, the action key three times in each, the stick, room changes back and forth)
read only the header and the first two bytes of the tail table (a function at 0x550700, from 0x690e30): the 1300 bytes of records in
between were not read once. So the records don't run for anything those inputs cause; they are most likely consulted by the
scenario's own event script at points of its story (keys, bosses, timers) that this didn't reach.

**Scripts, corrected**: the game doesn't run the room file's section 1; it runs the same script from the pack's `.sgl` (docs/research/script-ops.md). Every claim
above that the section-1 records "weren't read" is true of that copy. `bh_room_patch.py` writes script word edits into both.

**The .sgl's entity records (AI navigation nodes; `bh_rdt.Sgl.nodes`, `Node`, the editor's "AI node" squares, `nodes` in the edits)**. A room's block in the pack's
`.sgl` starts with two offsets (the script's, and another), a count (29 in the zoo's first room) and then that many records of 64 bytes from byte 12: byte 0 active
(1 in every one of the 5634 in File 2 and 3279 in File 1), byte 1 a kind (1 to 4; a few 0), a halfword id at +2, a position as three floats at +4, +8, +12 and
twelve more words (neighbour numbers and costs by their shape: not decoded; for some kinds the y word isn't a float). All 5634 positions in File 2 and all 3279 in File 1
lie within 3000 of their room's trigger areas. What they are: the game's single-player overlay function `single_007d70e0` walks a room's records, takes the ones with a given id (or any), finds the nearest to a position within 50 and returns it, and the partner AI code
(`single_007e3560`) calls it. Measured in the zoo's first room (File 2, the game's own "Original" partner AI, the player running and calling the partners as in `tools/ai_scenario.py`'s zoo-commands, 72
samples of the two partners' positions a run): unedited, the partners wandered over the room (x 11758 to 14513, z 11228 to 15115; one sample in 72 within 200 of the point (12500, 12500)); with all 29 nodes moved to (12500, 84, 12500) every one of 72 samples was within 200 of that point
(x 12442 to 12557, z 12438 to 12565); with all moved 3000 off to the side the partners stopped roaming and stood still. So the nodes are where the AI sends the partners: its navigation / roaming nodes. Moving them 200 units
sideways gave no difference the samples could show (their count near a node is too dense to tell). Not decoded: the kinds (1 to 4: 846, 1770, 1555 and 1461 of File 2's) and what the twelve words are for.
What was checked: the saved edit reached the game's memory (a node's position 13175 -> 13250 and a word 0 -> -254 read back from the running
game's copy at 0x80F28C).

## Textures in the 3D view (verified visually, room 3 of File 2)

- NBD UVs are array 10, per-mesh materials array 5, per-strip materials array 6; the TID chunk maps a material to a member index in the room pack.
- A texture is member N of the pack, decoded with `bh_sld.unpack` then `tm2.pictures()[0].image()`; the editor serves it at `/tex?index=N`.
- Wrap mode is REPEAT; alpha below 0.35 is discarded. A few floor strips look stretched, which comes from their source UVs, not the decoder.


## Area tags 10, 12 and 13 (read from the code and the data, no live test)

`fields[n]` below is the nth word after the area's 48 byte head (runtime offset +64 + 4 n in the game's area list).

- **Tag 13 (subtype 6; 8 in File 1, 7 in File 2)** is a marked spot: `fields[0]` an id, `fields[4..6]` a position as three floats and `fields[7]` a facing (65536 to a turn: 16384, 32768, -16384 ...). The
  position is next to the record's own quad: 4 of the 15 records have it inside, the rest within about 120 units outside (measured on all 15), so the quad is a trigger and the spot is where a character is put or stands. The script ops 0xF6 and its twin (game_00671620) switch these on: they walk the
  area list for tag 13 whose `fields[1]` equals the script's number, set the active byte (+60) and store the second word in the halfword at +98.
- **Tag 12 (subtype 3; 7 in File 1, 26 in File 2)** is switched on by the script ops 0x157 and 0x167 (game_006734c0, game_00673ba0): the loop takes the tag 12 areas whose `fields[0]` is 0 and `fields[1]` the script's number, sets the active byte, then
  looks for a character (a record of the character list, the team byte at +3009 or the player slot's +2992) and tests it against the area with game_005ae910. So a tag 12 is a zone a script waits on for a character to stand in it.
- **Tag 10 (7 in all)** has no fields at all in any record; no handler for it was found among the ops that walk the list.
- **Tag 6** (ladders by the rooms' pictures: subtypes 0, 1, 3, with `fields[3]` 1 to 6) still unproven.

## File 1 live check (2026-10-23)

A disc patch halving the field of view of every camera in every File 1 room (`bh_rdt.Room.set_camera`, patch from `bh_patchfile.write_patch`) was played from the title into the first scenario's bar: the picture is zoomed in against the unedited
one, so File 1 reads the same room files and the edit path works there too (the same code as File 2; rooms with a different fog count than cameras raise in `set_fog` and are skipped). Fog was tried the same way with the scene colour
floats 9 to 11 set to red in every camera's record and showed no change in the bar: in File 1 those fields are not the visible colour (not looked into further).

## Editing a model's vertices (verified in the game, File 2 zoo)

A mesh's positions are plain floats (x, y, z) in the mesh's own coordinates (`bh_nbd.Mesh.positions_at` is where they start in the file), so a vertex edit is written over them in place and the file's size doesn't change
(`bh_nbd.set_vertex`; the edits JSON key `models/<file>/vertices/<mesh>/<vertex>: [x, y, z]`). In the 3D view tick "edit vertices" for the selected object, click one of its green points (or type its number) and drag it in the
plane facing the camera, or type its coordinates. Measured: the zoo's gate lamp (r0400300.nbd, object 48) with every second vertex raised by 900 shows a tall spike over the gate in the running game; the unedited run doesn't.
Only positions move: triangles can't be added or removed (a bigger mesh would need a bigger file, which the archive can't give, see tools/bh_character_patch.py).
