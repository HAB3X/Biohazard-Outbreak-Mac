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

**Sections** (the header's 32 entries; the sizes are those of the zoo's first room): 0 cameras, 1 a script (668 to 3128 bytes),
2 and 3 sound banks (144896 bytes for 2 in every room looked at), 7, 10 doors and trigger areas, 11 lights, 13 fog (header, then
96 byte records per camera), 15, 16 sound zones. 4 to 6, 8, 9, 12, 14 and 17 to 31 are empty in the rooms looked at.

**Not known**: the script's opcodes (section 1), the doors' and trigger areas' records (10), the light records (11), what 7 and 15
hold, the camera types (1, 2, 7), the camera record's remaining fields, and how the game picks a camera from the boxes.

**For an editor**: moving or adding cameras, doors, lights and fog is a change inside the room file; the file's size can't change
(the archive's members can't grow: `tools/bh_character_patch.py`), so every edit has to fit the section it is in.
