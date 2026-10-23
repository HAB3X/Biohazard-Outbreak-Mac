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
| Move, turn, stretch, hide or **copy** the room's own objects (more scenery) | **Objects** tool, click one, the copy button in its panel | yes (copies of the zoo sign drew beside the original) |
| Bring in your own model (.obj) and paint a picture with your own image | **Add > Your own model**, then pick/paint a picture in the panel; the room's pictures are shared by everything that uses them | yes (a textured cube; every picture of a room repainted) |
| Make a puzzle | click the room's empty space: **When the room starts**: set a flag, lock a door, set an examine message, show/hide an object, set a counter, each only if a flag is on or off | yes (flags, conditions, counters) |
| Try it | **Play** (from the start, or from this room); **Save the patch file** keeps the result |  |

Everything is undoable (⌘Z) and saved as you go (`work/studio/`).

## What it can't do yet (measured, not guessed)

- **Pictures are the room's own pictures repainted** (the same size and number of colours), not new ones added; the pictures of a room are mostly atlases.
- **Choosing the scenario's start room.** Not in the scenario's `.sto`, not found elsewhere; edit the first room instead.
- **Puzzle triggers** (step in a zone, pick up an item): rules run when the room starts; the in-room trigger ops aren't measured.
- **File 1 rules**: File 1's scripts use other op numbers.
