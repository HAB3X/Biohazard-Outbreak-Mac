# Outbreak Studio: what you can do

Start it with `./studio.sh` (File 2, scenario 40 opens first; the scenario menu at the top switches). Everything is picked up and dropped in one picture: **Plan** (from above) or **3D**.

| You want to | Do this | Checked in the game |
|---|---|---|
| Put down a door, exit, item, enemy, camera, examine spot, ladder, zone | **Add** (bottom), click on the floor | yes |
| Set where the player starts | Add > Player start | yes |
| Change an enemy's health, kind, difficulty; remove one | click it; the trash button removes it | yes |
| Change what an item is and how many | click it | yes |
| Block people with walls | Add > Wall (two clicks per wall; the tick shows the open side) | yes |
| Make a raised or lowered place to stand | Add > Floor (two corners), set its height | yes (the player's height follows) |
| Move, turn, stretch or hide the room's own objects | **Objects** tool, click one | yes (hiding a lamp cage and a bench shown in a snapshot) |
| Make a puzzle | click the room's empty space: **When the room starts**: set a flag, lock a door, set an examine message, show/hide an object, set a counter, each only if a flag is on or off | yes (flags, conditions, counters) |
| Try it | **Play** (from the start, or from this room); **Save the patch file** keeps the result |  |

Everything is undoable (⌘Z) and saved as you go (`work/studio/`).

## What it can't do yet (measured, not guessed)

- **New objects/geometry in a room.** A mesh appended to a room model loads but is not drawn (docs/research/rdt-format.md); the number of drawn objects comes from somewhere not found yet. Use the room's own objects as building blocks.
- **Choosing the scenario's start room.** Not in the scenario's `.sto`, not found elsewhere; edit the first room instead.
- **Puzzle triggers** (step in a zone, pick up an item): rules run when the room starts; the in-room trigger ops aren't measured.
- **File 1 rules**: File 1's scripts use other op numbers.
