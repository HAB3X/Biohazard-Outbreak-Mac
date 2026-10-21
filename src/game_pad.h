// Port 0's DualShock as the game reads it (libpad2's frames in the game's memory): what the mods read of the player's buttons
// and sticks, whichever device made them (the keys and the mouse press PS2 buttons too).
#pragma once

#include <cstdint>

namespace GamePad
{
// PS2 buttons, active high
constexpr uint32_t Select = 0x0001, L3 = 0x0002, R3 = 0x0004, Start = 0x0008;
constexpr uint32_t Up = 0x0010, Right = 0x0020, Down = 0x0040, Left = 0x0080;
constexpr uint32_t L2 = 0x0100, R2 = 0x0200, L1 = 0x0400, R1 = 0x0800;
constexpr uint32_t Triangle = 0x1000, Circle = 0x2000, Cross = 0x4000, Square = 0x8000;

struct State
{
    bool valid = false;
    uint32_t held = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0; // -1..1, +y up, within ModernCam's dead zone (7000 of 32767) read as 0
};

// The newest of the two frames the word at `framesPointer` points at (as the game's own getter picks it)
State Read(uint32_t framesPointer);
}
