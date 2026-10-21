// What the Modern camera reads and writes in the game's memory: the camera block, the characters, the room's fixed cameras
// (cuts) and the objects they draw, the walls, the pad records the button mapper reads. Data only: the game's own camera code
// is held by native overrides of its functions (src/modern_camera.cpp), never by changing its code in memory.
//
// Adapted from REO-ModernCam by RaccoonRecomp (MIT), a recreation of Outbreak ModernCam by Snippy (MIT): its game.c, with
// Snippy's own addresses for the Japanese discs (his ro_addresses.py and ro_addresses_f2.py) where the US port moved them.
#pragma once

#include "modern_camera_rig.h"

#include <cstdint>
#include <vector>

namespace ModernCamera
{
// A character record's fields (where they are in each game: File 1's are ModernCam's offsets, checked in the code)
struct Fields
{
    uint32_t Id; // u8: the character's id, compared with Layout::localPlayerId
    uint32_t Stance; // u8: 1 weapon down, 2 aiming, 3 hit while aiming
    uint32_t Substate; // u8: 0x40 a scripted action
    uint32_t AimTarget; // u32: the enemy the auto-aim picked as she raised the weapon
    uint32_t Position; // f32 x, y, z
    uint32_t ForwardX; // f32: the model matrix's row 2
    uint32_t ForwardZ;
    uint32_t Heading; // s16: 0x10000 a turn
    uint32_t Action; // u8: 0 standing, 1 walking or turning, 2 the step back
    uint32_t Flags; // u32: 0x80 the game turns her round itself
    uint32_t GunElevation; // s16: 65536/360 a degree, within +-0x1FFF
    uint32_t Actions; // the action block: +0 pressed, +8 held, +0x20 the sticks, +0x32 the walk hold
    uint32_t Cpu; // u8: nonzero for a partner the game's AI plays
    uint32_t Live; // u8: the record is in use
    uint32_t AimState; // u8 with the weapon raised: 0 raising, 1 ready, 2 firing; down: her action's kind
    uint32_t ActionPhase; // u8: 0x0B aiming (or standing, the weapon down)
    uint32_t AutoAimLeft; // u16: the frames the auto-aim still turns her as she raises the weapon
    uint32_t CharacterId; // low byte: 0 Kevin .. 7 Cindy
    uint32_t Model; // the model: its first word points at the skeleton's node table
    uint32_t PadMode; // u8
    uint32_t AutoAimStep; // s16: the auto-aim's turn each of those frames
    uint32_t Weapon; // u8: the equipped weapon's item record
    uint32_t TurnsRound; // the bit of Flags set while the game turns her round itself
};

constexpr Fields File1Fields = {
    .Id = 0x003,
    .Stance = 0x008,
    .Substate = 0x009,
    .AimTarget = 0x024,
    .Position = 0x038,
    .ForwardX = 0x070,
    .ForwardZ = 0x078,
    .Heading = 0x092,
    .Action = 0x54F,
    .Flags = 0xBA0,
    .GunElevation = 0xBC8,
    .Actions = 0xF78,
    .Cpu = 0xFF0,
    .Live = 0x000,
    .AimState = 0x009,
    .ActionPhase = 0x00A,
    .AutoAimLeft = 0x00C,
    .CharacterId = 0x030,
    .Model = 0x034,
    .PadMode = 0xB98,
    .AutoAimStep = 0xC4E,
    .Weapon = 0xC7C,
    .TurnsRound = 0x80
};

// File 2's (the same code matched to File 1's: docs/mods.md)
constexpr Fields File2Fields = {
    .Id = 0x003,
    .Stance = 0x008,
    .Substate = 0x009,
    .AimTarget = 0x024,
    .Position = 0x038,
    .ForwardX = 0x070,
    .ForwardZ = 0x078,
    .Heading = 0x092,
    .Action = 0x54B,
    .Flags = 0xBB8,
    .GunElevation = 0xBE0,
    .Actions = 0xF88,
    .Cpu = 0xBB4,
    .Live = 0x000,
    .AimState = 0x009,
    .ActionPhase = 0x00A,
    .AutoAimLeft = 0x00C,
    .CharacterId = 0x030,
    .Model = 0x034,
    .PadMode = 0xBB0,
    .AutoAimStep = 0xC5E,
    .Weapon = 0xC8C,
    .TurnsRound = 0x20
};

// Where one game keeps it (docs/mods.md has the evidence for each address)
struct Layout
{
    const char* name;
    // The camera block (the executable's bss): the eye then the point looked at (6 f32), the fixed-cut handler's look distance
    // ModernCam calls its near value, the far plane, the vertical field of view in degrees; the whole block the camera's init
    // writes (snapshot while the rail handlers run); the frame's view matrix (rows; x right, y up, looking along -z)
    uint32_t eye, target, nearValue, farPlane, fovDeg;
    uint32_t block, blockSize;
    uint32_t aspect;          // f32 4/3: the projection's aspect ("horizontal scale")
    uint32_t entityLight;     // R, G, B, A, then the mode byte at entityLightMode (2: forced)
    uint32_t entityLightMode;
    uint32_t frames;          // u32: the vertical blanks counted at each game frame
    uint32_t controlType;     // u8 0-3: the controller settings' type
    uint32_t padRecords;      // the mapped pad records (48 bytes each): +8 held buttons, +0x20 left stick, +0x28 right stick
    uint32_t buttonMap;       // u32 actions per control type for square, cross, circle (12 bytes a type)
    uint32_t padFrames;       // the word pointing at port 0's two raw DualShock frames (0x80 bytes each)
    uint32_t characters;      // the characters' records, the player's first offline
    uint32_t characterSize;
    uint32_t localPlayerId;   // u8: the id (record +3) of the character this console's player drives
    uint32_t enemies;         // the enemies' records the auto-aim's target finder walks
    uint32_t enemySize;
    uint32_t enemyFlags;      // an enemy's u32 flags: bit 0 set, the target finder skips it
    uint32_t cuts;            // the room's fixed cameras: +1 count, +3 current, +5 forced, +6 forcing; records from +0x34
    uint32_t objectCount;     // u16: the room's objects
    uint32_t objectFlags;     // u8 each: bit 0 drawn, bit 7 hidden by an event
    uint32_t objectMatrices;  // f32 4x4 each (rows, the origin at +0x30)
    uint32_t wallPointer;     // the word pointing at the room's wall records (0x38 bytes each)
    uint32_t nearHide;        // 4 f32: the radii within which models near the camera are not drawn ([1] the characters')
    uint32_t systemPadPressed; // u32: the system pad's buttons newly pressed this frame (the ad-lib reads R3 there)
    uint32_t roomNow;         // u32: the room she's in (changes during a door's loading screen)
    Fields field;             // a character record's fields
};

extern const Layout File1;
extern const Layout File2;


constexpr uint8_t StanceNormal = 1, StanceAim = 2, StanceAimHit = 3;
constexpr uint8_t ActionBackStep = 2;
constexpr uint32_t ActForward = 0x0001, ActMove = 0x000F, ActDash = 0x0010, ActFire = 0x0020, ActAim = 0x0400;
// The action block's walk hold (u16, +0x32): set when the room's camera cuts while the stick walks her, so the new fixed
// angle doesn't turn her (she walks on the way she went until the stick is let go)
constexpr uint32_t ActWalkHold = 0x32;
// The game's stick walking: a stick pushed this far or more walks her; up to this far round from her heading she turns
// toward it, further round she steps back while an enemy is near (US game.bin 0x006621F0, its table at 0x00712B30)
constexpr int WalkStickMin = 16;
constexpr int WalkTurnLimit = 0x5000;
constexpr int GunElevationMax = 0x1FFF;

// A view of one game's memory for the camera (the layout and the character it follows)
class Game
{
public:
    explicit Game(const Layout& layout) : m_layout(layout) {}
    const Layout& layout() const { return m_layout; }

