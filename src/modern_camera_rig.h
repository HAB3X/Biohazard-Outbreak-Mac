// The Modern camera's rig: the over-the-shoulder shot's geometry, the heading it follows and its collision with the room's
// walls. Pure: no game memory, so it's tested on its own (tests/modern_camera_rig_test.cpp). Angles are degrees; headings are
// the game's atan2(x, z), so a turn clockwise seen from above is negative.
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), itself a recreation of Outbreak ModernCam by Snippy (MIT): its rig.c,
// mcam_rig.h, collision.c and mcam_collision.h, whose constants and formulas are kept.
#pragma once

#include <cstdint>
#include <vector>

namespace ModernCamera
{
constexpr float Pi = 3.14159265358979f;
constexpr float DegToRad = Pi / 180.0f;
constexpr float RadToDeg = 180.0f / Pi;

struct Vec3
{
    float x = 0, y = 0, z = 0;
};

// One framing (the main rig, the aim rig, the alternate view)
struct Rig
{
    float distance = 300;  // how far behind her the eye sits
    float shoulder = 50;   // sideways, + over her right shoulder
    float height = 130;    // lifts the eye and the point it looks at together (never tilts)
    float fovDeg = 60;     // vertical field of view (0: the room's own)
    float lookDrop = 0;    // how far below its own height the camera looks
    float pitchTrim = 0;   // a constant tilt added to the orbit's pitch
};

// An angle folded into [-180, 180)
float AngleWrap(float degrees);
float Lerp(float a, float b, float t);
// The blend factor for x += (target - x) * alpha that gives k a frame at 60 frames a second, at any frame time
float SmoothAlpha(float k, float dt);
// A free look's offset eased back toward 0 with the time constant tau (tau <= 0 holds it), the camera turned with it
void ReleaseFreeLook(float* camYaw, float* lookOff, float dt, float tau);
// The eye and the point it looks at for a shot of a rig around (px, py, pz)
void EyeAndLook(float px, float py, float pz, float yawDeg, float pitchDeg, const Rig& rig, Vec3* eye, Vec3* look);
// The heading the left stick asks for, from the view: 1 when the stick is out of the dead zone
bool ModernMovement(float lx, float ly, float viewYaw, float playerYaw, float deadzone, float* heading, float* gap);
Rig BlendRig(const Rig& hip, const Rig& aim, float t);

// The heading the follow camera sits behind: the way she travels, or her facing when her motion can't be trusted. A fast copy
// tells a backpedal, a slow one is followed
struct FollowHeading
{
    float moveHold = 0.18f;
    float fast = 0.6f;
    float slow = 0.15f;
    bool havePos = false;
    float posX = 0, posZ = 0;
    float since = 1e9f;
    bool haveStepYaw = false;
    float stepYaw = 0;
    bool haveYaw = false;
    float fastYaw = 0, moveYaw = 0;
    bool backpedal = false;

    void Reset();
    // Whether she's moving; *baseYaw the heading to sit behind
    bool Update(float px, float pz, float playerYaw, float dt, bool inputMoving, bool aiming, float* baseYaw);
};

// A wall of the room as the camera's collision uses it: its box and the triangle's longest edge across the floor
struct Wall
{
    float minX, minZ, w, d;
    float minY, maxY;
    float segX0, segZ0, segX1, segZ1;
    uint32_t flags;
};

// A wall record of the room's collision (0x38 bytes: flags, three corners, then the normal's y at +0x2C) made a camera wall;
// false for floors, ceilings and low steps
bool WallFromRecord(uint32_t flags, const float corners[9], float normalY, Wall* wall);

// A floor or ceiling triangle of the room's collision: what the boom may not pass through going up or down stairs (the walls
// above leave them out)
struct Slab
{
    Vec3 a, b, c;
};

// A collision record (as WallFromRecord's) made a slab: true for floors and ceilings, false for walls
bool SlabFromRecord(const float corners[9], float normalY, Slab* slab);

// The fraction of the boom from `head` to `eye` kept clear of every slab, `margin` short of the first one it crosses, in
// [0, 1] (1: it crosses none)
float BoomClearOfSlabs(const std::vector<Slab>& slabs, Vec3 head, Vec3 eye, float margin);

// The boom from the head (hx, hz) to the eye pulled in front of the first wall it crosses, by `margin` along the boom (walls
// with any of ignoreFlags, and with useMinTop those whose top is below minTop, don't count; with useHy the eye's height
// follows the boom from hy). The fraction of the boom kept, in (0, 1]
float ClampEyeToWalls(const std::vector<Wall>& walls, float* ex, float* ez, float* ey, float hx, float hz, float margin,
                      uint32_t ignoreFlags, bool useHy, float hy, bool useMinTop, float minTop);
}
