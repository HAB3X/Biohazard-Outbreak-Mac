// The Modern camera's settings (modern_camera_config.h): the Camera tab
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): the settings and their
// File 1 defaults (tools/settings.py), each row a few of the range's steps.
#include "modern_camera_config.h"

#include "mod_settings.h"

#include <iterator>

namespace ModernCamera
{
namespace
{
constexpr const char* Tab = "Camera";
constexpr const char* GroupView = "ModernCam by Snippy; ported, and First Person, by RaccoonRecomp";
constexpr const char* GroupFraming = "Modern: framing";
constexpr const char* GroupLook = "Modern: look";
constexpr const char* GroupAim = "Modern: aiming";
constexpr const char* GroupRoom = "Modern: the room";

const char* const OffOn[] = {"Off", "On", nullptr};
const char* const OnOff[] = {"On", "Off", nullptr};

// A row whose choices are numbers (its labels and values side by side)
struct Numbers
{
    const char* const* labels;
    const float* values;
};

const char* const ViewLabels[] = {"Classic", "Modern", "First person", nullptr};

const char* const DistanceLabels[] = {"100", "150", "200", "250", "300", "350", "400", "450", "500", "600", nullptr};
const float DistanceValues[] = {100, 150, 200, 250, 300, 350, 400, 450, 500, 600};
const char* const SideLabels[] = {"Right", "Left", nullptr};
const char* const OffsetLabels[] = {"0", "25", "50", "75", "100", "125", "150", nullptr};
const float OffsetValues[] = {0, 25, 50, 75, 100, 125, 150};
const char* const HeightLabels[] = {"50", "70", "90", "110", "130", "150", "170", "190", "210", "250", nullptr};
const float HeightValues[] = {50, 70, 90, 110, 130, 150, 170, 190, 210, 250};
const char* const FovLabels[] = {"The room's", "40", "45", "50", "55", "60", "65", "70", "75", "80", "90", nullptr};
const float FovValues[] = {0, 40, 45, 50, 55, 60, 65, 70, 75, 80, 90};
const char* const AimDistanceLabels[] = {"As the main", "100", "150", "200", "250", "300", "400", nullptr};
const float AimDistanceValues[] = {-1, 100, 150, 200, 250, 300, 400};
const char* const AimFovLabels[] = {"As the main", "30", "40", "45", "50", "55", "60", nullptr};
const float AimFovValues[] = {-1, 30, 40, 45, 50, 55, 60};
const char* const TurnLabels[] = {"Off", "Moving", "Always", nullptr};
const char* const MouseLabels[] = {"0.05", "0.08", "0.10", "0.12", "0.15", "0.20", "0.25", "0.30", "0.40", "0.60", nullptr};
const float MouseValues[] = {0.05f, 0.08f, 0.10f, 0.12f, 0.15f, 0.20f, 0.25f, 0.30f, 0.40f, 0.60f};
const char* const StickLabels[] = {"60", "80", "100", "120", "140", "160", "200", "250", "300", nullptr};
const float StickValues[] = {60, 80, 100, 120, 140, 160, 200, 250, 300};
const char* const AimSensLabels[] = {"0.25", "0.40", "0.55", "0.70", "0.85", "1.00", "1.25", "1.50", nullptr};
const float AimSensValues[] = {0.25f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f, 1.25f, 1.50f};
const char* const CrosshairLabels[] = {"Off", "Dot", "Cross", nullptr};
const char* const CrossSizeLabels[] = {"1", "2", "3", "4", "5", "6", "8", "10", nullptr};
const float CrossSizeValues[] = {1, 2, 3, 4, 5, 6, 8, 10};
const char* const ColourLabels[] = {"White", "Green", "Red", "Yellow", "Cyan", nullptr};
const uint32_t ColourValues[] = {0xF2F2F2, 0x40F040, 0xF04040, 0xF0E040, 0x40E0F0};

// The rows' indices
struct Rows
{
    int view, distance, side, offset, height, fov, aimDistance, aimFov, keepBehind, turnToCamera;
    int mouse, stick, invert, modernAim, freeAim, lockOn, aimSens, crosshair, crossSize, crossColour;
    int collision, cutMatch, showHidden, charLight;
} g_rows;

template <size_t N>
int IndexOf(const float (&values)[N], float value)
{
    for (size_t i = 0; i < N; i++)
    {
        if (values[i] == value)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

template <size_t N>
float ValueOf(const float (&values)[N], int index)
{
    return values[index >= 0 && static_cast<size_t>(index) < N ? index : 0];
}

int Add(const char* group, const char* name, const char* description, const char* const* choices, int initial)
{
    return ModSettings::Add({Tab, group, name, description, choices, initial});
}
}

void AddSettings()
{
    const Config d;
    g_rows.view = Add(GroupView, "View",
                      "Classic: the game's fixed cameras. Modern: over her shoulder. First person: in her eyes",
                      ViewLabels, 0);
    g_rows.distance = Add(GroupFraming, "Distance", "How far behind her the camera sits", DistanceLabels,
                          IndexOf(DistanceValues, d.hip.distance));
    g_rows.side = Add(GroupFraming, "Shoulder", "The shoulder it looks over (in play the middle mouse button, X or R3 swaps it)",
                      SideLabels, 0);
    g_rows.offset = Add(GroupFraming, "Shoulder offset", "How far to her side the camera sits", OffsetLabels,
                        IndexOf(OffsetValues, d.hip.shoulder));
    g_rows.height = Add(GroupFraming, "Height", "Raises the camera and what it looks at together", HeightLabels,
                        IndexOf(HeightValues, d.hip.height));
    g_rows.fov = Add(GroupFraming, "Field of view", "Up and down, in degrees (the room's: each fixed camera's own)", FovLabels,
                     IndexOf(FovValues, d.hip.fovDeg));
    g_rows.aimDistance = Add(GroupFraming, "Aim distance", "How far behind her the camera sits while she aims",
                             AimDistanceLabels, 0);
    g_rows.aimFov = Add(GroupFraming, "Aim field of view", "The field of view while she aims (a zoom)", AimFovLabels, 0);
    g_rows.keepBehind = Add(GroupFraming, "Keep behind her",
                            "On: it swings back behind her as she moves. Off: it stays where you turn it",
                            OffOn, d.keepBehind ? 1 : 0);
    g_rows.turnToCamera = Add(GroupFraming, "Turn her to the camera",
                              "Moving: she faces the way you push from the view. Always: where the camera looks",
                              TurnLabels, static_cast<int>(d.turnToCamera));
    g_rows.mouse = Add(GroupLook, "Mouse sensitivity", "How far the camera turns with the mouse's motion",
                       MouseLabels, IndexOf(MouseValues, d.mouseSensitivity));
    g_rows.stick = Add(GroupLook, "Stick sensitivity", "Degrees a second at the right stick's full push", StickLabels,
                       IndexOf(StickValues, d.stickSpeed));
    g_rows.invert = Add(GroupLook, "Invert up and down", "", OffOn, d.invertPitch ? 1 : 0);
    g_rows.modernAim = Add(GroupAim, "Modern aim",
                           "While she aims she faces where the camera looks, and can walk",
                           OnOff, d.modernAim ? 0 : 1);
    g_rows.freeAim = Add(GroupAim, "Free gun elevation",
                         "The gun follows the camera up and down, not the game's three angles", OnOff,
                         d.freeAim ? 0 : 1);
    g_rows.lockOn = Add(GroupAim, "Lock on enemy",
                        "The camera turns onto the enemy the game's aim picks and follows it", OnOff,
                        d.lockOnEnemy ? 0 : 1);
    g_rows.aimSens = Add(GroupAim, "Aim sensitivity", "The look's speed while she aims, times this", AimSensLabels,
                         IndexOf(AimSensValues, d.aimSensitivity));
    g_rows.crosshair = Add(GroupAim, "Crosshair", "Shown while she aims, over the picture's middle", CrosshairLabels, 1);
    g_rows.crossSize = Add(GroupAim, "Crosshair size", "The dot's radius or the cross's arms", CrossSizeLabels,
                           IndexOf(CrossSizeValues, d.crosshairSize));
    g_rows.crossColour = Add(GroupAim, "Crosshair colour", "", ColourLabels, 0);
    g_rows.collision = Add(GroupRoom, "Camera collision", "The camera stops in front of walls instead of going through them", OnOff,
                           d.camCollision ? 0 : 1);
    g_rows.cutMatch = Add(GroupRoom, "Match the room's cameras",
                          "The room is drawn for its fixed camera nearest the view", OnOff,
                          d.cutMatch ? 0 : 1);
    g_rows.showHidden = Add(GroupRoom, "Show hidden objects", "Objects the fixed cameras hide stay drawn", OffOn,
                            d.showHiddenObjects ? 1 : 0);
    g_rows.charLight = Add(GroupRoom, "Character light", "A grey light on the characters (ModernCam's override)", OffOn,
                           d.charLight ? 1 : 0);
}

Config ReadConfig()
{
    using ModSettings::Choice;
    Config c;
    int view = Choice(g_rows.view);
    c.view = view == 1 ? View::Modern : view == 2 ? View::FirstPerson : View::Classic;
    c.hip.distance = ValueOf(DistanceValues, Choice(g_rows.distance));
    c.hip.shoulder = ValueOf(OffsetValues, Choice(g_rows.offset)) * (Choice(g_rows.side) == 1 ? -1.0f : 1.0f);
    c.hip.height = ValueOf(HeightValues, Choice(g_rows.height));
    c.hip.fovDeg = ValueOf(FovValues, Choice(g_rows.fov));
    float aimDistance = ValueOf(AimDistanceValues, Choice(g_rows.aimDistance));
    c.aim.distance = aimDistance > 0;
    c.aim.rig.distance = aimDistance;
    float aimFov = ValueOf(AimFovValues, Choice(g_rows.aimFov));
    c.aim.fov = aimFov > 0;
    c.aim.rig.fovDeg = aimFov;
    c.keepBehind = Choice(g_rows.keepBehind) == 1;
    c.turnToCamera = static_cast<TurnMode>(Choice(g_rows.turnToCamera) % 3);
    c.mouseSensitivity = ValueOf(MouseValues, Choice(g_rows.mouse));
    c.stickSpeed = ValueOf(StickValues, Choice(g_rows.stick));
    c.invertPitch = Choice(g_rows.invert) == 1;
    c.modernAim = Choice(g_rows.modernAim) == 0;
    c.freeAim = Choice(g_rows.freeAim) == 0;
    c.lockOnEnemy = Choice(g_rows.lockOn) == 0;
    c.aimSensitivity = ValueOf(AimSensValues, Choice(g_rows.aimSens));
    c.crosshair = Choice(g_rows.crosshair) != 0;
    c.crosshairStyle = Choice(g_rows.crosshair) == 2 ? 1 : 0;
    c.crosshairSize = ValueOf(CrossSizeValues, Choice(g_rows.crossSize));
    int colour = Choice(g_rows.crossColour);
    c.crosshairRgb = ColourValues[colour >= 0 && colour < static_cast<int>(std::size(ColourValues)) ? colour : 0];
    c.camCollision = Choice(g_rows.collision) == 0;
    c.cutMatch = Choice(g_rows.cutMatch) == 0;
    c.showHiddenObjects = Choice(g_rows.showHidden) == 1;
    c.charLight = Choice(g_rows.charLight) == 1;
    return c;
}

void Presets(const Config& config, Rig* hip, Rig* aim)
{
    *hip = config.hip;
    *aim = *hip;
    const RigOverride& o = config.aim;
    if (o.distance) aim->distance = o.rig.distance;
    if (o.shoulder) aim->shoulder = o.rig.shoulder;
    if (o.height) aim->height = o.rig.height;
    if (o.fov) aim->fovDeg = o.rig.fovDeg;
    if (o.lookDrop) aim->lookDrop = o.rig.lookDrop;
    if (o.pitchTrim) aim->pitchTrim = o.rig.pitchTrim;
    if (!(aim->fovDeg != 0.0f))
    {
        aim->fovDeg = hip->fovDeg; // ModernCam: "if not aim['fov_deg']"
    }
}
}
