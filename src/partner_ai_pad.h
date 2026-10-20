// The characters' virtual pad: the game's own button bits, which the character's update reads (the player's pad mapped to
// them by the executable's pad code, File 1's 0x1A5880 through the table at 0x2135A0: bit n is that table's nth PS2 button),
// and which the partners' think writes. The roles are the game's controller type A's (docs/controls.md)
#pragma once

#include <cstdint>

namespace PartnerAi::Pad
{
constexpr uint32_t Forward = 1u << 0;  // d-pad up: walk ahead (with Run: run)
constexpr uint32_t Back = 1u << 1;     // d-pad down: step back (with Run: the quick turn)
constexpr uint32_t Left = 1u << 2;     // turn left
constexpr uint32_t Right = 1u << 3;    // turn right
constexpr uint32_t Run = 1u << 4;      // cross: run, cancel
constexpr uint32_t Attack = 1u << 5;   // circle: the action; with Ready held, attack or fire
constexpr uint32_t ReadyForward = 1u << 6; // R2
constexpr uint32_t Appeal = 1u << 7;   // L2: the appeals' shift
constexpr uint32_t AdLib = 1u << 8;    // square
constexpr uint32_t Map = 1u << 9;      // triangle
constexpr uint32_t Ready = 1u << 10;   // R1: ready the weapon (held)
constexpr uint32_t Reload = 1u << 11;  // L1
}
