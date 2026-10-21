// The first-person camera: the camera in the player's character's eyes (the head bone's matrix applied to a spot measured
// for each of the eight characters), looking where the mouse or the right stick points the view; she faces where you look and
// the movement keys or the left stick walk her relative to the view; while she aims, the view looks along her aim (with Kevin's
// 45 Auto and Mark's handgun, along their sights). Chosen in the PC menus' Camera tab (View: First person). The game's camera
// handlers are held by the same overrides as the Modern camera's (src/modern_camera.cpp), which drives this one.
//
// Adapted from REO-FirstPerson 2.6.8 by RaccoonRecomp (MIT): its first_person.c, every measure and rule kept; the mouse added
// as a look like the right stick's; the restroom mirrors and the On / Off key left out (docs/mods.md).
#pragma once

#include "modern_camera_game.h"

#include <cstdint>

namespace FirstPerson
{
// The input of a frame: the right stick (-1..1, +y up; 0 while the PC menus are up) and the mouse's motion turned to degrees
struct Look
{
    float stickX = 0, stickY = 0;
    float mouseYawDegrees = 0, mousePitchDegrees = 0;
};

// One game frame in play (the game overlay in play, not a cutscene): the camera put into her eyes. True while it drives (the
// caller holds the game's camera handlers then); false while it waits (no skeleton, or the game took the camera for a scene
// until she moves on)
bool Frame(ModernCamera::Game& game, const Look& look);
// Everything handed back (the game's camera of the room when restoreCamera, the hide radius, the artwork, the crosshair)
void HandBack(ModernCamera::Game& game, bool restoreCamera);
// At the button mapper for her record: her heading and her stick's direction as the view wants them, R3 kept from the ad-libs
void PadMap(ModernCamera::Game& game);
// Whether the game's right stick is to be held centred for her at the mapper (the view's stick)
bool CentreRightStick();
}
