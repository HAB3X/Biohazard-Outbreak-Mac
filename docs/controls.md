# PC controls

Both games use the same layout (src/controls.cpp). It follows the original PC releases of Resident Evil 4 and 5 as far as
this game's buttons allow. Every key can be rebound in the PC options' Controls (F10, or the game's own OPTIONS); the
bindings are kept in the settings folder's `bindings.txt`. A gamepad is the PS2 controller, unchanged.

The controls follow what the game is doing (its task list, read each update): **play**, **menus** (the title menu, the
selects, the status screen and its item actions) and **title / movies / cutscenes**.

## Play

The game's own roles (its controller settings, type A), on the keys of the PC releases of Resident Evil 4 and 5.

| Key | Does | (PS2) |
|---|---|---|
| W A S D | move | left stick |
| Left Shift (hold) | run; cancel (stop lending a shoulder, a reload) | ✕ |
| Left Ctrl | walk (toggle) | stick at half |
| E, Return | action, check, pick up, open | ○ |
| Right mouse (hold) | ready the weapon; while held, **the mouse aims**: across turns toward targets, up and down aims high and low | R1 + left stick |
| Left mouse | attack, fire (with the weapon ready; unarmed, a tackle); the action otherwise | ○ |
| Q (hold) | ready forward (no target picked) | R2 |
| R | reload | L1 |
| Space | ad-lib (the character speaks to suit the moment; hands over the item under the cursor in the status screen) | □ |
| M | the map screen (M or Space closes it) | △ |
| F (hold) | appeal shift: with 1-4, call a partner by name | L2 |
| 1 2 3 4 | the appeals: 1 "Go!", 2 "This way!", 3 "Help me!", 4 thanks or sorry (each character says it their way) | right stick up, down, left, right |
| Escape | the game's pause menu (settings, quit the game): the game stops; Escape again closes it | SELECT |
| Tab | the status screen (inventory, SPECIAL, FILE): the game goes on behind it, as on the console | START |
| Arrows | the D-pad | D-pad |

Checked in File 2's Training Ground (Kevin, unarmed there): the right mouse readies and the left mouse tackles, Space
ad-libs ("What's going on?"), M opens the map, 1-4 say "Go!", "This way!", "Help me!", "Sorry"/"...You saved me", F with 1
calls the partner's name ("Jim"), O opens the pause menu, R and Q reach the game (L1, R2). Firing a gun and reloading weren't
seen (no gun there).

## Menus

| Key | Does |
|---|---|
| W A S D, arrows | move the cursor |
| E, Return, right mouse | confirm (○) |
| Escape, Left Shift, Backspace | back, close (✕) |
| Space | △ (information windows) |
| F | □ |
| Q, PageUp / R, PageDown | previous / next tab (L1 / R1) |
| Z / C | L2 / R2 (the collection's pages) |
| M | closes the map (△) |
| O | SELECT (the options' default settings) |
| Tab | close the status screen (START) |
| Mouse | the pointer over an item moves the game's cursor there, a click chooses it, arrows and key hints press their keys, the wheel moves through lists and values: [The mouse in the menus](#the-mouse-in-the-menus) |

## The mouse in the menus

Every menu of both games is pointed at the same way (src/pointer.h; each area's menus in src/menus_*.cpp):

- **Nothing is steered.** The pointer over an item writes the menu's own cursor (the byte or word the game's up and down
  move, with whatever the game's own move writes with it), so the game draws the highlight, help text and sound from it the
  next frame: the cursor follows the pointer within a frame or two (25-75 ms measured through the control FIFO, its own
  polling included). A click sets the cursor and presses the menu's confirm (○); arrows, tabs and key hints press the
  button they show; the wheel presses the menu's up and down (or left and right, or the value's arrows over a setting).
- **Nothing is remembered.** Each vertical blank the menus are found again from the game's memory (the first finder whose
  menu is up wins; dialogs come before the screens under them), so a menu left and come back to, or one that changes under
  the pointer, is simply found again. The pointer shows while a menu is up and is hidden in play.
- **Places come from the game.** Items' places are read from what the game draws them with: its sprite records (the
  selects), its frame tables (the options' screens, the character log), its drawn text (src/text_map.h: where each string was
  drawn last frame; the questions' Yes and No, the difficulty), its parts pool (the lobby), or the positions its drawing code
  uses; a few hints were measured in pictures where nothing in memory holds them (noted in the code).
- **Moves that animate** (the selects' cards and portraits, the collection's tabs) are pressed, one press a step, each
  waiting until the menu takes input again.

The menus' engines, and what reads them:

| Engine | Screens | Read from |
|---|---|---|
| Title menus' state (File 2 0x24B070, File 1 0x23BE34) and the options' and character log's own (0x24B08C / 0x23BE40) | title menu, single play, character log list and pages, options, sound, brightness, screen position, save/load, network configuration's question | the state's sprites, the overlay's frame tables |
| Save data's engine (File 2 0x24AE14, File 1 0x23BBE0) | every YES/NO about the memory card | its texts' table, found where drawn |
| Selects' sprite records (File 2 0x24B080, File 1 0x3065FC) | scenario, character, partners, File 1's difficulty | the records' pictures |
| Collection (File 2 0x24B074, File 1 0x5A4420) | grid, tabs, scroll arrows, hints, its questions | the state's cursor, tab and first cell |
| Status task (File 2 GAME 0x6C8FA0, File 1 0x6B9450) | status screen (ITEM cells, weapon, actions, give, SPECIAL, FILE, file reader), pause menu, its quit question, map | the task's state and work area |
| Network setup's two menu layers (File 2 0x6AE220, File 1 0x68C5F0) | caution, NET file dialogs, broadband menu, manners, messages | its 13 layouts and bar places |
| KDDI browser (nethttp) | obsrv's pages | its own pointer, moved with the Mac's |
| Software keyboard (File 2 0x24B314, File 1 0x23C050) | every text entry | its key table and places |
| Lobby parts pool (File 2 0x6358A0, File 1 0x639AF0) | SELECT CAPCOM ID, SELECT SERVER, the area menu, room list, RULE, a room, their dialogs and hints | the parts' graphics, item numbers and cursors; the cursors the game's engines move each frame |

### Checklist

Checked through `--control` in hidden-window runs (`mouse X Y`, `tap Mouse_Left`, `wheel N`), the cursor read back with
`peek` and pictures taken with `snap`; latency is from the `mouse` command to the cursor's new value, through the FIFO.

| Screen | File 2 | File 1 |
|---|---|---|
| Title menu: hover, click | ✓ 24-71 ms | ✓ 32-72 ms |
| Single play (NEW GAME / LOAD) | ✓ | ✓ |
| Character log list; pages' tabs, scroll arrows, wheel, hints | ✓ (3 tabs) | ✓ (5 tabs) |
| Options list | ✓ | ✓ 24-72 ms |
| Sound: rows, volume bars, output arrows, wheel, Exit and Default hints | ✓ | ✓ |
| Brightness: slider, wheel, Default, Exit | ✓ | ✓ |
| Screen position: arrows, Default, Exit | ✓ | ✓ |
| Save/load rows, Exit | ✓ | ✓ 54 ms |
| Memory card YES/NO (hover, wheel, click) | ✓ | not seen |
| Collection: grid, tabs, scroll arrows, hints | grid ✓ 45-76 ms; tabs ✗ (File 2's tab byte isn't 0x10: its tab targets are wrong) | ✓ |
| Collection's "Return to the top menu?" Yes/No | not run | ✓ |
| Scenario select: cards, wheel, hints | ✓ | ✓ (one scenario open on a new card) |
| Character select: arrows, portraits (steps), big portrait, wheel | ✓ | ✓ |
| Difficulty (File 1) | - | ✓ hover 24-46 ms, click starts |
| Partner select | ✓ | not seen (File 1 started the scenario after the difficulty) |
| Status screen: cells, weapon, actions, tabs, FILE | ✓ | ✓ cells, weapon |
| Pause menu: rows, settings' wheel, quit question | ✓ | ✓ |
| Map: Cancel hint | ✓ | ✓ |
| Map, status screen, pause each opened and left 5 times by key and mouse | ✓ every time | ✓ (a map key lost once in the game's own event) |
| Network: caution, NET file dialogs, messages, broadband menu, manners | ✓ mouse alone to the login page | ✓ mouse alone to the login page |
| Browser's pointer, wheel | ✓ 56 ms | ✓ reached |
| Software keyboard: hover, click types | ✓ ("l6" typed, nothing sent) | not run |
| Lobby (after login) | built from the code, not run | built from the code, not run |

Not done with the mouse: the map's scrolling and zooming (the sticks' keys), File 2's DATA CONVERT, the results screen.

### What the owner's run needs to check

Logging in can't be done here, so the lobby is built from the game's code alone. With `BH_POINTER_LOG=1` set, each menu's
targets (their places, cursors and keys) and the strings drawn are written to the log when a menu comes up and every three
seconds: a run through SELECT CAPCOM ID, SELECT SERVER, the area menu (ROOM LIST / DATABASE / PERSONAL RANK), the room list,
RULE, a room and the hints (Sub-window, Soft keyboard, Chat log) shows whether each is found where it's drawn. A room's own
screen (its member slots and buttons) is File 2's only: File 1's step for it wasn't found.

## The Controls tab's options (the PC menus, F10)

- **Button prompts**: the game's own prompts (its pictures and the font's button glyphs, from the English patch's variants) as
  the keyboard's keys, a PlayStation pad's symbols or an Xbox pad's buttons (A for cross, B circle, X square, Y triangle: where
  they sit; LB, RB, LT, RT for the shoulders; Menu for START). Automatic follows the device used last.
- **Hold to toggle**: run, ready and ready forward (the controls marked as holds in src/controls.cpp) latch on at one press and
  off at the next; a change of mode (the status screen, a cutscene) lets them go. Checked in File 1's bar: one tap of Shift and
  W moved her 380 units in 2 s (running), 133 without (walking), 18 after a second tap.
- **Mouse speed** (25-300 %) and **Mouse acceleration**: every motion the game turns or aims with (the mouse stick and the Modern
  camera's look). Checked: 400 pixels turned the Modern camera 30 degrees at 100 % and 54 at 200 %.
- **Compensate game deadzone**: the game moves her only past 0.41 of a stick's push (measured with --control's stick: still at
  0.40, walking at 0.42, in both games); with it on, a gamepad stick's push past its own rest starts there. Not checked with a
  real gamepad.
- **Controller**: each PS2 button's gamepad button (bindings.txt's pad_ lines); Reset the controller puts them back. A
  DualShock 4's or DualSense's touchpad click, or an Xbox Series pad's Share, opens these Options (Back is the game's SELECT).
  Not checked with a real gamepad.

