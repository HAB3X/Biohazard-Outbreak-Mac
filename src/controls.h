// Biohazard Outbreak's PC controls: the keyboard and mouse in the layout of the original PC releases of Resident Evil 4 and 5
// (WASD to move, the mouse's right button to ready or aim the weapon and its left to fire, E for the action, Shift to run, Tab
// for the inventory), each a button of the PS2's controller (Ps2GameInfo's controls), in three modes the game's own memory says:
// play, its menus (the title menu, the selects, the status screen with its ITEM, SPECIAL and FILE tabs, the item's actions) and
// the title and movies (Escape or Return is START there, which skips them). Escape is START in play (the status screen, the
// game's own pause) and CROSS, back, in every menu, the status screen's top level included. A gamepad is the PS2's controller as
// before. docs/controls.md has the layout; every control can be rebound in the PC menus' Controls (F10 opens the PC options)
#pragma once

#include "ps2/runtime.h"

#include <cstdint>

namespace BiohazardControls
{
// Where one game's memory keeps what the controls read (the executable's data and the game overlay's, at the addresses the
// game loads them to). The game keeps its active tasks (the game's loop, the title, a menu, the status screen...) in a list of
// up to 8 entries, each the address of the task's function, with the count in a word of its own: a task's function says what's up
struct Layout
{
    uint32_t taskCount, taskList;
    uint32_t gameTask;      // the in-game loop (play)
    uint32_t cutsceneTask;  // the game's loop while a cutscene plays (0: not found)
    uint32_t baseTask;      // the executable's own loop (movies, the title and loading, and under the game)
    uint32_t selectTask;    // the scenario, character, partner and difficulty selects
    uint32_t titleState;    // a word: 0 a movie, 2 the title's "PRESS START", 3 its menu (single play, collection, options...)
    uint32_t statusTask;    // the status screen
    uint32_t titleCursor;   // the title menu's item (a word, 0 the first)
    uint32_t statusTab;     // the status screen's tab (a byte: 0 ITEM, 1 SPECIAL, 2 FILE)
    uint32_t statusCursor;  // the status screen's item cursor: a word, the address of the slot it's on (the equipped weapon's is elsewhere)
    uint32_t statusPopup;   // a byte that's nonzero while the item's actions (use, combine, give...) are up
    uint32_t slotStride;
    float titleTop, titleStep; // where the title menu's six items are drawn: the first's middle and the step, as fractions of the picture's height
    uint32_t slotStrideUnused;    // the item slots' size: the grid's four are one after the other from where the cursor is as the screen opens
};

extern const Layout File1;
extern const Layout File2;

void Apply(Ps2GameInfo& game, const Layout& layout);
}
