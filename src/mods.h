// The community mods ported to this game: the Modern camera (src/modern_camera.h) on the PC menus' Camera tab and the cheats
// (src/cheats.h) on its Cheats tab. Installed into
// the game's description before it runs: their rows after the game's own settings, the mouse's look while the camera drives,
// and the camera's own control (swap shoulder) among play's controls.
#pragma once

#include "cheats.h"
#include "controls.h"
#include "modern_camera_game.h"

#include "ps2/runtime.h"

namespace Mods
{
// After the game's own settings, controls and mouse are in `game` (BiohazardControls::Apply, the partners' AI)
void Install(Ps2GameInfo& game, const ModernCamera::Layout& camera, const BiohazardControls::Layout& play,
             const Cheats::Layout& cheats);
// The game frame's end (the override of the executable's frame function: `original` as recompiled), then the mods' frame
void FrameEnd(Ps2Cpu* c, Ps2Function original);
}
