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
| 0x55 | SetExamine | id, message, parameter | activates the examine areas (tag 5) with field 12 = id; their field 16 = the message number (the stage message file's record number - 1), field 22 = the parameter. Verified in the game |
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

## Ops read from the handlers' code (second pass, no live test)

These were read in the generated C of each handler (`bh_script_ops.py`, then the code): what each writes or calls is certain; what it is for is
inferred and marked so in `bh_rdt.OPS`. The commonest: 0x70 (645 uses), 0x20, 0x135, 0x1A5, 0xF1, 0x73, 0x121, 0x7E, 0x11, 0x120, 0x110, 0x131.

| op | what the code does |
|---|---|
| 0x110 | `0x491268[a0] = a1` (a0 below 8) |
| 0x20 | stores a byte at 0x3134CE |
| 0x10 / 0x11 | the byte at the start of a 536 byte record at 0x3B3200 + 536 n (0x11 writes 0) |
| 0x7E / 0x7F | the 104 byte table at 0x395890 (32 entries): 0x7E sets bit 0 of an entry's flag byte when the entry is in use |
| 0xA0 / 0xA1 | bank C, the word at 0x4900FC: 0xA1 clears the bit |
| 0x1A5 | writes 0 or 1 to +4176 of each of the eight character records in use |
| 0x155 | conditional: a byte of the current character's record against the argument; skips the block with `game_0066be60` |
| 0x180 | conditional on `cheat_single_play()` |
| 0x73 | position source: absolute (mode 2), a character's (mode 1) or none, then `f_001b8170` |
| 0x131 / 0x135 | like SetExamine: loop the room's areas, those with tag 5 and field 12 = id are activated and get message fields |
| 0x70, 0x120, 0x121, 0x1A0, 0xF1, 0x93, 0x94, 0xD1 | call engine routines; their effect is not understood yet |

## Where the game runs the room scripts, and the proof

The game runs the rooms' scripts from the pack's `.sgl` (`r040.sgl`, 195 KB, loaded at 0x80AA80 in File 2's memory; thread 1's program counter
walks 0x80D000 to 0x836000), not from the room file's section 1 (0x8c7e40 in memory, which the interpreter doesn't read). `bh_rdt.Sgl` reads its
index (the room id with the variant in the high half, then the offset of the room's block): a block holds the room's entity records and then
the script, the same words as the room file's section 1. Measured end to end in the zoo's first room (File 2), standing at the examine area whose id is 4 and pressing the action key:

| the script word (SetExamine, id 4) | what the game showed |
|---|---|
| 122 (as shipped) | "The high wall goes on" |
| 111 written into the .sgl | "There's a plan / Already memorized" |
| 113 written by `bh_room_patch.py` (the editor's save: the room file's copy and the .sgl's) | "A vintage cash register counter" |

So a message number N in a script is the stage message file's record N - 1 (the first record is number 1); and writing the .sgl's copy is what changes the game
(the room file's copy alone changed nothing: 101, 105 and 111 there showed the same "The high wall goes on"). The editor's script list shows the message text for
SetExamine and SetDoorLock.

## Still open

- Only about 25 of the 258 ops are read (the scenario's .evb and the .sgl use many more: 0x39, 0x40, 0x1A0, 0xF2, 0xF4, 0x16A, 0x31, 0x37, 0x30, 0x69 ...).
- The .sgl's room block before the script (the entity records) isn't decoded.
- The room file's own section 1 is a copy the game doesn't use for these rooms; it is still edited with the .sgl's so the two stay the same.
