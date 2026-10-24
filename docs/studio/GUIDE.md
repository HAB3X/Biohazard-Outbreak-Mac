# Outbreak Studio: what you can do

Start it with `./studio.sh` (File 2, scenario 40 opens first; the scenario menu at the top switches). Everything is picked up and dropped in one picture. **Game view** (the default) shows the room exactly as the player sees it: through the room's own fixed cameras, with the game's baked light (step through a room's cameras with the arrows, or `,` and `.`; picking something switches to a camera that shows it). **Real view** (button at the top of the picture) swaps Studio's own drawing for the game's own picture of that camera, taken in a copy of the game started behind the scenes on your changes (first picture about two minutes, then about ten seconds a picture; after you edit, the old picture is dimmed until you press Update picture). Only File 1's and File 2's first scenarios can do it. **Plan** (from above) and **3D** (free orbit) are for overview and awkward spots.

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
| Make a puzzle | click the room's empty space: **Rules**: set a flag, lock a door, set an examine message, show/hide an object, set a counter; each can be always, only if a flag is on/off, or (for a flag) while someone is inside one of the room's **script zones** (Add > Script zone) | yes in both games (zone -> flag -> counter measured) |
| Choose where the game begins | click a room > **Begin the game here** | yes (File 2's first scenario) |
| Start a level of your own in a room slot | click the room > **Clear this room**: no objects, enemies, items, walls or ground (doors stay); then **Add > Floor**, **Add > Wall**, **Add > Your own model** | yes (a cleared zoo room with a floor laid ran, the player walked on it) |
| Move, turn, resize with handles | select a thing in Game view or the free camera: the green arrow lifts it, the yellow ring turns an object, the blue diamond resizes it; arrow keys nudge, R turns, `-` `=` resize | lift checked by script only |
| Look around freely | **Free camera** tab, or press W A S D / right-drag in Game view | yes |
| Try it | **Play** (from the start, or from this room); **Save the patch file** keeps the result |  |

Everything is undoable (⌘Z) and saved as you go (`work/studio/`).

## What it can't do yet (measured, not guessed)

- **Pictures are the room's own pictures repainted** (the same size and number of colours), not new ones added; the pictures of a room are mostly atlases.
- **Start room** only where the scenario's event file has the opening start records (File 2's first scenario; others and File 1 not found yet).
- **Item pickup triggers**: only zones trigger rules so far.
