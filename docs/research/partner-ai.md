# The partners' AI: what's known

Goal (the owner's): partners that fight usefully, follow and don't get stuck, heal and share items sensibly, and obey the
ad-lib commands (Go!, This way!, Help me!, Thanks/Sorry; F with 1-4 calls one by name). Offline only.

How it was found: the games' code read in Ghidra (the executable and the overlays loaded at their addresses in one image, gp
set: File 1 0x241C70, File 2 0x2510F0) and checked against the recompiled C (`build/generated/*_code`, exact where Ghidra
drops a three-operand `mult`), with memory dumps of play (`--control ram`) and the telemetry below. (An earlier note here
said the positions weren't floats: they are, at +0x38 of the records below.)

## How a partner is driven: a virtual pad, written by SINGLE.BIN's think

- No AI data on the disc: the behaviour is code. It's in **SINGLE.BIN** (File 2: BIN/8.DAT), the single-play overlay at
  0x820000 (File 2 0x7D2000), loaded only for single play; network play loads NETAQ.BIN at the same address instead. The
  play mode byte (File 1 0x3065C2, File 2 0x3137D2) is 0 in single play and 1 in network play; the executable's hooks
  (File 1 0x1AEDC0-0x1AF4D0) call SINGLE's functions for 0 and NETAQ's for 1.
- The characters' update (File 1 GAME.BIN 0x6383E0, File 2 0x63EAF0) walks the 8 character records (the player first) each
  frame. For a record whose CPU flag is set it calls SINGLE's **think** (File 1 0x822440, File 2 0x7E99D0) first, only in
  single play. Then 0x1AFC60 (File 2 0x1B08B0) copies a pad into the record for characters a pad drives (the player: pad 0 by
  the table at 0x23BB38 indexed by the character's id; the partners' entries are 0xFF, so their record keeps what the think
  wrote), and the character's update reads the record's pad as it reads the player's.
- **The virtual pad** in each record: pressed this frame (File 1 +0xF78, File 2 +0xF88), released (+4), held (+8), three
  frames of history, then the sticks (File 1 +0xF98..). Its bits are the game's button roles (src/partner_ai_pad.h): 1 forward,
  2 back, 4 turn left, 8 turn right, 0x10 run (cross), 0x20 action/attack (circle), 0x40 R2, 0x80 L2, 0x100 ad-lib
  (square), 0x200 triangle, 0x400 ready (R1), 0x800 reload (L1). The think clears it, then sets bits: back + run is the
  quick turn, ready + attack fires.
- The think (File 1 0x822440): clears the pad; unless the game is paused or the partner is busy, it runs
  - **perception** 0x822520 (File 2 0x7E9AD0): the route node it's on, a wall check ahead (150 frames blocked resets the
    plan: its own unstick), the player's commands (below), and the enemies near it (the list at 0x3840E0 + id * 0x32, enemy
    indices ending 0xFF; File 2 0x385860);
  - **the plan step** 0x8232F0: a plan picked by desire weights (+0x10C8, eight bytes; the highest picks plan 0, 1, 2, 4 or
    5; others for special cases: 10 when the player's motion is 0x25, 11, 12, 13, 14 when the partner's motion is ';', 15
    for character type 7), then the plan's routine from the table at 0x839240 (16) by the plan index (+0xFF4), repeated
    while it returns nonzero;
  - **the move** 0x82A6D0 (table 0x839460 by +0xFF8) or 0x82BE60 (table 0x8394D0), and the **action** routines (table
    0x839BA0, 51, by +0x103E: attacks, item use...).
- The AI's own work area is the record's end (File 1 +0xFF0..+0x10E0; File 2 0x1C further on for most fields): the CPU flag
  +0xFF0 (File 2 +0xBB4), plan +0xFF4, move +0xFF8, action +0x103E, its picked enemy +0x106E (index) and +0x1068 (record),
  target point +0x101C, wanted facing +0x1038, the blocked-ahead counter +0x10A4.

## The records

