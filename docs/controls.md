# PC controls

Both games use the same layout (src/controls.cpp). It follows the original PC releases of Resident Evil 4 and 5 as far as
this game's buttons allow. Every key can be rebound in the PC options' Controls (F10, or the game's own OPTIONS); the
bindings are kept in the settings folder's `bindings.txt`. A gamepad is the PS2 controller, unchanged.

The controls follow what the game is doing (its task list, read each update): **play**, **menus** (the title menu, the
selects, the status screen and its item actions) and **title / movies / cutscenes**.

## Play

| Key | Does | (PS2) |
|---|---|---|
| W A S D | move | left stick |
| Left Shift (hold) | run | ✕ |
| Left Ctrl | walk (toggle) | stick at half |
| E, Return | action, check, pick up, open | ○ |
| Right mouse (hold) | ready the weapon; while held, **the mouse aims**: across turns toward targets, up and down aims high and low | R1 + left stick |
| Left mouse | fire, attack, use | □ |
| Space | command the partner (ad-lib) | △ |
| Q / R | previous / next item | L1 / R2 |
| F | quick action | L2 |
| Tab, Escape | the status screen (inventory, SPECIAL, FILE), the game's own pause | START |
| M | select | SELECT |
| Arrows | the D-pad | D-pad |

## Menus

| Key | Does |
|---|---|
| W A S D, arrows | move the cursor |
| E, Return, right mouse | confirm (○) |
| Escape, Left Shift, Backspace | back, close (✕) |
| Space | △ (information windows) |
| F | □ |
| Q, PageUp / R, PageDown | previous / next tab (L1 / R1) |
| Tab | close the status screen (START) |
| Mouse | pointing at a title menu item or an item slot moves the cursor there; a click chooses it; clicking a tab (ITEM, SPECIAL, FILE) opens it; the wheel turns tabs, select pages and lists |

## Title, movies and cutscenes

Escape, Return, Space, E or Tab is START: it skips movies and cutscenes and starts from the title.

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

Found by memory diffs during play (`--control ram`): the task list and its count, the game, cutscene, title, select and status
screen tasks, the title menu's cursor, the status screen's tab, item cursor (moving by a slot's 0x98 bytes) and item-actions
flag (0x1C after the cursor). File 1's select screens aren't a task of the list (it's empty there): the title overlay's
state (its address at 0x3065FC; DEMO.BIN's dispatcher at 0x543050 switches on its byte 4) says 8 on the scenario select and
9 on the character select, so the wheel and the side and arrow clicks step them as in File 2 (the cards and arrows are where
File 2 has them). The word File 1's layout calls titleState (0x23BA68) is a count of the title's live objects (the code at
0x19E140 counts a pool of 512), 3 on the title menu: it tells the title menu apart only because that count is 3 there.
