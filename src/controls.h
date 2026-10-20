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
// A row of SELECT's pause menu: its cursor's value there, its middle (a fraction of the picture's height), and whether it's a
// setting (the left and right keys, and the wheel, change it; a click doesn't choose it)
struct PauseItem
{
    int value;
    float y;
    bool setting;
};

struct Layout
{
    uint32_t taskCount, taskList;
    uint32_t gameTask;      // the in-game loop (play)
    uint32_t cutsceneTask;  // the game's loop while a cutscene plays (0: not found)
    // A byte that's 0 while the game's loop plays a scene of its own with no cutscene task (File 1's opening credits) and
    // nonzero while the player has control (0: none; found by memory diffs: the credits and a cutscene against two stretches
    // of play)
    uint32_t playControl;
    uint32_t baseTask;      // the executable's own loop (movies, the title and loading, and under the game)
    uint32_t selectTask;    // the character, partner and difficulty selects
    uint32_t scenarioTask;  // the scenario select (0: not found)
    // Where the selects aren't tasks (File 1): a word holding the address of the title overlay's state, whose byte 4 says the
    // screen (DEMO.BIN's dispatcher at 0x543050 switches on it: 8 the scenario select, 9 the character select; 0: none)
    uint32_t screenState;
    uint32_t titleState;    // a word saying where the title is: a movie, its "PRESS START", its menus
    uint32_t titleMenus;    // titleState's value on the title menu (File 2 3: 0 a movie, 2 PRESS START; File 1 1: 0 the logos
                            // and movies, 6 5 4 3 PRESS START; its other menus (character log, the options' screens) read 0 too)
    bool titleZeroInMenus;  // File 1: titleState reads 0 in the title menu's own screens (brightness, the character log) too
    uint32_t statusTask;    // the status screen
    uint32_t titleCursor;   // the title menu's item (a word, 0 the first)
    uint32_t statusTab;     // the status screen's tab (a byte: 0 ITEM, 1 SPECIAL, 2 FILE)
    uint32_t statusCursor;  // the status screen's item cursor: a word, the address of the slot it's on (the equipped weapon's is elsewhere)
    uint32_t statusPopup;   // a byte that's nonzero while the item's actions (use, combine, give...) are up
    uint32_t statusKind;    // a byte: what the status task shows (1 the status screen, 2 the map, 4 SELECT's pause menu)
    const PauseItem* pauseRows; // the pause menu's rows
    int pauseRowCount;
    // The title's OPTIONS: the word holding the address of its state while it's up (found by memory diffs of the list's cursor:
    // File 2 gp - 0x6064, File 1 0x23BE40), its rows, and the first row's middle as a fraction of the picture's height
    uint32_t optionsState;
    int optionsItems;
    float optionsTop;
    // Network Play's overlay (as the disc names it), the word holding the address of its screens' state (memory diffs of the
    // NET file's Yes and No: File 2 0x24B4E4, File 1 0x23B618; its first byte the screen), the word holding the address of its
    // lists' state (the configuration list's cursor at 0x10; File 2 0x24B4D0, File 1 0x23C224), and two screens' numbers: the manners screen's Next and Back,
    // the configuration list (0: not found)
    const char* netOverlay;
    uint32_t netState;
    uint32_t netList;
    uint8_t netNextBackScreen;
    uint8_t netConfigScreen;
    // The collection's grid: its task (File 2; 0 for File 1, told by its state), its state's address (File 2 0x8C0068, the title
    // menus' state block; File 1 0x5A4420 in the title overlay's data) and the cursor's and the tab's bytes there
    // (memory diffs of the grid's moves and tabs)
    uint32_t collectionTask;
    uint32_t collectionState;
    uint32_t collectionCursor;
    uint32_t collectionTab;
    uint32_t collectionTopTask; // File 2: the collection's first screen (COLLECTION, DATA CONVERT); 0 none
    uint32_t charLogCursor;     // the character log's cursor (a byte: 0 single play's data, 1 network play's)
    uint32_t collectionFirst; // the first cell the view shows (File 2 0x28, File 1 0x10; dumps of the view scrolled 0, 1, 2 rows)
    // The network browser (the KDDI browser of Network Play, its overlay `browserOverlay` as the disc names it): the word
    // (File 2 0x24B360, File 1 0x23C094) holding the address of its pointer's state, the pointer's x and y (16 bits each, in the 640 x 448
    // picture) at 16 and 18 there (netwk's link-focus code at 0x567370 in File 2 sets them so)
    const char* browserOverlay;
    uint32_t browserPointer;
    uint32_t slotStride;    // the item slots' size: the grid's four are one after the other from where the cursor is as the screen opens
    float titleTop, titleStep; // where the title menu's six items are drawn: the first's middle and the step, as fractions of the picture's height
};

extern const Layout File1;
extern const Layout File2;

void Apply(Ps2GameInfo& game, const Layout& layout);
}
