// The Modern camera's rig (modern_camera_rig.h).
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): rig.c and collision.c,
// constants and formulas unchanged.
#include "modern_camera_rig.h"

#include <algorithm>
#include <cmath>

namespace ModernCamera
{
namespace
{
constexpr float ReferenceDt = 1.0f / 60.0f;
// The follow heading's tuning (ModernCam's FollowHeading)
constexpr float FollowTeleport = 800.0f;    // a step longer than this is a warp or a room change
constexpr float FollowMinStep = 0.3f;       // shorter steps are the same position read back
constexpr float FollowBackpedalOn = 135.0f;
constexpr float FollowBackpedalOff = 115.0f;
// The walls (ModernCam's ro_collision)
constexpr float WallMinHeight = 20.0f;
constexpr float WallMaxNormalY = 0.707f;    // mostly horizontal: a floor or a ceiling, not a wall
constexpr float BoomMinKeep = 0.15f;
}

float AngleWrap(float degrees)
{
    float a = std::fmod(degrees + 180.0f, 360.0f);
    if (a < 0)
    {
        a += 360.0f;
    }
    return a - 180.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float SmoothAlpha(float k, float dt)
{
    if (k <= 0.0f || dt <= 0.0f)
    {
        return 0.0f;
    }
    if (k >= 1.0f)
    {
        return 1.0f;
    }
    return 1.0f - std::pow(1.0f - k, dt / ReferenceDt);
}

void ReleaseFreeLook(float* camYaw, float* lookOff, float dt, float tau)
{
    // The camera and the offset move together: the offset is the camera's angle from straight behind her, so winding it down
    // without turning the camera would make it a lie
    if (tau <= 0.0f || dt <= 0.0f)
    {
        return;
    }
    float eased = *lookOff * std::exp(-dt / tau);
    *camYaw += eased - *lookOff;
    *lookOff = eased;
}

void EyeAndLook(float px, float py, float pz, float yawDeg, float pitchDeg, const Rig& rig, Vec3* eye, Vec3* look)
{
    float yaw = yawDeg * DegToRad, pitch = pitchDeg * DegToRad;
    float fx = std::sin(yaw), fz = std::cos(yaw); // from her toward the eye (behind her)
    float rx = fz, rz = -fx;                      // the screen's right
    float across = rig.distance * std::cos(pitch);
    eye->x = px + fx * across + rx * rig.shoulder;
    eye->y = py + rig.height + rig.distance * std::sin(pitch);
    eye->z = pz + fz * across + rz * rig.shoulder;
    look->x = px + rx * rig.shoulder;
    look->y = py + rig.height + rig.lookDrop;
    look->z = pz + rz * rig.shoulder;
}

bool ModernMovement(float lx, float ly, float viewYaw, float playerYaw, float deadzone, float* heading, float* gap)
{
    if (std::hypot(lx, ly) < deadzone)
    {
        *heading = 0;
        *gap = 0;
        return false;
    }
    // The minus on lx: the screen's right is viewYaw - 90 in the game's headings
    float stick = std::atan2(-lx, ly) * RadToDeg;
    *heading = AngleWrap(viewYaw + stick);
    *gap = AngleWrap(*heading - playerYaw);
    return true;
}

Rig BlendRig(const Rig& hip, const Rig& aim, float t)
{
    Rig r;
    r.distance = Lerp(hip.distance, aim.distance, t);
    r.shoulder = Lerp(hip.shoulder, aim.shoulder, t);
    r.height = Lerp(hip.height, aim.height, t);
    r.fovDeg = Lerp(hip.fovDeg, aim.fovDeg, t);
    r.lookDrop = Lerp(hip.lookDrop, aim.lookDrop, t);
    r.pitchTrim = Lerp(hip.pitchTrim, aim.pitchTrim, t);
    return r;
}

void FollowHeading::Reset()
{
    havePos = false;
    since = 1e9f; // no step for ages: not moving
    haveStepYaw = false;
    haveYaw = false;
    backpedal = false;
}

bool FollowHeading::Update(float px, float pz, float playerYaw, float dt, bool inputMoving, bool aiming, float* baseYaw)
{
    if (!havePos)
    {
        havePos = true;
        posX = px;
        posZ = pz;
    }
    else
    {
        float dx = px - posX, dz = pz - posZ;
        float step = std::hypot(dx, dz);
        if (step > FollowTeleport)
        {
            Reset();
            havePos = true;
            posX = px;
            posZ = pz;
        }
        else if (step >= FollowMinStep)
        {
            // Against the last position that changed, never per poll
            float span = std::min(1.0f, since + dt);
            float raw = std::atan2(dx, dz) * RadToDeg;
            if (!haveYaw)
            {
                haveYaw = true;
                fastYaw = moveYaw = raw;
            }
            else
            {
                fastYaw += AngleWrap(raw - fastYaw) * SmoothAlpha(fast, span);
                moveYaw += AngleWrap(raw - moveYaw) * SmoothAlpha(slow, span);
            }
            posX = px;
            posZ = pz;
            since = 0;
            haveStepYaw = true;
            stepYaw = playerYaw; // her facing at this step
        }
        else
        {
            since += dt;
        }
    }
    bool moving = since <= moveHold;
    if (aiming || !haveYaw || (!moving && inputMoving))
    {
        haveYaw = true;
        fastYaw = moveYaw = playerYaw;
        haveStepYaw = true;
        stepYaw = playerYaw;
        *baseYaw = playerYaw;
        return moving || inputMoving || aiming;
    }
    if (!moving)
    {
        *baseYaw = moveYaw;
        return false;
    }
    float gap = std::fabs(AngleWrap(fastYaw - (haveStepYaw ? stepYaw : playerYaw)));
    backpedal = backpedal ? gap > FollowBackpedalOff : gap > FollowBackpedalOn;
    *baseYaw = backpedal ? AngleWrap(moveYaw + 180.0f) : moveYaw;
    return true;
}

bool SlabFromRecord(const float c[9], float normalY, Slab* slab)
{
    if (std::fabs(normalY) <= WallMaxNormalY)
    {
        return false;
    }
    *slab = {{c[0], c[1], c[2]}, {c[3], c[4], c[5]}, {c[6], c[7], c[8]}};
    return true;
}

namespace
{
// Where along from + t * dir the segment meets the triangle (either side), t in [0, 1]; false if it doesn't (Moller-Trumbore)
bool SegmentHitsTriangle(Vec3 from, Vec3 dir, const Slab& s, float* t)
{
    Vec3 e1{s.b.x - s.a.x, s.b.y - s.a.y, s.b.z - s.a.z}, e2{s.c.x - s.a.x, s.c.y - s.a.y, s.c.z - s.a.z};
    Vec3 p{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z, dir.x * e2.y - dir.y * e2.x};
    float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
    if (std::fabs(det) < 1e-6f)
    {
        return false; // the boom runs along the slab's plane
    }
    float inv = 1.0f / det;
    Vec3 o{from.x - s.a.x, from.y - s.a.y, from.z - s.a.z};
    float u = (o.x * p.x + o.y * p.y + o.z * p.z) * inv;
    if (u < 0 || u > 1)
    {
        return false;
    }
    Vec3 q{o.y * e1.z - o.z * e1.y, o.z * e1.x - o.x * e1.z, o.x * e1.y - o.y * e1.x};
    float v = (dir.x * q.x + dir.y * q.y + dir.z * q.z) * inv;
    if (v < 0 || u + v > 1)
    {
        return false;
    }
    *t = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
    return *t >= 0 && *t <= 1;
}
}

float BoomClearOfSlabs(const std::vector<Slab>& slabs, Vec3 head, Vec3 eye, float margin)
{
    Vec3 dir{eye.x - head.x, eye.y - head.y, eye.z - head.z};
    float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (length < 1e-3f)
    {
        return 1.0f;
    }
    float first = 1.0f;
    bool hit = false;
    for (const Slab& s : slabs)
    {
        float t;
        if (SegmentHitsTriangle(head, dir, s, &t) && t < first)
        {
            first = t;
            hit = true;
        }
    }
    return hit ? std::max(0.0f, first - margin / length) : 1.0f;
}

bool WallFromRecord(uint32_t flags, const float c[9], float normalY, Wall* wall)
{
    if (std::fabs(normalY) > WallMaxNormalY)
    {
        return false;
    }
    float x0 = c[0], y0 = c[1], z0 = c[2], x1 = c[3], y1 = c[4], z1 = c[5], x2 = c[6], y2 = c[7], z2 = c[8];
    Wall w{};
    w.minX = std::min({x0, x1, x2});
    w.minZ = std::min({z0, z1, z2});
    w.minY = std::min({y0, y1, y2});
    w.maxY = std::max({y0, y1, y2});
    if (w.maxY - w.minY < WallMinHeight)
    {
        return false;
    }
    w.w = std::max({x0, x1, x2}) - w.minX;
    w.d = std::max({z0, z1, z2}) - w.minZ;
    if (w.w < 1.0f) // an axis-aligned sliver: 4 units thick
    {
        w.minX -= 2.0f;
        w.w = 4.0f;
    }
    if (w.d < 1.0f)
    {
        w.minZ -= 2.0f;
        w.d = 4.0f;
    }
    // The wall's segment is the triangle's longest edge across the floor
    float d01 = (x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0);
    float d12 = (x2 - x1) * (x2 - x1) + (z2 - z1) * (z2 - z1);
    float d20 = (x0 - x2) * (x0 - x2) + (z0 - z2) * (z0 - z2);
    if (d01 >= d12 && d01 >= d20)
    {
        w.segX0 = x0, w.segZ0 = z0, w.segX1 = x1, w.segZ1 = z1;
    }
    else if (d12 >= d01 && d12 >= d20)
    {
        w.segX0 = x1, w.segZ0 = z1, w.segX1 = x2, w.segZ1 = z2;
    }
    else
    {
        w.segX0 = x2, w.segZ0 = z2, w.segX1 = x0, w.segZ1 = z0;
    }
    w.flags = flags;
    *wall = w;
    return true;
}

float ClampEyeToWalls(const std::vector<Wall>& walls, float* ex, float* ez, float* ey, float hx, float hz, float margin,
                      uint32_t ignoreFlags, bool useHy, float hy, bool useMinTop, float minTop)
{
    float minT = 1.0f;
    for (const Wall& w : walls)
    {
        if ((ignoreFlags != 0 && (w.flags & ignoreFlags) != 0) || (useMinTop && w.maxY < minTop))
        {
            continue;
        }
        float den = (hx - *ex) * (w.segZ0 - w.segZ1) - (hz - *ez) * (w.segX0 - w.segX1);
        if (den == 0.0f)
        {
            continue;
        }
        float t = ((hx - w.segX0) * (w.segZ0 - w.segZ1) - (hz - w.segZ0) * (w.segX0 - w.segX1)) / den;
        float u = -((hx - *ex) * (hz - w.segZ0) - (hz - *ez) * (hx - w.segX0)) / den;
        // t runs from the head (0) to the eye (1)
        if (t >= 0.0f && t < minT && u >= -0.01f && u <= 1.01f)
        {
            float yAt = useHy ? hy + (*ey - hy) * t : *ey;
            if (yAt >= w.minY && yAt <= w.maxY)
            {
                minT = t;
            }
        }
    }
    if (minT >= 1.0f)
    {
        return 1.0f;
    }
    float length = std::hypot(*ex - hx, *ez - hz);
    if (length > 0.0f)
    {
        minT = std::max(BoomMinKeep, minT - margin / length);
    }
    if (useHy)
    {
        *ey = hy + (*ey - hy) * minT;
    }
    *ex = hx + (*ex - hx) * minT;
    *ez = hz + (*ez - hz) * minT;
    return minT;
}
}
