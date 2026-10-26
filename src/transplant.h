// File 2's enemies in File 1: the AI of kinds File 1 has none of, carried from File 2's code (tools/bh_transplant.py) and
// recompiled with File 1 as its generated overlay "transplant" (config/file1.toml)
#pragma once

#include "ps2/recomp.h"

namespace Transplant
{
// The enemies' stand-in routine (File 1's game 0x601ED0, the one the creation gives a kind with no AI: the enemy in a0): a
// carried kind runs its own routine, any other the game's stand-in
void StandIn(Ps2Cpu* c, Ps2Function original);
}
