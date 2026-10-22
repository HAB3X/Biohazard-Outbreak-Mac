// Widescreen (16:9) in both games (src/widescreen.cpp): their 3D's projection made for a picture 16:9 wide
#pragma once

#include "ps2/recomp.h"

namespace Widescreen
{
// The games' one projection function (File 2's 0x18F4C0, File 1's 0x18F0E0; config/fileN.toml's overrides): its aspect made
// widescreen's while the picture is (ps2_widescreen), then its recompiled body run (set)
void SetProjection(Ps2Cpu* c, void (*set)(Ps2Cpu*));
}
