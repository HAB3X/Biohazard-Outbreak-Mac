// The Modern camera (modern_camera.h): ModernCam's camera run once a game frame from the frame's own function.
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): driver.c. The camera's
// logic, constants and order are theirs; the host layer is the runtime's: the game's camera code held by overrides instead of
// feature switches, the input read from the PS2 controller the game is given (and the mouse's own motion), the crosshair drawn
// by the PC menus' overlay. Left out: the camera's own buttons the game's controls already have here (aim is the game's R1,
// shoot its fire button, reload its L1, sprint its run), its reload call, the alternate rig, the reveal and watch-next-player
// aids, and the J's Bar restroom mirrors (docs/mods.md).
#include "modern_camera.h"

#include "modern_camera_config.h"

#include "ps2/iop.h"
#include "ps2/runtime.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

namespace ModernCamera
{
namespace
{
constexpr float MaxDt = 0.1f;
constexpr float RefreezeDistance = 25.0f; // the game moved the camera when the eye is this far from ours
constexpr float HeadingEpsilon = 0.4f;    // degrees: smaller heading corrections aren't written
constexpr float LookThreshold = 0.12f;
constexpr float MoveDeadzone = 0.2f;
constexpr uint32_t MaxCuts = 64;
constexpr float LockTurn = 0.25f;         // the aim lock turns the camera onto its target by SmoothAlpha(this) a frame
constexpr float LockMinDistance = 40.0f;  // a target nearer than this (holding her) keeps the lock's last direction
constexpr uint32_t LockGrace = 3;         // game frames the lock holds through a gap (after a hit the game aims again)
constexpr float RelocateDistance = 100.0f; // a step longer than this in a frame: the game put her somewhere else
constexpr float VblankHz = 59.94f;

// What the camera does to her pad record at the mapper this frame
struct PadTransform
{
    bool centreLeft = false, centreRight = false;
    bool walkFree = false; // the game's own stick walking, relative to this camera
    int walkSide = 0;      // the side WalkTurn turned her to last frame
    bool active = false;
};

// The input of this frame: the PS2 controller the game is given (the keys and the gamepad both make it) and the mouse's motion
struct Input
{
    bool mouse = false; // the keyboard and mouse were used last
    uint32_t held = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    float mouseDx = 0, mouseDy = 0;
    bool menusOpen = false;
};

struct State
{
    bool init = false;
    uint32_t prevFrames = 0;
    float now = 0;

    bool levelLive = false;
    bool frozen = false;
    bool freeAimOn = false;
    float nearScale = 1.0f;
    bool lightOn = false;
    bool haveClip = false;
    float nearOriginal = 0, farOriginal = 0;

    float camYaw = 0, pitch = 0, lookOff = 0, camT = 1.0f, camHoldTimer = 0, aimAmount = 0;
    bool aimPrev = false, aimWas = false;
    float recenterTimer = 0;
    bool haveShoulder = false;
    float shoulderTarget = 1, shoulderSign = 1;
    FollowHeading follow;

    uint32_t prevHeld = 0;
    PadTransform map;

    bool haveSteerYaw = false;
    float steerYaw = 0;

    // Seconds since the player last moved her: outside the aim lock the camera turns with her only while the player moves her,
    // and its boom grows back after a wall only when the player opened the space
    float moveIdle = 1e9f;
    bool boomReturn = false;
    bool havePrevPos = false;
    float prevX = 0, prevZ = 0;

    // The game's aim lock
    bool locked = false;
    float lockSide = 1;
    bool haveLockYaw = false;
    float lockYaw = 0;
    bool haveLockDist = false;
    float lockDist = 0;
    uint32_t lockMiss = 0;
    Vec3 lockPos;
    bool lockReturn = false;

    // Doors
    bool haveLastWritten = false;
    Vec3 lastWritten;
    uint32_t divergences = 0;

    // Cut matching
    int cutForced = -1, cutCandidate = -1;
    float cutCandidateT = 0;
    bool haveCutEyes = false;
    uint32_t cutEyesN = 0;
    float cutEyesT = 0;
    Vec3 cutEyes[MaxCuts];