| | File 1 | File 2 |
|---|---|---|
| Characters (8, the player first) | 0x476DD0, 0x10E0 bytes each | 0x47BD30, 0x1100 |
| Enemies (50) | 0x3FFCA0, 0xD90 | 0x403DB0, 0xDB0 |
| Room shown (the player's) | 0x3065AC (s16) | 0x3137BC |
| Distances between characters | 0x386110 + id * 0x40 + other * 8 (float) | 0x387890 |
| Distances character to enemy | 0x385490 + id * 400 + enemy's +6 * 8 (float, then two flags) | 0x386C10 |

Cross-checked against RaccoonRecomp's REO-GreatestHits-Cheats (MIT, the USA File 1 disc, src/partners.c): its character
blocks are 0x4A5C30 there with the same 0x10E0 stride, type +0xB98 (0-7 Kevin, Mark, Jim, George, David, Alyssa, Yoko,
Cindy) and the most hit points +0x546, as found here on the Japanese disc.

Both kinds of record: active +0, kind +2 (2 an enemy), id +3 (an enemy's own index at +6), the character it's after +0x24
(an enemy's: a character record), position +0x38 (three floats, y up), facing +0x92 (u16, 0x10000 a turn), room +0x1E2.
Hit points (s16, then the most): File 1 +0x544, File 2 +0x540 (Kevin 2300, Mark 3000, Cindy 1500 in File 1's bar; enemies the
same field: zombies 700-800). The motion byte (what the character's doing): File 1 +0x54F, File 2 +0x54B. The character type
+0xB98 (File 1: 0 Kevin, 1 Mark, 7 Cindy); the weapon in hand's item number +0xCA8 (the item table at 0x6E5940, 0x44 bytes
each).

## The player's commands

The appeals ("Go!", "This way!", "Help me!", thanks/sorry, and a partner's name with the appeal shift) are lines the player's
character says. The speech code (File 1 GAME.BIN 0x687420) puts the line's command code in the speaker's record (File 1
+0x1075, File 2 +0x1091) while it's said, and 0 after. The partners' perception reads the player's: codes 2, 4, 5 and 8 set
their response timers and pick an answer line; 9 and 10 call partner 1 and 2 by name. Measured (File 1, each key pressed in play): 1
"Go!" 2, 2 "This way!" 4, 3 "Help me!" 5, 4 thanks or sorry 3, F+1 10 (Cindy by name), F+2 6, F+3 9 (Mark by name).

## Telemetry

`BH_AI_LOG=FILE` (src/partner_ai_telemetry.cpp) writes each partner's state every half second (room, distance to the player,
hit points, motion, the AI's plan/move/action/target, the pad, the enemies in the player's room) and events (attacks with the
weapon ready, damage to each enemy, heals, hurts, the player's commands). `tools/ai_report.py LOG` sums it up: distance
stats, time in another room, attacks and hits, damage put to each partner (damage within half a second of its attack),
stuck time (forward held, under 30 units a second), heals with the hit points before, the player's damage and time below 30%.

## Items

The items are records at File 1 0x3967D0 (File 2 0x397B40), 0x3C bytes each: +0 there, +6 the item's number, +0x24 its
rounds (a gun's loaded rounds), +0x2B its class. The classes (File 1 0x716980, File 2 0x75D100; 0x18 bytes each) begin with
flags (the executable's 0x583EA0 tests them): 1 a weapon, with 0x2000/0x4000/0x8000 one that fires (Kevin's and Mark's
handguns 0x102041), without them a melee weapon (knife, pipes); 4 without 1 a recovery item (herbs, sprays, 0x40004). A
character's item in hand is +0xC7C (File 2 +0xC8C); it carries nine (File 1 +0xC84: four slots, its own item, then four in
it: Cindy's herb case holds her herbs there). The AI's item check (File 1 0x823780, File 2 0x7EB0C0) looks at what the bits
of +0x1096 (File 2 +0x10B0) ask (1 use or pick up, 2 give, 4 a better weapon, 8 reload) and starts plan 7, 8 or 9; the plans
ask it only on arriving somewhere.

## What a partner can do to the world

Everything SINGLE.BIN calls outside itself was listed (the call graph of the decompiled code) and read: route and door code
(File 1 0x829830 asks the door's state, 0x551C20, and turns back at the locked ones: 2, 3 and 5), the character actions its
moves start (GAME.BIN 0x637810 picks up the item an AI plan chose, motion 9; 0x637510 uses an item it carries, 0x17, for a
weapon swap; 0x6375B0 combines, a reload; 0x637740 gives one to the player, 0x15; 0x637990 Cindy's herbs on another, 0x12;
0x637AF0 helps the player up), item and status queries, the speech and network stand-ins. None operates a device, enters a
code or uses an item on the world, and its pad presses the action button only to fire (with the weapon readied). The items
it picks up come from 0x833250 (File 2 0x7E3AA0): it walks the room's item objects (+0x30 0x80, the item at +0x40) and takes
any it has room for, keys and event items too. With the New AI that chooser is overridden: only weapons, ammunition and
recovery items (class flags 1, 2/0x400/0x800, 4) are taken; the rest is the player's (the owner's rule: docs/adr/0002).

## Measured: the game's own AI

File 1, Outbreak (J's Bar, Kevin, normal), the player standing still from the start of play (256 s of play): Mark spent 41%
of the time away from the player (into the next room and back, again and again) and fired his first shot at about 200 s,
after the zombies had had the player down to 1 hit point; Cindy shoved (1-10 damage) and healed herself twice. The player
took 3959 damage, spent 65 s below 30% and was saved from death by an item at 1 HP; one zombie killed.

The game is deterministic for the same input: runs of a scenario differ only by the input's timing (tools/ai_scenario.py's
seeds have the player take a few steps first). Measured with `tools/ai_scenario.py bar-idle MODE --seeds 3`, 240 s of play
each (the means):

| | Original | Director, first (0e71ce1) | Director now |
|---|---|---|---|
| First partner attack after an enemy is within 600 of the player | 31.6 s | 6.8 s | 0.2 s |
| Mark's time away from the player (another room or farther than 1500) | 24% | 0% | 0% |
| Mark's damage to the zombies (15 rounds) | 1568 | 1816 | 1665 |
| Damage the player took (in the 2 minutes from the first threat) | 2871 (1703) | 2510 (763) | 1970 (1283) |
| The player's time below 30% / downed | 36 s / 1 of 3 | 42 s / 1 of 3 | 6 s / 0 of 3 |
| Mark's damage taken / downed | 1270 / 0 of 3 | 2463 / 0 of 3 | 1133 / 1 of 3 |
| Cindy's damage taken / downed | 1900 / 1 of 3 | 1500 / 3 of 3 | 1320 / 1 of 3 |

What changed after the first: a partner without a loaded gun steps away from an enemy within 200 (on until 320) unless it's
on the player, and shoves one off the player only with nothing else at its elbow; a partner with a loaded gun shoots its
target itself within 1300 (turning to it with the weapon down: readied, left and right move the game's own aim between
enemies instead, and the first try at turning with it readied emptied a clip into nothing), and leaves it to the game's
attack routine for 8 s after 6 presses that don't hurt the target. The game's own routine is still the better shot (about
120 damage a round against 111), but it walks and waits before firing.