    // The record of the character this console's player drives (the player's on its own, offline)
    uint32_t PlayerRecord() const;
    bool PlayerLive() const; // her position is sane and not the origin
    Vec3 PlayerPosition() const;
    void SetPlayerPosition(const Vec3& p) const;
    float PlayerYaw() const;       // from her model matrix: the way she faces
    float PlayerHeading() const;   // from her heading field
    void SetPlayerHeading(float degrees) const;
    void SetGunElevation(float degrees) const;
    bool PlayerAiming() const;     // in the aim stance
    bool PlayerMoveHeld() const;   // the game's movement actions for her are held: the player moves her
    bool PlayerBusy() const;       // in a scripted action
    // The game's aim lock: in the aim stance with a target it picked that it can still target. Its position
    bool AimTarget(Vec3* position) const;
    // She moved in her own frame (File 1 roots her as she aims): +right her right, +forward her facing
    void AimMoveStep(float right, float forward, float dt, float speed) const;

    void WriteCamera(const Vec3& eye, const Vec3& look) const;
    Vec3 CameraEye() const;
    float Fov() const;
    void SetFov(float degrees) const;
    void SetNearFar(float nearValue, float farPlane) const; // <= 0 leaves one alone
    float NearValue() const;
    float FarPlane() const;
    void SetAspect(float scale);
    void RestoreAspect();