    const char* status = "off";
};

State s;
Game* g_game = nullptr;
const BiohazardControls::Layout* g_play = nullptr;
// What the overrides read: the camera holds the game's camera, free aim holds her gun's elevation
bool g_freeze = false;
bool g_freeAim = false;
// The mouse turns the camera (asked on the presenter's thread)
std::atomic<bool> g_mouseLook{false};

float Hypot(float x, float y) { return std::hypot(x, y); }
float DistXz(const Vec3& a, const Vec3& b) { return Hypot(a.x - b.x, a.z - b.z); }
float Dist3(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

void SetStatus(const char* status)
{
    if (std::strcmp(status, s.status) != 0)
    {
        Ps2::Log("modern camera: %s", status);
        s.status = status;
    }
}

bool HasTask(uint32_t task)
{
    uint32_t n = std::min<uint32_t>(rd32(g_play->taskCount), 8);
    for (uint32_t i = 0; i < n; i++)
    {
        if (rd32(g_play->taskList + 4 * i) == task)
        {
            return true;
        }
    }
    return false;
}

// Play, where the camera may drive: the game's loop without a cutscene over it (and File 1's game overlay in its play state,
// not its opening credits). The status screen, the map and the pause menu over play keep the camera's view
bool InPlay()
{
    if (!HasTask(g_play->gameTask) || HasTask(g_play->cutsceneTask))
    {
        return false;
    }
    return g_play->playState == 0 || rd8(g_play->playState) != g_play->openingState;
}

void InputNeutral()
{
    s.map = PadTransform{};
    g_mouseLook = false;
}

void CrosshairOff()
{
    Ps2Crosshair x{};
    ps2_set_crosshair(&x);
}

void ResetRigToPlayer()
{
    s.follow.Reset();
    s.camYaw = g_game->PlayerYaw() + 180.0f; // seeded behind her whatever the follow side says
    s.lookOff = 0;
    s.pitch = 0;
    s.haveSteerYaw = false;
    s.haveLastWritten = false;
    s.camT = 1.0f; // the boom starts at its length and the walls pull it in at once
    s.boomReturn = false;
    s.lockReturn = false;
    s.havePrevPos = false;
}

// The room's own camera for the current cut put back (its pose, field of view and clip values), so the game doesn't keep the
// view until its next cut change
void RestoreEngineCamera()
{
    Vec3 eye, look;
    float fov;
    if (g_game->CutPose(&eye, &look, &fov))
    {
        g_game->WriteCamera(eye, look);
        if (std::isfinite(fov) && fov > 0)
        {
            g_game->SetFov(fov);
        }
    }
    if (s.haveClip)
    {
        g_game->SetNearFar(s.nearOriginal, s.farOriginal);
        s.haveClip = false;
    }
}

void Unfreeze(bool restoreCamera)
{
    if (s.frozen)
    {
        s.frozen = false;
        g_freeze = false;
        if (restoreCamera)
        {
            RestoreEngineCamera();
        }
    }
}

void Freeze()
{
    if (!s.haveClip)
    {
        s.nearOriginal = g_game->NearValue();
        s.farOriginal = g_game->FarPlane();
        s.haveClip = true;
    }
    g_freeze = true;
    s.frozen = true;
}

void LockRelease()
{
    if (!s.locked)
    {
        s.haveLockDist = false;
    }
    s.locked = false;
    s.lockMiss = 0;
    s.haveLockYaw = false;
}

// Everything the camera changed undone (ModernCam's shutdown's order)
void HandBackAll()
{
    InputNeutral();
    CrosshairOff();
    if (s.cutForced >= 0)
    {
        g_game->ReleaseCut();
    }
    s.cutForced = s.cutCandidate = -1;
    if (s.freeAimOn)
    {
        g_game->SetGunElevation(0);
        s.freeAimOn = false;
    }
    g_freeAim = false;
    g_game->RestoreNearHide();
    s.nearScale = 1.0f;
    if (s.lightOn)
    {
        g_game->RestoreEntityLight();
        s.lightOn = false;
    }
    g_game->RestoreAspect();
    LockRelease();
    g_game->RestoreArtwork();
    Unfreeze(s.levelLive);
    s.levelLive = false;
}

void SyncEngineState(const Config& c)
{
    float wantNear = std::clamp(c.drawDistance, 0.1f, 1.0f);
    bool wantFreeAim = c.modernAim && c.freeAim;
    // The near-camera hide radii: shrunk below 1 as ModernCam does, never enlarged (that hid nearby characters)
    if (std::fabs(wantNear - s.nearScale) > 1e-6f)
    {
        if (wantNear >= 1.0f)
        {
            g_game->RestoreNearHide();
        }
        else
        {
            g_game->SetNearHideScale(wantNear);
        }
        s.nearScale = wantNear;
    }
    // The game writes the flags at every cut change, so holding them means writing them again
    if (c.showHiddenObjects)
    {
        g_game->ShowAllObjects();
    }
    if (wantFreeAim != s.freeAimOn)
    {
        if (!wantFreeAim)
        {
            g_game->SetGunElevation(0);
        }
        s.freeAimOn = wantFreeAim;
    }
    g_freeAim = s.freeAimOn;
    if (c.charLight)
    {
        g_game->SetEntityLight(c.charLightRgb);
        s.lightOn = true;
    }
    else if (s.lightOn)
    {
        g_game->RestoreEntityLight();
        s.lightOn = false;
    }
}

// The game moved the camera (a door, a scripted shot): taken back
void CheckCameraTaken(const Config& c)
{
    if (!c.doorHold || !s.haveLastWritten)
    {
        return;
    }
    if (Dist3(g_game->CameraEye(), s.lastWritten) <= RefreezeDistance)
    {
        s.divergences = 0;
        return;
    }
    if (++s.divergences < 2)
    {
        return;
    }
    Freeze();
    if (c.showHiddenObjects)
    {
        g_game->ShowAllObjects();
    }
    s.divergences = 0;
}

void CutMatch(const Config& c, const Vec3& eye)
{
    if (!c.cutMatch)
    {
        if (s.cutForced >= 0)
        {
            g_game->ReleaseCut();
        }
        s.cutForced = s.cutCandidate = -1;
        return;
    }
    uint32_t n = g_game->CutCount();
    if (!s.haveCutEyes || n != s.cutEyesN || s.now - s.cutEyesT > 2.0f)
    {
        if (n != s.cutEyesN)
        {
            s.cutForced = s.cutCandidate = -1;
        }
        for (uint32_t i = 0; i < n && i < MaxCuts; i++)
        {
            g_game->CutEye(i, &s.cutEyes[i]);
        }
        s.haveCutEyes = true;
        s.cutEyesN = n;
        s.cutEyesT = s.now;
    }
    n = std::min(n, MaxCuts);
    if (n > c.cutMatchMaxCuts)
    {
        // Too many cuts to match by position
        if (s.cutForced >= 0)
        {
            g_game->ReleaseCut();
            s.cutForced = s.cutCandidate = -1;
        }
        return;
    }
    int want = -1;
    float best = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        float d = DistXz(s.cutEyes[i], eye);
        if (want < 0 || d < best)
        {
            want = static_cast<int>(i);
            best = d;
        }
    }
    if (want < 0)
    {
        if (s.cutForced >= 0)
        {
            g_game->ReleaseCut();
            s.cutForced = s.cutCandidate = -1;
        }
    }
    else if (s.cutForced < 0)
    {
        uint32_t engine = g_game->CutIndex();
        float dEngine = engine < n ? DistXz(s.cutEyes[engine], eye) : 1e30f;
        if (want != s.cutCandidate)
        {
            s.cutCandidate = want;
            s.cutCandidateT = s.now;
        }
        if (best <= c.cutMatchRadius && best < dEngine * 0.5f && s.now - s.cutCandidateT >= 0.35f &&
            static_cast<uint32_t>(want) != engine)
        {
            g_game->ForceCut(static_cast<uint32_t>(want));
            s.cutForced = want;
        }
    }
    else if (DistXz(s.cutEyes[s.cutForced], eye) > c.cutMatchRadius * 1.5f)
    {
        g_game->ReleaseCut();
        s.cutForced = s.cutCandidate = -1;
    }
    else
    {
        float dForced = DistXz(s.cutEyes[s.cutForced], eye);
        if (want != s.cutCandidate)
        {
            s.cutCandidate = want;
            s.cutCandidateT = s.now;
        }
        if (want != s.cutForced && best <= c.cutMatchRadius && best < dForced * 0.85f && s.now - s.cutCandidateT >= 0.35f)
        {
            g_game->ForceCut(static_cast<uint32_t>(want));
            s.cutForced = want;
        }
    }
}

Input ReadInput()
{
    Input in;
    Game::Pad pad = g_game->ReadPad();
    in.mouse = ps2_prompt_style() == 0;
    in.held = pad.held;
    in.lx = pad.lx;
    in.ly = pad.ly;
    // The right stick looks with a gamepad; with the keyboard it's the appeals' (1-4), and the mouse looks
    if (!in.mouse)
    {
        in.rx = pad.rx;
        in.ry = pad.ry;
    }
    float dx = 0, dy = 0;
    if (ps2_take_mouse_motion(&dx, &dy))
    {
        in.mouseDx = dx;
        in.mouseDy = dy;
    }
    in.menusOpen = ps2_menus_open() != 0;
    return in;
}

void DriverFrame()
{
    uint32_t frames = rd32(g_game->layout().frames);
    if (!s.init)
    {
        s.init = true;
        s.prevFrames = frames;
        return;
    }
    float dt = std::clamp(static_cast<float>(frames - s.prevFrames) / VblankHz, 0.0f, MaxDt);
    s.prevFrames = frames;
    s.now += dt;

    const Config c = ReadConfig();
    if (c.view != View::Modern)
    {
        if (s.levelLive || s.frozen || s.freeAimOn)
        {
            HandBackAll();
        }
        SetStatus("off");
        return;
    }
    // A level live: the game overlay in play with her record filled in
    if (!InPlay() || !g_game->PlayerLive())
    {
        if (s.levelLive)
        {
            if (s.cutForced >= 0)
            {
                g_game->ReleaseCut();
            }
            s.cutForced = s.cutCandidate = -1;
            s.haveCutEyes = false;
            s.haveLastWritten = false;
            s.haveSteerYaw = false;
            g_game->ForgetWalls();
            LockRelease();
            g_game->RestoreArtwork();
            s.levelLive = false;
        }
        // Handed back where the game shows its own camera (a cutscene: the camera it wrote stays)
        Unfreeze(false);
        if (s.freeAimOn)
        {
            g_game->SetGunElevation(0);
            s.freeAimOn = false;
        }
        g_freeAim = false;
        InputNeutral();
        CrosshairOff();
        SetStatus("waiting for play");
        return;
    }
    if (!s.levelLive)
    {
        Freeze();
        ResetRigToPlayer();
        s.levelLive = true;
        SetStatus("driving the camera");
        return; // seeded this frame, driven from the next
    }
    if (dt <= 0)
    {
        return; // the same game frame
    }
    SyncEngineState(c);

    // The shoulder's side (an animated swap)
    if (!s.haveShoulder)
    {
        s.haveShoulder = true;
        s.shoulderTarget = s.shoulderSign = c.hip.shoulder < 0 ? -1.0f : 1.0f;
    }
    if (s.shoulderSign != s.shoulderTarget)
    {
        s.shoulderSign += (s.shoulderTarget - s.shoulderSign) * SmoothAlpha(std::clamp(c.shoulderSwapTime, 0.02f, 1.0f), dt);
        if (std::fabs(s.shoulderTarget - s.shoulderSign) < 0.001f)
        {
            s.shoulderSign = s.shoulderTarget;
        }
    }
    if (!std::isfinite(s.pitch))
    {
        s.pitch = 0;
    }
    if (!std::isfinite(s.camYaw))
    {
        s.camYaw = g_game->PlayerYaw() + 180.0f;
    }

    // The game's aim lock (Lock on enemy): her aim stance picked a target; until it ends the camera stays on it, on the side
    // it had, never pulling back; when it ends the camera turns back behind her
    bool wasLocked = s.locked;
    Vec3 lockPos;
    if (!c.lockOnEnemy)
    {
        LockRelease();
    }
    else if (g_game->AimTarget(&lockPos))
    {
        if (!s.locked)
        {
            s.locked = true;
            s.lockSide = s.shoulderTarget;
            s.haveLockYaw = false;
            s.haveLockDist = false;
            s.lockReturn = false;
        }
        s.lockMiss = 0;
        s.lockPos = lockPos;
    }
    else if (s.locked && ++s.lockMiss <= LockGrace)
    {
        lockPos = s.lockPos; // a short gap: the lock holds where it was
    }
    else
    {
        LockRelease();
    }
    if (wasLocked && !s.locked)
    {
        s.lockReturn = true;
        s.lookOff = 0;
    }
    if (s.locked)
    {
        s.shoulderTarget = s.lockSide;
    }

    // The input
    Input in = ReadInput();
    uint32_t held = in.menusOpen ? 0 : in.held;
    uint32_t pressed = held & ~s.prevHeld;
    s.prevHeld = held;
    float lx = std::clamp(in.lx, -1.0f, 1.0f), ly = std::clamp(in.ly, -1.0f, 1.0f);
    float rx = std::clamp(in.rx, -1.0f, 1.0f), ry = std::clamp(in.ry, -1.0f, 1.0f);
    float mdx = in.mouseDx, mdy = in.mouseDy;
    if (in.menusOpen)
    {
        rx = ry = mdx = mdy = 0;
    }
    bool inputMoving = std::fabs(lx) > 0.1f || std::fabs(ly) > 0.1f;
    if (inputMoving || g_game->PlayerMoveHeld())
    {
        s.moveIdle = 0;
    }
    else
    {
        s.moveIdle += dt;
    }
    // Aim: the game's own (R1, the right mouse button)
    bool aiming = (held & Sce::R1) != 0;
    float blend = aiming ? c.aimBlendIn : c.aimBlendOut;
    if (!(blend > 0))
    {
        blend = c.aimBlend;
    }
    s.aimAmount += ((aiming ? 1.0f : 0.0f) - s.aimAmount) * SmoothAlpha(std::clamp(blend, 0.02f, 1.0f), dt);
    bool aimingNow = s.aimAmount > 0.1f;

    // Swap shoulder: R3 (the middle mouse button)
    if ((pressed & Sce::R3) != 0 && !s.locked)
    {
        s.shoulderTarget = -s.shoulderTarget;
    }

    // Her position
    Vec3 p = g_game->PlayerPosition();
    float playerYaw = g_game->PlayerYaw();
    if (!s.locked && s.havePrevPos && Hypot(p.x - s.prevX, p.z - s.prevZ) > RelocateDistance)
    {
        // The game put her somewhere else (a scripted scene): the boom starts at its length again
        s.camT = 1.0f;
        s.boomReturn = false;
    }
    s.havePrevPos = true;
    s.prevX = p.x;
    s.prevZ = p.z;

    CheckCameraTaken(c);

    // Look: the right stick and the mouse
    float pdir = c.invertPitch ? -1.0f : 1.0f, ydir = c.invertStick ? -1.0f : 1.0f;
    float msens = c.mouseSensitivity * 0.4f;
    float lookScale = s.aimAmount > 0.001f ? Lerp(1.0f, std::clamp(c.aimSensitivity, 0.05f, 3.0f), s.aimAmount) : 1.0f;
    s.pitch -= ry * pdir * c.pitchSpeed * dt * lookScale;
    s.pitch += mdy * msens * pdir * lookScale;

    Rig hipRig, aimRig;
    Presets(c, &hipRig, &aimRig);
    float aimTrim = s.aimAmount > 0.001f ? aimRig.pitchTrim : 0.0f;
    float gunLimit = std::clamp(c.gunPitchMax, 1.0f, 45.0f);
    float aimZero = std::clamp(c.aimZeroDeg, -gunLimit, gunLimit);
    float lo = c.pitchMin, hi = c.pitchMax;
    if (s.aimAmount > 0.001f && c.modernAim && c.aimPitchLock)
    {
        // While she aims the camera can't tilt further than the gun can follow
        float base = aimZero - aimTrim;
        lo = Lerp(lo, base - gunLimit, s.aimAmount);
        hi = Lerp(hi, base + gunLimit, s.aimAmount);
    }
    s.pitch = std::max(lo, std::min(hi, s.pitch));

    bool looking = std::fabs(rx) > LookThreshold || std::fabs(mdx) > 0.001f;
    float dyaw = (-(rx * ydir) * c.stickSpeed * dt - mdx * ydir * msens) * lookScale;
    if (s.locked)
    {
        dyaw = 0; // the lock holds the yaw on its target
    }
    s.camYaw += dyaw;
    s.lookOff = AngleWrap(s.lookOff + dyaw);

    // Modern aim: the gun where the camera looks; with free aim the left stick walks her instead of tilting the gun
    bool centreLeft = false;
    if (c.modernAim && aiming && s.freeAimOn)
    {
        g_game->SetGunElevation(std::clamp(-(s.pitch + aimTrim) + aimZero, -gunLimit, gunLimit));
    }
    if (s.aimWas && !aiming && s.freeAimOn)
    {
        g_game->SetGunElevation(0);
    }
    s.aimWas = aiming;
    if (aiming && s.freeAimOn)
    {
        centreLeft = true;
        if (c.aimStrafeSpeed > 0)
        {
            g_game->AimMoveStep(lx, ly, dt, c.aimStrafeSpeed);
        }
    }
    // The right stick is the game's ad-lib control: a gamepad's turns of the camera say nothing
    bool centreRight = c.ownLookStick && !in.mouse;
    // With Turn to camera off and Keep behind off the game's own stick walking moves her relative to this camera; its hold of
    // her walking direction at a fixed camera's cut is let go, and she turns round toward the stick rather than step back
    s.map.walkFree = c.turnToCamera == TurnMode::Off && !c.keepBehind;
    if (!s.map.walkFree)
    {
        s.map.walkSide = 0;
    }
    s.map.centreLeft = centreLeft;
    s.map.centreRight = centreRight;
    s.map.active = true;
    g_mouseLook = true;

    // Yaw: follow, the free look's return, the aim's snap, recenter
    float behind = c.followBehind ? 180.0f : 0.0f;
    float baseYaw = playerYaw;
    s.follow.moveHold = std::max(0.0f, c.followHold);
    s.follow.Update(p.x, p.z, playerYaw, dt, inputMoving, aimingNow, &baseYaw);
    // Outside the aim lock her camera stays on her: it turns only with the player (the look, and with Keep behind her facing
    // while the player moves her); a knockback, a hit, the recoil or the auto-aim never swings it
    bool playerActs = s.moveIdle <= std::max(0.0f, c.followHold);
    bool active = playerActs;
    baseYaw = playerYaw;
    bool aimSteer = (aiming || aimingNow) && c.modernAim;
    bool steering = c.turnToCamera != TurnMode::Off || aimSteer;
    float viewYaw;
    if (s.locked)
    {
        // The camera turns onto the line from the target through her; Modern aim turns her to the target
        float tx = lockPos.x - p.x, tz = lockPos.z - p.z;
        if (Hypot(tx, tz) >= LockMinDistance)
        {
            s.lockYaw = AngleWrap(std::atan2(tx, tz) * RadToDeg + behind);
            s.haveLockYaw = true;
        }
        else if (!s.haveLockYaw)
        {
            s.lockYaw = s.camYaw;
            s.haveLockYaw = true;
        }
        s.camYaw += AngleWrap(s.lockYaw - s.camYaw) * SmoothAlpha(LockTurn, dt);
        s.lookOff = AngleWrap(s.camYaw - (playerYaw + behind + c.yawOffset));
        viewYaw = AngleWrap(s.lockYaw - behind);
        s.aimPrev = aiming;
    }
    else
    {
        if (s.lockReturn)
        {
            // The lock has ended: back behind her as fast as the lock turned; the player's own look takes over at once
            float back = AngleWrap(playerYaw + behind + c.yawOffset - s.camYaw);
            if (looking || std::fabs(back) < 0.5f)
            {
                s.lockReturn = false;
            }
            else
            {
                s.camYaw += back * SmoothAlpha(LockTurn, dt);
            }
        }
        if (!looking && !(aimingNow && c.aimFreeLook) && c.keepBehind)
        {
            ReleaseFreeLook(&s.camYaw, &s.lookOff, dt, c.freeLookReturn);
        }
        viewYaw = AngleWrap(s.camYaw - behind);
        if (active && (c.keepBehind || (aimingNow && c.aimFollow && !aimSteer)) && !steering)
        {
            float k = aimingNow ? c.aimFollowStrength : c.followStrength;
            float target = baseYaw + behind + c.yawOffset + s.lookOff;
            s.camYaw += AngleWrap(target - s.camYaw) * SmoothAlpha(std::max(0.02f, k), dt);
        }
        if (aiming && !s.aimPrev && c.aimSnapBehind)
        {
            s.lookOff = 0;
        }
        if (c.aimSnapBehind && aimingNow && !aimSteer)
        {
            float snap = playerYaw + behind + c.yawOffset + s.lookOff;
            if (std::fabs(AngleWrap(snap - s.camYaw)) > 0.5f)
            {
                s.camYaw += AngleWrap(snap - s.camYaw) * SmoothAlpha(std::clamp(c.aimSnapSpeed, 0.1f, 1.0f), dt);
            }
        }
        s.aimPrev = aiming;
        if (s.recenterTimer > 0)
        {
            float a = std::min(1.0f, dt * 15.0f);
            s.recenterTimer -= dt;
            s.camYaw += AngleWrap(playerYaw + behind - s.camYaw) * a;
            s.lookOff = Lerp(s.lookOff, 0, a);
            s.pitch = Lerp(s.pitch, 0, a);
        }
    }

    // The rig: from the main framing to the aim one
    bool writeFov = true;
    if (hipRig.fovDeg <= 0 && aimRig.fovDeg <= 0)
    {
        writeFov = false; // the room's own field of view
    }
    else
    {
        float engineFov = g_game->Fov();
        if (hipRig.fovDeg <= 0)
        {
            hipRig.fovDeg = engineFov;
        }
        if (aimRig.fovDeg <= 0)
        {
            aimRig.fovDeg = engineFov;
        }
    }
    Rig eff = BlendRig(hipRig, aimRig, s.aimAmount);
    if (s.locked)
    {
        // The aim lock never pulls the camera back: its distance may shorten, not grow, until the lock ends
        if (s.haveLockDist && eff.distance > s.lockDist)
        {
            eff.distance = s.lockDist;
        }
        s.lockDist = eff.distance;
        s.haveLockDist = true;
    }
    else if (s.haveLockDist)
    {
        // Just ended: the boom keeps the lock's length and grows back as after a wall
        if (eff.distance > s.lockDist && eff.distance > 0)
        {
            s.camT = std::min(s.camT, s.camT * s.lockDist / eff.distance);
        }
        s.haveLockDist = false;
    }
    eff.lookDrop *= 1.0f - s.aimAmount;
    float mag = std::fabs(eff.shoulder);
    if (s.aimAmount > 0.001f && c.aimShoulderMin > 0)
    {
        mag = Lerp(mag, std::max(mag, c.aimShoulderMin), s.aimAmount);
    }
    eff.shoulder = mag * s.shoulderSign;
    if (writeFov && eff.fovDeg > 0)
    {
        g_game->SetFov(eff.fovDeg);
    }

    // The eye and what it looks at
    Vec3 eye, look;
    EyeAndLook(p.x, p.y, p.z, s.camYaw, s.pitch + eff.pitchTrim, eff, &eye, &look);

    // Collision: the eye pulled in front of walls
    if (c.camCollision)
    {
        float ex = eye.x, ez = eye.z, ey = eye.y;
        // Only the boom's fraction is used: smoothing the point would drag the eye through walls
        float tHit = ClampEyeToWalls(g_game->Walls(dt), &ex, &ez, &ey, look.x, look.z, c.camMargin, 0, true, look.y, true,
                                     look.y + c.camBlockTop);
        s.camHoldTimer = tHit < 0.999f ? c.camHold : std::max(0.0f, s.camHoldTimer - dt);
        if (s.locked)
        {
            s.boomReturn = false; // the aim lock never lets it grow back, nor does its end
        }
        else if (tHit > s.camT && (playerActs || looking))
        {
            s.boomReturn = true; // the player moved her or the camera and the wall cleared
        }
        if (tHit < s.camT)
        {
            s.camT = c.camSnap > 0 ? s.camT + (tHit - s.camT) * SmoothAlpha(std::clamp(c.camSnap, 0.5f, 1.0f), dt) : tHit;
            s.boomReturn = false;
        }
        else if (s.camHoldTimer > 0 || s.locked || !s.boomReturn)
        {
            // held in (the lock: until it ends; otherwise until the player opens the space: never by itself)
            if (!playerActs && !looking)
            {
                s.boomReturn = false;
            }
        }
        else
        {
            s.camT += (tHit - s.camT) * SmoothAlpha(std::max(0.02f, c.camRecover), dt);
            if (tHit - s.camT < 0.001f)
            {
                s.boomReturn = false;
            }
        }
        if (s.camT < 0.999f)
        {
            float ey2 = look.y + (eye.y - look.y) * s.camT;
            eye.x = look.x + (eye.x - look.x) * s.camT;
            eye.y = std::max(ey2, p.y + 25.0f);
            eye.z = look.z + (eye.z - look.z) * s.camT;
        }
    }

    g_game->WriteCamera(eye, look);
    // The walls and pictures a fixed camera leaves out stay drawn in this view
    g_game->KeepArtwork();
    // The room's cut nearest this eye borrowed, so what the game draws suits the view
    CutMatch(c, eye);
    s.haveLastWritten = true;
    s.lastWritten = eye;

    // The crosshair while she aims
    Ps2Crosshair x{};
    x.visible = c.crosshair && aimingNow;
    x.style = c.crosshairStyle;
    x.size = c.crosshairSize;
    x.gap = c.crosshairGap;
    x.thickness = c.crosshairThickness;
    x.rgb = c.crosshairRgb;
    x.opacity = std::clamp(s.aimAmount, 0.0f, 1.0f);
    ps2_set_crosshair(&x);

    g_game->SetNearFar(c.nearValue, c.farPlane);

    // Turn her toward the camera
    if (steering && (aiming || aimingNow) && !aimSteer)
    {
        steering = false;
    }
    if (steering && in.menusOpen)
    {
        steering = false;
    }
    if (steering)
    {
        bool pushing;
        float targetYaw, gap;
        if (aimSteer)
        {
            pushing = std::fabs(rx) > LookThreshold || std::fabs(mdx) > 0.001f ||
                      std::fabs(AngleWrap(viewYaw - playerYaw)) > c.modernAimDeadzone;
            targetYaw = viewYaw;
        }
        else if (c.turnToCamera == TurnMode::Always)
        {
            pushing = true;
            targetYaw = viewYaw;
        }
        else
        {
            pushing = ModernMovement(lx, ly, viewYaw, playerYaw, MoveDeadzone, &targetYaw, &gap);
        }
        if (pushing)
        {
            float step = std::max(1.0f, c.turnSpeed) * dt;
            if (!s.haveSteerYaw)
            {
                s.haveSteerYaw = true;
                s.steerYaw = g_game->PlayerHeading();
            }
            gap = AngleWrap(targetYaw - s.steerYaw);
            s.steerYaw = AngleWrap(s.steerYaw + std::clamp(gap, -step, step));
            if (std::fabs(AngleWrap(s.steerYaw - g_game->PlayerHeading())) >= HeadingEpsilon)
            {
                g_game->SetPlayerHeading(s.steerYaw);
            }
        }
        else
        {
            s.haveSteerYaw = false;
        }
    }
    else
    {
        s.haveSteerYaw = false;
    }
}
}

void Use(const Layout& layout, const BiohazardControls::Layout& play)
{
    static Game game(layout);
    g_game = &game;
    g_play = &play;
}

void FrameMark(Ps2Cpu* c, Ps2Function original)
{
    original(c);
    DriverFrame();
}

void PadMap(Ps2Cpu* c, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    // ModernCam's mark at the mapper's load of her held word: nothing between the mapper's start and that load changes the
    // mapped record, so the change is made before it (only her record, offline the mapped record 0)
    if (s.map.active && s.levelLive && record == g_game->PlayerRecord())
    {
        g_game->PadMapApply(s.map.centreLeft, s.map.centreRight, true);
        if (s.map.walkFree)
        {
            g_game->ReleaseWalkHold();
            s.map.walkSide = g_game->WalkTurn(s.map.walkSide);
        }
    }
    original(c);
}

void FixedCut(Ps2Cpu* c, Ps2Function original)
{
    if (!g_freeze)
    {
        original(c);
    }
}

void RailCut(Ps2Cpu* c, Ps2Function original)
{
    if (!g_freeze)
    {
        original(c);
        return;
    }
    // Its rail state goes on (so a hand-back continues from where the game is); its writes to the camera block are undone
    const Layout& l = g_game->layout();
    uint8_t block[0x100];
    for (uint32_t i = 0; i < l.blockSize; i += 4)
    {
        uint32_t w = rd32(l.block + i);
        std::memcpy(block + i, &w, 4);
    }
    original(c);
    for (uint32_t i = 0; i < l.blockSize; i += 4)
    {
        uint32_t w;
        std::memcpy(&w, block + i, 4);
        wr32(l.block + i, w);
    }
}

void Follow(Ps2Cpu* c, Ps2Function original)
{
    if (!g_freeze)
    {
        original(c);
    }
}

void GunElevation(Ps2Cpu* c, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    if (!g_freeAim || record != g_game->PlayerRecord())
    {
        original(c);
        return;
    }
    // The player's branch of it sets her elevation from the aim's stick (the servo and its stores): kept as the camera set it
    const uint32_t at = record + Field::GunElevation;
    const uint16_t kept = rd16(at);
    original(c);
    wr16(at, kept);
}

int MouseStick(uint32_t pressed, int (*game)(uint32_t))
{
    if (g_mouseLook)
    {
        return PS2_MOUSE_LOOK;
    }
    return game != nullptr ? game(pressed) : 0;
}
}
