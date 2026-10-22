// The Modern camera: an over-the-shoulder camera behind the player's character, turned with the mouse or the right stick, with
// free aim, a crosshair and the room's walls in its way. While it drives, the game's own camera handlers are held by native
// overrides of them (config/fileN.toml): the fixed cut's skipped, the rail cuts' camera writes undone, the follow handler
// skipped, and with free aim the gun's servo's writes to her elevation undone; the camera is written each game frame from the
// frame's own function, and her pad record is adjusted at the button mapper. Cutscenes, scripted cameras and every screen but
// play are the game's own: it hands the camera back there. Chosen in the PC menus' Camera tab (View: Classic, Modern).
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT), the camera written for
// this very disc: its driver.c, config.c and settings (tools/settings.py), the host layer made the runtime's (docs/mods.md).
#pragma once

#include "controls.h"
#include "modern_camera_game.h"

#include "ps2/recomp.h"

#include <cstdint>

namespace ModernCamera
{
// The game this program is: its camera's layout and its play's (the task list, from the PC controls)
void Use(const Layout& layout, const BiohazardControls::Layout& play);

// The overrides (each `original` the game's function as recompiled)
// Once a game frame, at its end (src/mods.h): the camera computed and written
void Frame();
void PadMap(Ps2Cpu* c, Ps2Function original);      // the button mapper (a0 a character's record)
void WalkStep(Ps2Cpu* c, Ps2Function original);    // a character's stick walking (a0 its record): no cut hold while the camera drives
void FixedCut(Ps2Cpu* c, Ps2Function original);    // the fixed cut's handler: skipped while the camera drives
void RailCut(Ps2Cpu* c, Ps2Function original);     // the rail and spline cuts' handlers: their camera writes undone
void Follow(Ps2Cpu* c, Ps2Function original);      // the follow handler (doors): skipped while the camera drives
void GunElevation(Ps2Cpu* c, Ps2Function original); // the gun's elevation (a0 a record): free aim keeps the player's

// Ps2GameInfo::mouseStick while the camera drives in play: the mouse turns it (PS2_MOUSE_LOOK); otherwise the game's own
int MouseStick(uint32_t pressed, int (*game)(uint32_t));
}
