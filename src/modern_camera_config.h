// The Modern camera's settings: the rows of the PC menus' Camera tab and the values the camera reads each game frame (changes
// apply at the next frame). Defaults and ranges are ModernCam's File 1 ones; the values the camera tab doesn't offer keep them.
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): config.c,
// mcam_config.h and tools/settings.py.
#pragma once

#include "modern_camera_rig.h"

#include <cstdint>

namespace ModernCamera
{
enum class View : int
{
    Classic,
    Modern,
    FirstPerson,
};

enum class TurnMode : int
{
    Off,
    Moving,
    Always,
};

// An aim or alternate rig's value used only while it's set (ModernCam's per-value inheritance)
struct RigOverride
{
    bool distance = false, shoulder = false, height = false, fov = false, lookDrop = false, pitchTrim = false;
    Rig rig;
};

struct Config
{
    View view = View::Classic;

    Rig hip;
    RigOverride aim;
    // The Alt view's rig (L3+R3 toggles it), each value the main one's unless set
    RigOverride alt;

    // Following
    bool keepBehind = false;
    float followStrength = 0.12f;
    float freeLookReturn = 1.2f;
    float followHold = 0.18f;
    bool followBehind = true;
    float yawOffset = 0;
    float shoulderSwapTime = 0.18f;
    float recenterTime = 0.25f;

    // Look
    float stickSpeed = 140;
    float mouseSensitivity = 0.15f;
    float mouseSmoothing = 0.35f; // the share of last frame's mouse motion kept (0: none)
    float pitchSpeed = 90;
    bool invertPitch = false;
    bool invertStick = false;
    float pitchMin = -35;
    float pitchMax = 70;

    // Aiming
    bool modernAim = true;
    bool freeAim = true;
    bool aimPitchLock = true;
    float aimSensitivity = 0.55f;
    float gunPitchMax = 45;
    float aimZeroDeg = -0.1f;
    float aimStrafeSpeed = 45;
    float aimBlend = 0.25f;
    float aimBlendIn = 0.35f;
    float aimBlendOut = 0.15f;
    bool aimFollow = true;
    float aimFollowStrength = 0.3f;
    bool aimFreeLook = true;
    bool aimSnapBehind = true;
    float aimSnapSpeed = 0.5f;
    float aimShoulderMin = 0;
    float modernAimDeadzone = 12;
    bool lockOnEnemy = true;

    // The crosshair
    bool crosshair = true;
    int crosshairStyle = 0; // 0 a dot, 1 a cross
    float crosshairSize = 3;
    float crosshairGap = 3;
    float crosshairThickness = 2;
    uint32_t crosshairRgb = 0xF2F2F2;

    bool ownLookStick = true;
    // She turns to the way the stick points from the camera as she moves, quickly (RE4's and RE5's feel); ModernCam's default
    // was Off, the game's own slower walking turns
    TurnMode turnToCamera = TurnMode::Moving;
    float turnSpeed = 540;

    // Collision
    bool camCollision = true;
    float camMargin = 55;
    float camBlockTop = 25;
    float camRecover = 0.1f;
    float camHold = 0.12f;
    float camSnap = 0; // the pull in front of a wall at once (eased, the eye was beyond the wall for a few frames)

    // Projection
    float nearValue = 50;
    float farPlane = 0;

    // The room
    bool cutMatch = true;
    float cutMatchRadius = 250;
    uint32_t cutMatchMaxCuts = 6;
    bool showHiddenObjects = false;
    float drawDistance = 1.0f;
    bool charLight = false;
    uint32_t charLightRgb = 0x808080;

    // Doors: the game's moves of the camera taken back
    bool doorHold = true;
};

// The Camera tab's name, and its rows (src/mod_settings.h)
constexpr const char* SettingsTab = "Camera";
void AddSettings();
// The settings now
Config ReadConfig();
// The rigs for this frame: the main one, and the aim one with its set values over it (an aim field of view of 0: the main's)
// (with altView the main rig is the Alt view's)
void Presets(const Config& config, bool altView, Rig* hip, Rig* aim);
}
