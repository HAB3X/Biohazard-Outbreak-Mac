// Skip intro movies (the PC menus' Gameplay tab): the games' start-up sequences end as soon as they begin, as START ends them
#pragma once

#include "ps2/recomp.h"

#include <cstdint>

namespace Intro
{
// Where a game's start-up sequences check START (src/intro.cpp has the evidence)
struct Layout
{
    // The return addresses of the start-up sequences' calls of the check: only those are answered (the check is the demo
    // overlay's, which its other sequences call too)
    uint32_t startUpCalls[2];
};
extern const Layout File1;
extern const Layout File2;

// The Gameplay tab's row (before Mods::Install collects the rows)
void AddSettings();
// A sequence's START check (File 2's demo 0x54F250, File 1's 0x540CB0: the task in a0, nonzero back when START was pressed,
// the task's flag set): from a start-up sequence with Skip intro movies on, START taken as pressed; else the game's own check
void StartPressed(Ps2Cpu* c, void (*check)(Ps2Cpu*), const Layout& layout);
}