    uint32_t CutCount() const;
    uint32_t CutIndex() const;
    bool CutEye(uint32_t index, Vec3* eye) const;
    bool CutPose(Vec3* eye, Vec3* look, float* fov) const; // the current cut's authored camera
    void ForceCut(uint32_t index) const;
    void ReleaseCut() const;
    // The room's own camera of the current cut written back (its eye, target and field of view), so the game's view
    // returns at once and not at its next cut change
    void RestoreCutCamera() const;

    void ShowAllObjects() const;
    // The walls and pictures a fixed camera leaves out kept drawn while the camera drives (every cut's mask also holds what
    // the cuts that look at an object draw); restored puts the game's masks back (not a new room's: those are the game's)
    void KeepArtwork();
    void RestoreArtwork();
    void SetNearHideScale(float scale); // the near-camera hide radii scaled from the game's, never above them
    void RestoreNearHide();
    void SetEntityLight(uint32_t rgb);
    void RestoreEntityLight();

    // The room's walls, read again when the room's wall pointer changes or after a second
    const std::vector<Wall>& Walls(float dt);
    // The room's floors and ceilings, gathered with the walls (valid after Walls)
    const std::vector<Slab>& Slabs() const { return m_slabs; }
    void ForgetWalls() { m_wallsBase = 0; m_haveWalls = false; }

    // At the button mapper, for her record: the mapped record's sticks centred (the game doesn't see them), and R3 taken from
    // it (the camera's swap shoulder: the mapper makes it the action 0x2000, seen in her action block while it's held)
    void PadMapApply(bool centreLeft, bool centreRight, bool hideR3) const;
    // Her walk hold let go: the left stick walks her relative to this camera again
    void ReleaseWalkHold() const;
    // With the left stick further round from her heading than the game's walking turns her: the stick's angle turned toward her
    // to just inside that, so she turns round toward it instead of stepping back. side: the last frame's (+1 her left, -1 her
    // right, 0 none); this frame's returned
    int WalkTurn(int side) const;

private:
    uint32_t MappedActions(uint32_t held) const;
    void ArtKeep();
    bool ArtOurs() const;
    void ArtCurrent(bool ours) const;
    void ArtWrite() const;
    uint32_t ArtMask(uint32_t cut, uint32_t word) const;

    const Layout& m_layout;
    bool m_haveAspect = false;
    float m_aspect = 0;
    bool m_haveNearHide = false;
    float m_nearHide[4] = {};
    bool m_haveLight = false;
    uint32_t m_light = 0;
    // The walls
    bool m_haveWalls = false;
    uint32_t m_wallsBase = 0;
    float m_wallsAge = 0;
    std::vector<Wall> m_walls;
    std::vector<Slab> m_slabs;
    // The artwork kept: each cut's own mask, what every cut also draws, the cuts' cameras
    static constexpr uint32_t MaxCuts = 64, MaskWords = 4;
    struct CutCamera
    {
        bool ok = false;
        Vec3 eye, forward, right, up;
        float tanX = 0, tanY = 0;
    };
    bool m_artOn = false;
    uint32_t m_artCuts = 0, m_artWords = 0;
    uint32_t m_artAdd[MaskWords] = {};
    uint32_t m_artOriginal[MaxCuts][MaskWords] = {};
    CutCamera m_artCamera[MaxCuts];
};
}
