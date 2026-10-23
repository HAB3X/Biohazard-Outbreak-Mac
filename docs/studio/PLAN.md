# Outbreak Studio: the plan

The goal: make your own levels for File 1 and File 2 and have them show up in the game, with an editor that is quick and pleasant to use: place spawn points, enemies, doors, zones and items,
set health, build puzzles, build scenery and assets, set collision. Not a pile of panels: one window, one viewport, tools you pick up and drop.

## What the game lets us do (found out, measured)

- **Rooms, doors, cameras, fog, lights, sound zones, items, enemies, scripts**: decoded (docs/research). Edits are patches over your own disc; nothing of the disc is copied.
- **Size was the wall**: a member of the game's archives couldn't grow (the game adds up sector counts). **Not any more**: `tools/bh_vdisc.py` rebuilds the archives' tables and moves the rebuilt
  file to the end of the disc; the runtime (disc.cpp, patch version 3) lays the pieces: copied ranges of the original and the new bytes, so the patch stays small.
  Measured: a room of File 2's zoo grown by 340 KB past its sectors (an exit added that the old room couldn't hold) plays; the English patch is carried across.
- A room's **door** needs a door object of the room's model, so a new door takes an existing door's number (and shares its object): at most a few copies; more than a few crashed the game (the limit is being measured).
- Not known yet: collision (the floor and walls the characters stand on: is it the model or a table?), the start position of a scenario, the sound-source kinds, most script ops' meanings.

## The editor (what it will be)

One window (a local web page the Python tools serve, opened by `./studio.sh`; a native app later if it pays):

1. **Viewport** (centre): top-down plan or 3D, one camera, one set of tools. Rooms drawn from their real models (textured), objects drawn on top and picked by clicking.
2. **Tool rail** (left): Select, Move, then things to place: Door, Exit, Item, Enemy, Spawn, Zone/Trigger, Message, Camera, Light. Click to place; drag to move; handles for turning and sizing.
3. **Outliner** (left): the scenario's rooms and what is in each (search, hide, lock).
4. **Inspector** (right): friendly forms, not numbers: an enemy's kind as a name, health as a slider with the real hit points, difficulty chips, facing as a dial; an item's kind from a list with pictures when known.
5. **Undo/redo everywhere, autosave, projects** (your work in `work/studio/`).
6. **Play** (top right): builds the patch and starts the game, in the room you were editing.
7. **Problems** (bottom): the editor tells you before the game does (a door to nowhere, a room with no exit, too many doors).

## Stages

1. **Foundation** (done): the rebuilt disc (bh_vdisc, bh_studio_build, runtime v3 patches).
2. **Studio shell**: the window, the viewport, selection and moving, the inspector for everything that already exists (areas, doors, cameras, lights, zones, items, enemies, nodes), undo/redo, build and play.
3. **Make things that did not exist**: add items and enemies (the item and enemy files grow through the rebuilt disc), new doors, spawn points (the start position: to be found, else a runtime hook), triggers and messages.
4. **Puzzles**: a block editor for room scripts (flags, locks, items, messages) on the ops that are understood, with the rest left as raw words.
5. **Scenery and assets**: replace a room's model with your own (import OBJ/glTF, convert to the game's model format, textures to the game's picture format), place and move scenery objects.
6. **Collision**: once found, edit it; until then, generate it from the model.
7. **New level wizard**: pick a scenario's room slots, give them your geometry, link them, set the start; export.

Each stage is checked in the running game before it is called done (the editor shows what is measured and what is not).
