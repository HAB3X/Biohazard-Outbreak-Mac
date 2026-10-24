# Outbreak Studio: what you can do

**Build** (the first view) is the whole editor in one place: fly with W A S D and right-drag, click anything to pick it, drag it (arrows slide it along x or z, the green arrow lifts, the ring turns, the diamond resizes; Ctrl+D copies, Del removes), and put props down from the **Library** (B) or the **Add** menu. Over the game's own picture your changes show as sketches until the picture is made again (about a minute after you stop). **Rooms** (top left) and **This room** (rules, clearing the room) open the side lists, which stay out of the way until asked for. **Camera views** is the old per-camera view, **Plan** is from above. The **?** button repeats the tour; the arrow beside undo lists every step.

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
| Start a level, a door that opens on a spot, a group of enemies | **Add > Ready-made pieces** (Start a level here empties the room and lays a floor round the player start; the door piece needs a door picked first, then a click on the spot) | rules measured one by one; the combination not yet run in the game |
| Find a table, shelf, chair or any prop and place it | **Library** (bottom bar, or `B`): scroll the pictures of every object in the scenario, filter by size or room, click one, then click in the room as often as you like; Esc stops | yes (File 1 bar: four bar stools added at 3x size stood on the floor with their pictures, no crash) |
| Bring in your own model (.obj) and paint a picture with your own image | **Add > Your own model**, then pick/paint a picture in the panel; the room's pictures are shared by everything that uses them | yes (a textured cube; every picture of a room repainted) |
| Make a puzzle | click the room's empty space: **Rules**: set a flag, lock a door, set an examine message, show/hide an object, set a counter; each can be always, only if a flag is on/off, while someone is inside one of the room's **script zones** (Add > Script zone, for a flag), or once an **item is taken** (its pickup trigger's flag) | yes in both games (zone -> flag -> counter measured) |
| Choose where the game begins | click a room > **Begin the game here** | yes (File 2's zoo; the other File 2 scenarios use the same records) |
| Start a level of your own in a room slot | click the room > **Clear this room**: no objects, enemies, items, walls or ground (doors stay); then **Add > Floor**, **Add > Wall**, **Add > Your own model** | yes (a cleared zoo room with a floor laid ran, the player walked on it) |
| Move, turn, resize with handles | select a thing in Game view or the free camera: the green arrow lifts it, the yellow ring turns an object, the blue diamond resizes it; arrow keys nudge, R turns, `-` `=` resize | lift checked by script only |
| Look around freely | **Free camera** tab, or press W A S D / right-drag in Game view. A moment after you stop, the picture becomes **the game's own** from exactly where the camera is (the game, started behind the scenes, is told to hold its camera there: eye, look and field of view are written to a mailbox in main memory at 0x01D60000, read by `src/modern_camera.cpp`, with the room's nearest cut borrowed so it draws what suits that view). While you move, the last picture stays dimmed. **Game's picture** switches it off (then Studio's own, rougher drawing shows) | yes (File 1 bar and File 2 zoo, arbitrary eye and look) |
| Try it | **Play** (from the start, or from this room); **Save the patch file** keeps the result |  |

Everything is undoable (⌘Z) and saved as you go (`work/studio/`).

## The game's pictures

Studio keeps a copy of the game running behind the scenes and photographs every camera of every room it can reach, one scenario after another (File 1's first, then File 2's 15, 20, 26 and 40), into `work/studio/survey/`. The chip at the top shows how far it has got. A room's pictures appear in Game view as they are taken. In File 1's first scenario only the bar and the next three rooms can be reached from the start (the doors beyond don't open without the story), so the rest show Studio's own, rougher drawing. In File 2 a room the doors don't reach is photographed by starting a game in it.

## What it can't do yet (measured, not guessed)

- **Pictures are the room's own pictures repainted** (the same size and number of colours), not new ones added; the pictures of a room are mostly atlases.
- **Start room** only where the scenario's event file has the opening start records (File 2's scenarios 15, 20, 21, 22, 23, 26, 27, 29, 30 and 40; File 1's start isn't in them, so File 1 has no start choice).
- **New room numbers**: a room can be cleared and built in, but a room with a number the scenario does not have is not built (the game stores its room index ahead of the room data).
