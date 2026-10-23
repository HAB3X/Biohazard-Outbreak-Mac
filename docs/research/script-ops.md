# The script interpreter (File 2)

How it was found: a read watch in the runtime's trace build (`PS2_TRACE_RWATCH`, a debugging edit that was not committed) on the scenario
script's copy in RAM, with the game played by a script, named the functions that read it; the generated C of those functions
(`build/generated/file2_code/code_*.c`) was then read. `tools/bh_script_ops.py` summarises a handler mechanically (the words it loads from its
argument pointer, the fixed addresses it touches, the functions it calls). Addresses are File 2's, in the game overlay (named `game_` in the code).

## The interpreter

- `game_006d72b0(thread)` runs a script thread: a program counter for each thread (0x4911A0 + 4 * thread; 0x491291 is the running thread's
  number), a count of the records run (0x4911D8 + 4 * thread).
- A record's first word is `(class << 24) | op`; the record is `1 << class` words long (checked in this loop: the program counter moves on by
  `(1 << (word >> 24)) * 4` bytes). The handler is found at 0x74CCE0 + 8 * op: a function address and three flag bytes (+4, +5, +6; +5 non-zero
  skips the record while a mode variable is 2 or more); an op with no handler points at 0x66C200 (the default, a no-op).
  File 2 has 258 ops with a handler; the 116 used by the rooms' scripts all have one.
- The handler is called with a0 pointing at the words after the op word. It returns 0 to go on to the next record, 2 to say it has moved the
  counter itself (a skip), and the loop handles 10 and 13 as leaving. A block-skipping handler calls `game_0066be60`, which moves the counter on past
  the matching end of the block (it counts nested blocks by the ops 228, 229 and 10: the "end" markers in a script).
- Two flag banks: A at 0x48FFF0 and B at 0x490030, a bit for each flag number (number >> 5 selects the word).
- A list of the room's trigger areas at the address `gp - 25000` (a linked list, +40 the next): each runtime area has +48 its tag, +49 its subtype,
  +60 an "active" byte, +64 its field 12 (the id), +68 field 13, +72 field 14, +80 field 16, +84 field 17, +88 field 18, +96 .. +112 fields 20 .. 24 (the
  loader `game_005975c0` copies the area record's 176 bytes: tag, subtype, the quad, the heights to +32 and the 128 bytes of fields to +64). So the
  scripts switch areas on and off and change their fields while the game runs.

## The ops read in the code (all of these are in `bh_rdt.OPS`, shown in the editor's script list)

| op | name | words | what it does |
|---|---|---|---|
| 0x0C | SetFlag | type, flag, value | clears (value 0) or sets bank B's flag (type 0); other types go to the functions 0x6d7790 / 0x6d7630 |
| 0x0B | FlagOp | mode, number, byte | calls one of 0x5433f0, 0x543460, 0x6d76e0, 0x6d7580 by mode: sets or clears a flag in another bank |
| 0xE6 | IfFlagB | flag, value | block runs when bank B's flag equals value; else the block is skipped |
| 0xEC | IfFlagAB | flag, value | block runs when (A's flag and not B's) equals value |
| 0xE3 | IfGlobal | number, value | block runs when function 0x6d74a0(number) equals value |
| 0xED | IfGlobal2 | number, value | as above and function 0x6d7510 |
| 0xEF | IfGlobalB | number, value | block runs when 0x6d7510(number) equals value |
| 0xEE, 0x08, 0x113 | other ifs | | conditional blocks (they call the skip function); their tests aren't read |
| 0xB0 / 0xB1 | ShowObject / HideObject | object number | clears / sets bit 7 of the flag byte at 0x475080 + n (the room's objects: the models' meshes), n taken through the script's 32 byte table (header word 1) |
| 0xB5 | SetActorState | id, byte | stores the byte at +113 of every actor with that id |
| 0x55 | SetExamine | id, message, parameter | activates the examine areas (tag 5) with field 12 = id; their field 16 = the message number, field 22 = the parameter |
| 0x56 | ResetExamine | id | calls 0x59a7c0 for the examine areas with that id |
| 0x58 / 0x59 | EnableItem / DisableItem | id (, value) | activates / deactivates the item and lock areas (tag 3 subtypes 1 and 3, tag 4) with that id; tag 3's field 18 is set to the value |
| 0x5C | DisableExit | id | deactivates the exit areas (tag 1) with that id |
| 0x5F | SetDoorLock | id, value | the doors' (tag 0) and room doors' (tag 2) field 22 = value (the zoo's first room: door 1 gets 201, the stage's message 201 "locked with a simple lock") |
| 0x54 | SetDoor | id, number, byte | the doors' runtime fields |
| 0x104 / 0x105 | Activate / Deactivate | tag, id, value | turns the areas of that tag (0 to 13 by the types it tests) with that id on or off |
| 0xD0 | SpawnEnemy | kind, id, five words | allocates an enemy slot and copies five words (position and angles) into it |
| 0x45 | SetOption | flag, value, index, number | writes two small arrays at 0x491258 and 0x491268 |

Not read yet: the rest of the 258 (e.g. 0x20, 0x70, 0xF8, 0x135 and 0x131 are among the commonest). A mechanical summary of any one: `bh_script_ops.py file2 OPTABLE.json 0x20`.

## What it says about the rooms

- The tag 5 "examine" areas get their message number from the script's SetExamine, not from their own record (their fields are zero in the file); tag 11's field 14 held
  numbers 200 to 209, the stage's door messages ("locked from inside", ...). So the editor's script list is where a message area's text is chosen.
- A door's lock text is set by SetDoorLock (0x5F); the room script of the zoo's first room runs `SetDoorLock 1, 201`.

## Still open

- When a room's script (section 1) runs. The interpreter reads the scenario's .evb (the `r040.evb`: 15 KB, header [128, 164, 5224 ... 14372 ...]) continually; the room script's records were not
  read in four minutes of play in the zoo's first room (see rdt-format.md), though its ops (SetExamine, SetDoorLock) belong to the room.
  Changing a SetExamine's message number there didn't change what the area showed, so the room script may run at moments the tests didn't reach (a scenario's first entry into the room, a flag).