## Title, movies and cutscenes

Escape, Return, Space, E or Tab is START: it skips movies and cutscenes (File 2's cutscene task 0x344C10, File 1's
opening credits by the game overlay's state byte 0x48BF60 at 14; a byte of a pooled object, 0x27794D, was used before
and read 0 all through Decisions, Decisions' play, where the keys then didn't move the player) and starts from the title.

## Rules worth knowing

- A key held while the game changes what it's doing keeps its old meaning until it's let go: Escape held to close the status
  screen doesn't reopen it, a key held to skip a cutscene doesn't act in play. Keys that mean the same in both (Shift to run)
  carry on.
- The mouse pointer shows over the game's menus only; while the weapon is readied it's captured for aiming (released when the
  right button is let go).
- The game's menus ignore presses during their own transitions, as on the console: a press made as a screen changes may need
  repeating.
- Prompts: with the English patch, pictures showing the PS2's buttons are drawn for the device used last (the keyboard's keys,
  or the controller's buttons), and texts name the action rather than the button.

## Addresses (in src/controls.cpp's Layout)

What the controls follow is read each update, nothing kept between: the game's task (play, the status screen and its pause
menu: the status task's byte saying what it shows, 4 the pause), the cutscene task, and, before play, whether a menu finder
found a menu (the pointer's menus are the controls' menus); with no game running, the title (its state at PRESS START or
the attract movies) is START's. The task list, its count and the tasks were found by memory diffs during play (`--control
ram`).
