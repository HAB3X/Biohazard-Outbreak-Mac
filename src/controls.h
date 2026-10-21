// Biohazard Outbreak's PC controls: the keyboard and mouse in the layout of the original PC releases of Resident Evil 4 and 5
// (WASD to move, the mouse's right button to ready or aim the weapon and its left to fire, E for the action, Shift to run, Tab
// for the inventory), each a button of the PS2's controller (Ps2GameInfo's controls), in modes the game's own memory says: play,
// its menus, its pause menu, and the title and movies (Escape or Return is START there, which skips them). In the menus the
// mouse points at the game's own items (src/pointer.h, src/menus.h). A gamepad is the PS2's controller as before.
// docs/controls.md has the layout; every control can be rebound in the PC menus' Controls (F10 opens the PC options)
#pragma once

#include "ps2/runtime.h"

#include <cstdint>

namespace BiohazardControls
{
// Where one game's memory keeps what the modes are told by (the executable's data and the game overlay's, at the addresses
// the game loads them to). The game keeps its active tasks (the game's loop, the title, a menu, the status screen...) in a
// list of up to 8 entries, each the address of the task's function, with the count in a word of its own
struct Layout
{
    uint32_t taskCount, taskList;
    uint32_t gameTask;      // the in-game loop (play)
    uint32_t cutsceneTask;  // the game's loop while a cutscene plays
    // The game overlay's own state byte (0: none), which its main function switches on (File 1's GAME.BIN at 0x5CD67C: 2
    // in play), and its value while that function plays the opening credits itself with no cutscene task (14, handled at
    // 0x5D2A90): a scene there is the title's
    uint32_t playState;
    uint32_t openingState;
    uint32_t baseTask;      // the executable's own loop (movies, the title and loading, and under the game)
    uint32_t statusTask;    // the status screen, the map and SELECT's pause menu (over play)
    uint32_t statusKind;    // a byte: what the status task shows (1 the status screen, 2 the map, 4 SELECT's pause menu)
    // The title's OPTIONS: the word holding the address of its state while it's up (its byte 1 says the list or one of its
    // screens, its 16-bit word at 4 the list's row)
    uint32_t optionsState;
};

extern const Layout File1;
extern const Layout File2;

void Apply(Ps2GameInfo& game, const Layout& layout);
}
