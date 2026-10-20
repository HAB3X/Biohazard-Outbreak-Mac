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
| Mouse | pointing at a title menu item or an item slot moves the cursor there; a click chooses it; clicking a tab (ITEM, SPECIAL, FILE) opens it; the wheel turns tabs, select pages and lists |

## The pause menu (Escape in play)

The pointer over a row moves the cursor there, the wheel turns the setting under it (volumes, brightness, the screen's
position), a click on DEFAULT SETTINGS, Quit Game or EXIT chooses it. Arrows and WASD move, E or Return chooses, Escape
closes it.

## The status screen's items

The pointer over a slot (the four and the equipped weapon's) moves the cursor there, a click opens that item's actions
(Use, Equip, Combine, Give...), and the pointer and a click choose among them.

## Title menus and Network Play

- OPTIONS: the pointer over a row moves the cursor there, a click opens it. CONTROLLER SETTINGS opens the PC menus' Controls
  while the keyboard and mouse were used last (the game's own screen with a gamepad). SCREEN ADJUST and EDIT NETWORK
  CONFIGURATION stay: the options' rows come from one of two layout tables (File 2's demo overlay at 0x56A520 and 0x56A570),
  the shorter one, without EDIT NETWORK CONFIGURATION, only while the game finds no network adaptor (0x3137B1 below 2, set
  at boot by 0x1BDC10, which Network Play reads too); SCREEN ADJUST is in both. Neither is safe to change for the menu alone.
- Single play's NEW GAME/CONTINUE (File 1's SCENARIO), the character log and File 2's COLLECTION/DATA CONVERT: the pointer
  over a row moves the cursor, a click chooses it.
- The collection's grid: the pointer over a cell moves the cursor, a click chooses it, a click on a tab switches to it, the
  wheel scrolls the rows (the grid's own first shown cell tells which cells are on screen).
- Network Play: the yes/no dialogs (the NET file, formatting, saving), the manners screen's Next and Back and the connection
  list follow the pointer and take a click; a click on a screen with nothing to choose is the confirm. In the KDDI browser
  (obsrv's pages) the browser's own pointer goes where the Mac's is, a click selects there (a field opens the software
  keyboard, a link or button is followed), and the wheel scrolls the page (the browser's L1 and R1).
- Not yet with the mouse: the status screen's SPECIAL and FILE lists (no run with two or more entries to find their cursor),
  File 1's horizontal "Return to the top menu?" in the collection (the keys work).

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

Found by memory diffs during play (`--control ram`): the task list and its count, the game, cutscene, title, select and status
screen tasks, the title menu's cursor, the status screen's tab, item cursor (moving by a slot's 0x98 bytes) and item-actions
flag (0x1C after the cursor). File 1's select screens aren't a task of the list (it's empty there): the title overlay's
state (its address at 0x3065FC; DEMO.BIN's dispatcher at 0x543050 switches on its byte 4) says 8 on the scenario select and
9 on the character select, so the wheel and the side and arrow clicks step them as in File 2 (the cards and arrows are where
File 2 has them). The word File 1's layout calls titleState (0x23BA68) is a count of the title's live objects (the code at
0x19E140 counts a pool of 512), 3 on the title menu: it tells the title menu apart only because that count is 3 there.
So File 1's options list (2 objects) and collection (4) are taken for the title and have no menu keys (only START): open, they
need a gamepad. Not fixed yet: 0x200D5C looks like the title's own state (sampled from boot: 0 the logos, then 6, 5 PRESS
START, 4, 3; 1 in the title menu, the options list and the collection) but reads 0 in the options' screens (brightness...)
as in the logos, so the controls keyed on it alone let the keyboard into the brightness screen and not out of it.
