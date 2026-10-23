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
  0 (436 doors), 1 (384), 3 (270), 6 (126), 4 (117), 7 (48), 2 (27), 12 (26), 13 (7), 10 (3); the wiki's guess for 5 is messages
  and 7 desks or other things to examine, not checked here; the data after the door fields (the spawn position and
  facing in the next room?) is not decoded.

**Sections** (the header's 32 entries; the sizes are those of the zoo's first room): 0 cameras, 1 a script (668 to 3128 bytes),
2 and 3 sound banks (144896 bytes for 2 in every room looked at), 7, 10 doors and trigger areas, 11 lights, 13 fog (header, then
96 byte records per camera), 15, 16 sound zones. 4 to 6, 8, 9, 12, 14 and 17 to 31 are empty in the rooms looked at.

**Not known**: the script's opcodes (section 1), the trigger areas other than doors (10), a door's destination spot, the light records (11), what 7 and 15
hold, the camera types (1, 2, 7), the camera record's remaining fields, and how the game picks a camera from the boxes.

**For an editor**: moving or adding cameras, doors, lights and fog is a change inside the room file; the file's size can't change
(the archive's members can't grow: `tools/bh_character_patch.py`), so every edit has to fit the section it is in.
