// Biohazard Outbreak's PC controls: the keyboard and mouse in the layout of the original PC releases of Resident Evil 4 and
// 5 (WASD to move, the mouse's buttons to ready and attack, E for the action, Shift to run, Escape for the game's own pause
// menu: START), each a button of the PS2's controller (Ps2GameInfo's controls). One mode: the game's menus and its play use
// the same buttons (the Japanese game's: circle confirms and cross goes back, as run and action are on the same two). A gamepad
// is the PS2's controller as before; every control can be rebound in the PC menus' Controls
#pragma once

#include "ps2/runtime.h"

namespace BiohazardControls
{
const char* const Modes[] = {"Game"};

inline int Mode() { return 0; }

const Ps2GameControl Controls[] = {
    {"move_forward", "Move forward", "left_up", "W", 0},
    {"move_back", "Move back", "left_down", "S", 0},
    {"move_left", "Move left", "left_left", "A", 0},
    {"move_right", "Move right", "left_right", "D", 0},
    {"walk", "Walk", "walk", "Left Ctrl", 0},
    {"run", "Run, back out of a menu", "cross", "Left Shift, Backspace", 0},
    {"action", "Action, confirm", "circle", "E, Return", 0},
    {"attack", "Attack, use an item", "square", "Mouse Left", 0},
    {"ready", "Ready the weapon", "r1", "Mouse Right", 0},
    {"command", "Command the partner", "triangle", "Space, Tab", 0},
    {"item_previous", "Previous item", "l1", "Q", 0},
    {"item_next", "Next item", "r2", "R", 0},
    {"quick", "Quick action", "l2", "F", 0},
    {"select", "Select", "select", "M", 0},
    {"menu_up", "Menu up, D-pad up", "up", "Up", 0},
    {"menu_down", "Menu down, D-pad down", "down", "Down", 0},
    {"menu_left", "Menu left, D-pad left", "left", "Left", 0},
    {"menu_right", "Menu right, D-pad right", "right", "Right", 0},
};

// The game's own pause (Escape is START) and these controls
inline void Apply(Ps2GameInfo& game)
{
    game.ownPause = 1;
    game.controls = Controls;
    game.controlCount = static_cast<int>(sizeof(Controls) / sizeof(Controls[0]));
    game.modes = Modes;
    game.modeCount = static_cast<int>(sizeof(Modes) / sizeof(Modes[0]));
    game.mode = Mode;
}
}
