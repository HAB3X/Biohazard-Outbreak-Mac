# Outbreak Studio: what you can do

**Build** (the first view) is Studio's own live 3D drawing of the level, lit and fogged as the game lights its rooms; no pictures or video of the game are shown in place of it (the game runs only when you press Play or Test). It is the whole editor in one place: fly with W A S D and right-drag, click anything to pick it, drag it (arrows slide it along x or z, the green arrow lifts, the ring turns, the diamond resizes; Ctrl+D copies, Del removes), and put props down from the **Library** (B) or the **Add** menu. **Rooms** (top left) and **This room** (rules, clearing the room) open the side lists, which stay out of the way until asked for. The **?** button repeats the tour; the arrow beside undo lists every step.

Start it with `./studio.sh` (File 2, scenario 40 opens first; the scenario menu at the top switches). **Camera views** shows the room as the player sees it: through the room's own fixed cameras, with the game's baked light (step through a room's cameras with the arrows, or `,` and `.`; picking something switches to a camera that shows it). **Plan** (from above) is for overview and awkward spots.

| You want to | Do this | Checked in the game |
|---|---|---|
| Put down a door, exit, item, enemy, camera, examine spot, ladder, zone | **Add** (bottom), click on the floor | yes |
| Set where the player starts | Add > Player start | yes |
| Change an enemy's health, kind, difficulty; remove one | click it; the trash button removes it | yes |
| Change what an item is and how many | click it | yes |
| Block people with walls | Add > Wall (two clicks per wall; the tick shows the open side) | yes |
| Make a raised or lowered place to stand | Add > Floor (two corners), set its height | yes (the player's height follows) |
| Move, turn, stretch, hide or **copy** the room's own objects (more scenery) | **Objects** tool, click one, the copy button in its panel | yes (copies of the zoo sign drew beside the original) |
| Start a level, a door that opens on a spot (measured in both games: the door's lock value reads 201 at the start, 65535 once someone has stood on the spot, and stays), a door that opens with an item (the item half not yet checked in the game), a group of enemies | **Add > Ready-made pieces** (Start a level here empties the room and lays a floor round the player start; the door piece needs a door picked first, then a click on the spot) | rules measured one by one; the combination not yet run in the game |
| Find a table, shelf, chair or any prop and place it | **Library** (bottom bar, or `B`): scroll the pictures of every object in the scenario, filter by size or room, click one, then click in the room as often as you like (it snaps to the grid and to the sides of what stands nearby; R turns it); Esc stops. Names are given by looking: File 1's first scenario and File 2's 40, 15, 20 and 26 have them for the recognisable props (the rest show as unnamed shapes); the Test button (T) starts the game in the room you're in | yes (File 1 bar: four bar stools added at 3x size stood on the floor with their pictures, no crash) |
| Bring in your own model (.obj) and paint a picture with your own image | **Add > Your own model**, then pick/paint a picture in the panel; the room's pictures are shared by everything that uses them | yes (a textured cube; every picture of a room repainted) |
| Make a puzzle | click the room's empty space: **Rules**: set a flag, lock a door, set an examine message, show/hide an object, set a counter; each can be always, only if a flag is on/off, while someone is inside one of the room's **script zones** (Add > Script zone, for a flag), or once an **item is taken** (its pickup trigger's flag) | yes in both games (zone -> flag -> counter measured) |
| Choose where the game begins | click a room > **Begin the game here** | yes (File 2's zoo; the other File 2 scenarios use the same records) |
| Start a level of your own in a room slot | click the room > **Clear this room**: no objects, enemies, items, walls or ground (doors stay); then **Add > Floor**, **Add > Wall**, **Add > Your own model** | yes (a cleared zoo room with a floor laid ran, the player walked on it) |
| Move, turn, resize with handles | select a thing in Build or Camera views: the green arrow lifts it, the yellow ring turns an object, the blue diamond resizes it; arrow keys nudge, R turns, `-` `=` resize | lift checked by script only |
| Look around freely | **Build** tab, or press W A S D / right-drag in Camera views. You fly through the level: Q E down and up, Shift fast, the wheel forward and back. The room you are in (the one whose floor is nearest below you) is drawn and is the room you edit; fly through a door and the next room takes over. (The game holds one room at a time and each room carries its own copies of the buildings around it, so drawing them together made them fight; Plan shows every room) | yes (File 1 bar) |
| Try it | **Play** (from the start, or from this room); **Save the patch file** keeps the result |  |

Everything is undoable (⌘Z) and saved as you go (`work/studio/`).

## What it can't do yet (measured, not guessed)

- **Pictures are the room's own pictures repainted** (the same size and number of colours), not new ones added; the pictures of a room are mostly atlases.
- **Start room** only where the scenario's event file has the opening start records (File 2's scenarios 15, 20, 21, 22, 23, 26, 27, 29, 30 and 40; File 1's start isn't in them, so File 1 has no start choice).
- **New room numbers**: a room can be cleared and built in, but a room with a number the scenario does not have is not built (the game stores its room index ahead of the room data).


## Bringing things from other levels

- **Library → level picker** (top of the Library): any scenario of either game. Its rooms load (a few seconds each the first time), their objects show as
  usual, and the one you put down brings its pictures into this scenario when you Play or Build.
- **Library → Enemies**: every enemy model on the disc. Any kind can be put in any scenario: the new enemy takes its kind's own settings from a scenario
  that has one. Kinds that wait in their own scenarios (the dogs) wait here too. Kinds marked "events only" never stand in a level, so they can't be made.
