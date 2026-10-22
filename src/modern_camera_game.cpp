// The Modern camera's view of the game's memory (modern_camera_game.h).
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): game.c and
// collision.c's wall reader. Widths, offsets and write order are the originals'.
#include "modern_camera_game.h"

#include "ps2/recomp.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ModernCamera
{
// File 1 (SLPM-65428). Snippy's ro_addresses.py (his ModernCam's File 1 map) for the camera block, the pad records and the
// button map, the characters, the cuts, the objects, the walls, the hide radii and the light; each checked in the recompiled
// code (docs/mods.md). The rest moved from the US port's (reo_camera_us.h) by the shifts its own notes give for each block
// (camera block +0x1DE00, submain's bss +0x2EE60) and checked the same way
const Layout File1 = {.name = "file1",
                      .eye = 0x306338,
                      .target = 0x306344,
                      .nearValue = 0x306374,
                      .farPlane = 0x306378,
                      .fovDeg = 0x306380,
                      .block = 0x306320,
                      .blockSize = 0x70,
                      .aspect = 0x239D48,
                      .entityLight = 0x306298,
                      .entityLightMode = 0x3062BC,
                      .frames = 0x3065C4,
                      .controlType = 0x3065BC,
                      .padRecords = 0x306410,
                      .buttonMap = 0x2168C0,
                      .padFrames = 0x258698,
                      .characters = 0x476DD0,
                      .characterSize = 0x10E0,
                      .localPlayerId = 0x48BF7D,
                      .enemies = 0x3FFCA0,
                      .enemySize = 0xD90,
                      .enemyFlags = 0xC94,
                      .cuts = 0x3AEF50,
                      .objectCount = 0x470902,
                      .objectFlags = 0x470930,
                      .objectMatrices = 0x470BB0,
                      .wallPointer = 0x3938C4,
                      .nearHide = 0x6D6CF0,
                      .systemPadPressed = 0x3063E0,
                      .roomNow = 0x3065AC,
                      .field = File1Fields};

// File 2 (SLPM-65692). Snippy's ro_addresses_f2.py for the camera block, the cuts, the objects, the walls, the hide radii, the
// light, the button map and the raw pad; the rest found by matching File 1's code in File 2's (the camera block and the pad
// block are File 1's + 0xD210 there: the fixed-cut handler's stores, the button mapper's loads) and checked in play
const Layout File2 = {.name = "file2",
                      .eye = 0x313548,
                      .target = 0x313554,
                      .nearValue = 0x313584,
                      .farPlane = 0x313588,
                      .fovDeg = 0x313590,
                      .block = 0x313530,
                      .blockSize = 0x70,
                      .aspect = 0x2491FC,
                      .entityLight = 0x3134A8,
                      .entityLightMode = 0x3134CC,
                      .frames = 0x3137D4,
                      .controlType = 0x3137CC,
                      .padRecords = 0x313620,
                      .buttonMap = 0x21C160,
                      .padFrames = 0x267918,
                      .characters = 0x47BD30,
                      .characterSize = 0x1100,
                      .localPlayerId = 0x4912BD,
                      .enemies = 0x403DB0,
                      .enemySize = 0xDB0,
                      .enemyFlags = 0xCB0,
                      .cuts = 0x3B31D0,
                      .objectCount = 0x475052,
                      .objectFlags = 0x475080,
                      .objectMatrices = 0x475300,
                      .wallPointer = 0x394FB4,
                      .nearHide = 0x718DB0,
                      .systemPadPressed = 0x3135F0,
                      .roomNow = 0x3137BC,
                      .field = File2Fields};

namespace
{
// A cut's header and records (Layout::cuts)
constexpr uint32_t CutCountAt = 1, CutCurrentAt = 3, CutForceIndexAt = 5, CutForceOnAt = 6;
constexpr uint32_t CutRecords = 0x34, CutSize = 0x198, CutFovAt = 0x14, CutEyeAt = 0xA0, CutTargetAt = 0xB8;
constexpr uint32_t CutMask = 0x154; // 4 words, bit i: object i drawn by this cut
// An enemy record's fields the target finder checks: live (u8 +0), its model (+0x34), untargetable (bit 0 of +0xC94)
constexpr uint32_t EnemyLive = 0x000, EnemyModel = 0x034, EnemyPosition = 0x038;
// A pad record (Layout::padRecords)
constexpr uint32_t PadHeld = 0x08, PadLeftStick = 0x20, PadRightStick = 0x28;
// The mapper's source word (the pad records' +8): the three buttons the control type maps
constexpr uint32_t SrcCross = 0x0010, SrcCircle = 0x0020, SrcSquare = 0x0100;
// R3's bit there (measured: set while R3 is held, the mapper's action 0x2000)
constexpr uint32_t SrcR3 = 0x2000;
// The walls
constexpr uint32_t WallSize = 0x38, MaxWalls = 256;
constexpr float WallRefreshSeconds = 1.0f;
// The artwork: a cut "looks at" an object whose origin is within its field of view and 15 degrees more
constexpr float ArtMarginTan = 0.267949f;

float F32(uint32_t a) { return std::bit_cast<float>(rd32(a)); }
void SetF32(uint32_t a, float v) { wr32(a, std::bit_cast<uint32_t>(v)); }
bool Sane(float v) { return std::isfinite(v) && v > -1e6f && v < 1e6f; }
bool InRam(uint32_t a, uint32_t n) { return a >= 0x100000 && a < 0x2000000 && n <= 0x2000000 - a; }

Vec3 ReadVec(uint32_t a) { return {F32(a), F32(a + 4), F32(a + 8)}; }
// A cut's authored eye and target: three s32, not floats
Vec3 ReadIntVec(uint32_t a)
{
    return {static_cast<float>(static_cast<int32_t>(rd32(a))), static_cast<float>(static_cast<int32_t>(rd32(a + 4))),
            static_cast<float>(static_cast<int32_t>(rd32(a + 8)))};
}

// A centred stick as the mapper reads one: magnitude 0, angle 0xC000, x 0, y 0
void CentreStick(uint32_t a)
{
    wr32(a, 0xC0000000u);
    wr32(a + 4, 0);
}

float TanAdd(float a, float b)
{
    float d = 1.0f - a * b;
    return d > 0.01f ? (a + b) / d : 0.0f;
}
}

uint32_t Game::PlayerRecord() const
{
    // The record whose id is the local player's (offline the player's record, the first)
    const uint8_t id = rd8(m_layout.localPlayerId);
    for (uint32_t i = 0; i < 4; i++)
    {
        uint32_t record = m_layout.characters + i * m_layout.characterSize;
        if (rd8(record + m_layout.field.Id) == id)
        {
            return record;
        }
    }
    return m_layout.characters;
}

bool Game::PlayerLive() const
{
    Vec3 p = PlayerPosition();
    return Sane(p.x) && Sane(p.y) && Sane(p.z) && (p.x != 0 || p.y != 0 || p.z != 0);
}

Vec3 Game::PlayerPosition() const { return ReadVec(PlayerRecord() + m_layout.field.Position); }

void Game::SetPlayerPosition(const Vec3& p) const
{
    uint32_t a = PlayerRecord() + m_layout.field.Position;
    SetF32(a, p.x);
    SetF32(a + 4, p.y);
    SetF32(a + 8, p.z);
}

float Game::PlayerYaw() const
{
    uint32_t r = PlayerRecord();
    float fx = F32(r + m_layout.field.ForwardX), fz = F32(r + m_layout.field.ForwardZ);
    float n = std::hypot(fx, fz);
    if (!(n >= 1e-6f) || !Sane(n))
    {
        return 0;
    }
    return std::atan2(fx / n, fz / n) * RadToDeg;
}

float Game::PlayerHeading() const
{
    return static_cast<float>(static_cast<int16_t>(rd16(PlayerRecord() + m_layout.field.Heading))) * (360.0f / 65536.0f);
}

void Game::SetPlayerHeading(float degrees) const
{
    float d = std::fmod(degrees, 360.0f);
    if (d < 0)
    {
        d += 360.0f;
    }
    wr16(PlayerRecord() + m_layout.field.Heading, static_cast<uint16_t>(static_cast<int32_t>(std::floor(d * (65536.0f / 360.0f) + 0.5f))));
}

void Game::SetGunElevation(float degrees) const
{
    int v = std::clamp(static_cast<int>(std::floor(degrees * (65536.0f / 360.0f) + 0.5f)), -GunElevationMax, GunElevationMax);
    wr16(PlayerRecord() + m_layout.field.GunElevation, static_cast<uint16_t>(v));
}

bool Game::PlayerAiming() const { return rd8(PlayerRecord() + m_layout.field.Stance) == StanceAim; }

bool Game::PlayerMoveHeld() const { return (rd32(PlayerRecord() + m_layout.field.Actions + 8) & ActMove) != 0; }

bool Game::PlayerBusy() const { return rd8(PlayerRecord() + m_layout.field.Substate) == 0x40; }

bool Game::AimTarget(Vec3* position) const
{
    uint32_t r = PlayerRecord();
    uint8_t stance = rd8(r + m_layout.field.Stance);
    // Aiming, or hit while aiming, with the aim still held: the game keeps the target it picked
    if ((stance != StanceAim && stance != StanceAimHit) || (rd32(r + m_layout.field.Actions + 8) & ActAim) == 0)
    {
        return false;
    }
    uint32_t t = rd32(r + m_layout.field.AimTarget);
    // An enemy record (never a stray word): inside the finder's table, on a record's start
    if (t < m_layout.enemies || t >= m_layout.characters || (t - m_layout.enemies) % m_layout.enemySize != 0)
    {
        return false;
    }
    if (rd8(t + EnemyLive) == 0 || rd32(t + EnemyModel) == 0 || (rd32(t + m_layout.enemyFlags) & 1) != 0)
    {
        return false;
    }
    *position = ReadVec(t + EnemyPosition);
    return Sane(position->x) && Sane(position->y) && Sane(position->z);
}

void Game::AimMoveStep(float right, float forward, float dt, float speed) const
{
    float r = std::clamp(right, -1.0f, 1.0f), f = std::clamp(forward, -1.0f, 1.0f);
    if (r == 0 && f == 0)
    {
        return;
    }
    float mag = std::hypot(r, f);
    if (mag > 1.0f)
    {
        r /= mag;
        f /= mag;
    }
    float yaw = PlayerHeading() * DegToRad, step = speed * dt;
    // forward = (sin yaw, cos yaw), right = (-cos yaw, sin yaw); the game still resolves her against the world afterwards
    Vec3 p = PlayerPosition();
    p.x += (-std::cos(yaw) * r + std::sin(yaw) * f) * step;
    p.z += (std::sin(yaw) * r + std::cos(yaw) * f) * step;
    SetPlayerPosition(p);
}

void Game::WriteCamera(const Vec3& eye, const Vec3& look) const
{
    SetF32(m_layout.eye, eye.x);
    SetF32(m_layout.eye + 4, eye.y);
    SetF32(m_layout.eye + 8, eye.z);
    SetF32(m_layout.target, look.x);
    SetF32(m_layout.target + 4, look.y);
    SetF32(m_layout.target + 8, look.z);
}

Vec3 Game::CameraEye() const { return ReadVec(m_layout.eye); }

float Game::Fov() const
{
    float v = F32(m_layout.fovDeg);
    return Sane(v) && v > 0 ? v : 60.0f;
}

float Game::Aspect() const
{
    float v = F32(m_layout.aspect);
    return Sane(v) && v > 0 ? v : 4.0f / 3.0f;
}

void Game::SetFov(float degrees) const { SetF32(m_layout.fovDeg, degrees); }

void Game::SetNearFar(float nearValue, float farPlane) const
{
    if (nearValue > 0)
    {
        SetF32(m_layout.nearValue, nearValue);
    }
    if (farPlane > 0)
    {
        SetF32(m_layout.farPlane, farPlane);
    }
}

float Game::NearValue() const { return F32(m_layout.nearValue); }
float Game::FarPlane() const { return F32(m_layout.farPlane); }

void Game::SetAspect(float scale)
{
    if (!m_haveAspect)
    {
        m_aspect = F32(m_layout.aspect);
        m_haveAspect = true;
    }
    SetF32(m_layout.aspect, scale);
}

void Game::RestoreAspect()
{
    if (m_haveAspect)
    {
        SetF32(m_layout.aspect, m_aspect);
        m_haveAspect = false;
    }
}

uint32_t Game::CutCount() const
{
    uint32_t n = rd8(m_layout.cuts + CutCountAt);
    return n > 0 && n <= MaxCuts ? n : 0;
}

uint32_t Game::CutIndex() const { return rd8(m_layout.cuts + CutCurrentAt); }

bool Game::CutEye(uint32_t index, Vec3* eye) const
{
    if (index >= CutCount())
    {
        return false;
    }
    *eye = ReadIntVec(m_layout.cuts + CutRecords + index * CutSize + CutEyeAt);
    return true;
}

bool Game::CutPose(Vec3* eye, Vec3* look, float* fov) const
{
    uint32_t i = CutIndex();
    if (i >= CutCount())
    {
        return false;
    }
    uint32_t rec = m_layout.cuts + CutRecords + i * CutSize;
    *eye = ReadIntVec(rec + CutEyeAt);
    *look = ReadIntVec(rec + CutTargetAt);
    *fov = F32(rec + CutFovAt);
    return true;
}

void Game::ForceCut(uint32_t index) const
{
    // The index first, then the flag
    wr8(m_layout.cuts + CutForceIndexAt, static_cast<uint8_t>(index));
    wr8(m_layout.cuts + CutForceOnAt, 1);
}

void Game::ReleaseCut() const { wr8(m_layout.cuts + CutForceOnAt, 0); }

void Game::RestoreCutCamera() const
{
    Vec3 eye, look;
    float fov;
    if (CutPose(&eye, &look, &fov))
    {
        WriteCamera(eye, look);
        if (Sane(fov) && fov > 1.0f && fov < 120.0f)
        {
            SetFov(fov);
        }
    }
}

void Game::ShowAllObjects() const
{
    // Nothing to put back: the game writes these flags again at every cut change
    uint32_t n = rd16(m_layout.objectCount);
    if (n == 0 || n > 256)
    {
        return;
    }
    for (uint32_t i = 0; i < n; i++)
    {
        uint8_t f = rd8(m_layout.objectFlags + i);
        if ((f & 1) == 0)
        {
            wr8(m_layout.objectFlags + i, f | 1);
        }
    }
}

// Each of the room's fixed cameras draws only the objects of its mask: a cut leaves out what stands between its lens and the
// room (the wall it looks through, the pictures on it, a bulb by the lens), and the game still picks its cut from where she
// stands while this camera drives, so those walls went missing from the view. Every cut's mask also gets the objects the cuts
// that look at them draw; one every cut that looks at it leaves out stays out (the bar restroom's vent grille), as do objects
// no cut draws and an event's hide (bit 7)
uint32_t Game::ArtMask(uint32_t cut, uint32_t word) const { return m_artOriginal[cut][word] | m_artAdd[word]; }

static uint32_t ArtMaskAddress(const Layout& l, uint32_t cut, uint32_t word)
{
    return l.cuts + CutRecords + cut * CutSize + CutMask + 4 * word;
}

bool Game::ArtOurs() const
{
    if (rd8(m_layout.cuts + CutCountAt) != m_artCuts || (rd16(m_layout.objectCount) + 31u) / 32u != m_artWords)
    {
        return false;
    }
    for (uint32_t c = 0; c < m_artCuts; c++)
    {
        for (uint32_t w = 0; w < m_artWords; w++)
        {
            if (rd32(ArtMaskAddress(m_layout, c, w)) != ArtMask(c, w))
            {
                return false;
            }
        }
    }
    return true;
}

// Bit 0 of the objects whose bit the camera's mask of the current cut changes, as the camera's mask has it (ours) or the game's
void Game::ArtCurrent(bool ours) const
{
    uint32_t cur = rd8(m_layout.cuts + CutCurrentAt), count = rd16(m_layout.objectCount);
    if (cur >= m_artCuts)
    {
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t bit = 1u << (i & 31);
        uint32_t game = m_artOriginal[cur][i >> 5] & bit, mine = ArtMask(cur, i >> 5) & bit;
        if (mine == game)
        {
            continue;
        }
        bool want = (ours ? mine : game) != 0;
        uint8_t f = rd8(m_layout.objectFlags + i);
        if (((f & 1) != 0) != want)
        {
            wr8(m_layout.objectFlags + i, want ? (f | 1) : (f & ~1u));
        }
    }
}

void Game::ArtWrite() const
{
    for (uint32_t c = 0; c < m_artCuts; c++)
    {
        for (uint32_t w = 0; w < m_artWords; w++)
        {
            if (rd32(ArtMaskAddress(m_layout, c, w)) != ArtMask(c, w))
            {
                wr32(ArtMaskAddress(m_layout, c, w), ArtMask(c, w));
            }
        }
    }
}

void Game::ArtKeep()
{
    if (m_artOn && ArtOurs())
    {
        return;
    }
    m_artOn = false;
    uint32_t n = rd8(m_layout.cuts + CutCountAt), count = rd16(m_layout.objectCount);
    if (n == 0 || n > MaxCuts || count == 0 || count > 32 * MaskWords)
    {
        return;
    }
    m_artCuts = n;
    m_artWords = (count + 31) / 32;
    for (uint32_t c = 0; c < n; c++)
    {
        for (uint32_t w = 0; w < m_artWords; w++)
        {
            m_artOriginal[c][w] = rd32(ArtMaskAddress(m_layout, c, w));
        }
        // The cut's camera: its eye, axes and the tangents of its half fields of view (with the margin)
        uint32_t rec = m_layout.cuts + CutRecords + c * CutSize;
        CutCamera& k = m_artCamera[c];
        Vec3 target = ReadIntVec(rec + CutTargetAt);
        float fov = F32(rec + CutFovAt);
        if (!Sane(fov) || fov < 1 || fov > 170)
        {
            fov = 60;
        }
        k.eye = ReadIntVec(rec + CutEyeAt);
        k.forward = {target.x - k.eye.x, target.y - k.eye.y, target.z - k.eye.z};
        float l = std::sqrt(k.forward.x * k.forward.x + k.forward.y * k.forward.y + k.forward.z * k.forward.z);
        k.ok = l > 1e-3f;
        if (!k.ok)
        {
            continue;
        }
        k.forward = {k.forward.x / l, k.forward.y / l, k.forward.z / l};
        l = std::hypot(k.forward.z, k.forward.x);
        k.right = {l > 1e-4f ? k.forward.z / l : 1.0f, 0.0f, l > 1e-4f ? -k.forward.x / l : 0.0f};
        k.up = {k.right.y * k.forward.z - k.right.z * k.forward.y, k.right.z * k.forward.x - k.right.x * k.forward.z,
                k.right.x * k.forward.y - k.right.y * k.forward.x};
        float tv = std::tan(fov * 0.5f * DegToRad);
        k.tanY = TanAdd(tv, ArtMarginTan);
        k.tanX = TanAdd(tv * (4.0f / 3.0f), ArtMarginTan);
    }
    // Per object: drawn by a cut that looks at it; one no cut looks at: drawn by any cut
    std::fill(std::begin(m_artAdd), std::end(m_artAdd), 0u);
    for (uint32_t i = 0; i < count; i++)
    {
        Vec3 p = ReadVec(m_layout.objectMatrices + 0x40 * i + 0x30);
        bool sane = Sane(p.x) && Sane(p.y) && Sane(p.z);
        bool any = false, looked = false, drawn = false;
        for (uint32_t c = 0; c < n; c++)
        {
            bool bit = ((m_artOriginal[c][i >> 5] >> (i & 31)) & 1) != 0;
            any |= bit;
            const CutCamera& k = m_artCamera[c];
            if (!k.ok || !sane)
            {
                continue;
            }
            float rx = p.x - k.eye.x, ry = p.y - k.eye.y, rz = p.z - k.eye.z;
            float z = rx * k.forward.x + ry * k.forward.y + rz * k.forward.z;
            float x = std::fabs(rx * k.right.x + ry * k.right.y + rz * k.right.z);
            float y = std::fabs(rx * k.up.x + ry * k.up.y + rz * k.up.z);
            if (z > 0 && (k.tanX == 0 || x <= z * k.tanX) && (k.tanY == 0 || y <= z * k.tanY))
            {
                looked = true;
                drawn |= bit;
            }
        }
        if (looked ? drawn : any)
        {
            m_artAdd[i >> 5] |= 1u << (i & 31);
        }
    }
    ArtWrite();
    m_artOn = true;
    ArtCurrent(true); // the current cut was set with the game's mask
}

void Game::KeepArtwork() { ArtKeep(); }

void Game::RestoreArtwork()
{
    if (!m_artOn)
    {
        return;
    }
    if (ArtOurs())
    {
        for (uint32_t c = 0; c < m_artCuts; c++)
        {
            for (uint32_t w = 0; w < m_artWords; w++)
            {
                if (rd32(ArtMaskAddress(m_layout, c, w)) != m_artOriginal[c][w])
                {
                    wr32(ArtMaskAddress(m_layout, c, w), m_artOriginal[c][w]);
                }
            }
        }
        ArtCurrent(false);
    }
    m_artOn = false;
}

void Game::SetNearHideScale(float scale)
{
    // Never above the game's radii: a larger radius only hides more of what's near the camera (characters)
    scale = std::clamp(scale, 0.0f, 1.0f);
    if (!m_haveNearHide)
    {
        for (int i = 0; i < 4; i++)
        {
            m_nearHide[i] = F32(m_layout.nearHide + 4 * i);
        }
        m_haveNearHide = true;
    }
    for (int i = 0; i < 4; i++)
    {
        SetF32(m_layout.nearHide + 4 * i, m_nearHide[i] * scale); // from the game's, never compounded
    }
}

void Game::RestoreNearHide()
{
    if (!m_haveNearHide)
    {
        return;
    }
    for (int i = 0; i < 4; i++)
    {
        SetF32(m_layout.nearHide + 4 * i, m_nearHide[i]);
    }
    m_haveNearHide = false;
}

void Game::SetEntityLight(uint32_t rgb)
{
    // ModernCam's set_entity_light: R, G, B, alpha 0xFF, then the mode byte 2
    uint32_t rgba = ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | 0xFF000000u;
    if (!m_haveLight)
    {
        m_light = rd32(m_layout.entityLight);
        m_haveLight = true;
    }
    if ((rd32(m_layout.entityLight) & 0xFFFFFF) != (rgba & 0xFFFFFF))
    {
        wr32(m_layout.entityLight, rgba); // the colour first, then the mode
        wr8(m_layout.entityLightMode, 2);
    }
}

void Game::RestoreEntityLight()
{
    if (m_haveLight)
    {
        wr32(m_layout.entityLight, m_light);
        wr8(m_layout.entityLightMode, 2);
        m_haveLight = false;
    }
}

const std::vector<Wall>& Game::Walls(float dt)
{
    uint32_t base = rd32(m_layout.wallPointer);
    m_wallsAge += dt;
    if (m_haveWalls && base == m_wallsBase && m_wallsAge < WallRefreshSeconds)
    {
        return m_walls;
    }
    m_haveWalls = true;
    m_wallsBase = base;
    m_wallsAge = 0;
    m_walls.clear();
    m_slabs.clear();
    if (!(base > 0x100000 && base < 0x2000000))
    {
        return m_walls;
    }
    for (uint32_t i = 0; i < MaxWalls; i++)
    {
        uint32_t rec = base + i * WallSize;
        if (!InRam(rec, WallSize))
        {
            break;
        }
        uint32_t flags = rd32(rec);
        if (flags == 0 || flags == 0xFFFFFFFFu)
        {
            break; // the list's end
        }
        float corners[9];
        for (int k = 0; k < 9; k++)
        {
            corners[k] = F32(rec + 4 + 4 * k);
        }
        Wall wall;
        Slab slab;
        if (WallFromRecord(flags, corners, F32(rec + 0x2C), &wall))
        {
            m_walls.push_back(wall);
        }
        else if (SlabFromRecord(corners, F32(rec + 0x2C), &slab))
        {
            m_slabs.push_back(slab);
        }
    }
    return m_walls;
}

void Game::PadMapApply(bool centreLeft, bool centreRight, bool hideR3) const
{
    // Offline the local player is mapped record 0 (the mapper's index for her)
    if (hideR3)
    {
        uint32_t held = rd32(m_layout.padRecords + PadHeld);
        if ((held & SrcR3) != 0)
        {
            wr32(m_layout.padRecords + PadHeld, held & ~SrcR3);
        }
    }
    if (centreLeft)
    {
        CentreStick(m_layout.padRecords + PadLeftStick);
    }
    if (centreRight)
    {
        CentreStick(m_layout.padRecords + PadRightStick);
    }
}

void Game::ReleaseWalkHold() const
{
    uint32_t a = PlayerRecord() + m_layout.field.Actions + ActWalkHold;
    if (rd16(a) != 0)
    {
        wr16(a, 0);
    }
}

// The actions the mapper makes of a held word: square, cross and circle through the control type's row of the button map,
// every other bit as it is
uint32_t Game::MappedActions(uint32_t held) const
{
    uint32_t row = m_layout.buttonMap + 12 * (rd8(m_layout.controlType) & 3);
    uint32_t out = held & ~(SrcSquare | SrcCross | SrcCircle);
    out |= (held & SrcSquare) ? rd32(row) : 0;
    out |= (held & SrcCross) ? rd32(row + 4) : 0;
    out |= (held & SrcCircle) ? rd32(row + 8) : 0;
    return out;
}

int Game::WalkTurn(int side) const
{
    // Just inside the turn limit (a step of the game's yaw, 16, is 0.09 degrees), and the part of a turn round near straight
    // behind her in which the side it began on is kept (a stick held straight back wobbles across it)
    constexpr int TurnTo = WalkTurnLimit - 0x100;
    constexpr int KeepSide = 0x7000;
    uint32_t stick = m_layout.padRecords + PadLeftStick;
    uint32_t record = PlayerRecord();
    // Only where the game can step her back: her own walking with the weapon down, player-driven, the stick pushed, and none
    // of the game's own turns round (the dash, its turn-round flag)
    if (static_cast<int16_t>(rd16(stick)) < WalkStickMin || rd8(record + m_layout.field.Stance) != StanceNormal ||
        rd8(record + m_layout.field.Action) > ActionBackStep || rd8(record + m_layout.field.Cpu) != 0 ||
        (rd32(record + m_layout.field.Flags) & m_layout.field.TurnsRound) != 0 ||
        (MappedActions(rd32(m_layout.padRecords + PadHeld)) & ActDash) != 0)
    {
        return 0;
    }
    float dx = F32(m_layout.target) - F32(m_layout.eye), dz = F32(m_layout.target + 8) - F32(m_layout.eye + 8);
    if (!Sane(dx) || !Sane(dz) || (dx == 0 && dz == 0))
    {
        return 0;
    }
    // The game's sum: the camera's yaw and the stick's angle in its steps of 16, less her heading
    int32_t yaw = static_cast<int32_t>(std::atan2(dx, dz) * (32768.0f / Pi));
    uint32_t dir = rd16(stick + 2);
    int32_t diff = static_cast<int16_t>(((static_cast<uint32_t>(yaw) & 0xFFF0u) + (dir & 0xFFF0u) -
                                         rd16(record + m_layout.field.Heading)) & 0xFFFFu);
    if (diff >= -TurnTo && diff <= TurnTo)
    {
        return 0;
    }
    // Further round: the stick's angle turned toward her heading to just inside the limit on the stick's side (the side
    // already turned to, near straight behind)
    if (side == 0 || (diff > -KeepSide && diff < KeepSide))
    {
        side = diff > 0 ? 1 : -1;
    }
    int32_t want = side * TurnTo;
    uint32_t turned = ((dir & 0xFFF0u) - static_cast<uint32_t>(diff - want) + 8u) & 0xFFF0u;
    wr16(stick + 2, static_cast<uint16_t>(turned | (dir & 0xFu)));
    return side;
}
}
