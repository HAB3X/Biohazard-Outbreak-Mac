// Resident Evil 4 AI routines ported to the Resident Evil Outbreak engine (biohazard-outbreak-mac).
// Handles Ganado villagers, castle zealots, Chainsaw Man (Dr. Salvador), El Gigante boss, and Garrador.
#pragma once

#include "ps2/recomp.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Re4
{

// Enemy kinds mapped to RE4 archetypes and guest horror stalkers/bosses (kinds >= 100)
enum EnemyKind : uint8_t
{
    KindGanado = 100,       // Villager (em10)
    KindZealot = 101,       // Castle Zealot (em14)
    KindChainsaw = 104,     // Dr. Salvador / Chainsaw Man (em10 type 4 / em14)
    KindGigante = 120,      // El Gigante boss (em20 / em2b)
    KindGarrador = 122,     // Garrador (em22 / em1c)

    // Haunting Ground (Demento)
    KindDebilitas = 140,    // Debilitas groundkeeper boss
    KindHewie = 145,        // Hewie dog companion / pursuer

    // Clock Tower 3
    KindSledgehammer = 150, // Sledgehammer Robert Morris
    KindCorroder = 151,     // Corroder John Haigh acid stalker

    // Silent Hill Series
    KindPyramidHead = 160,  // Red Pyramid Thing / Pyramid Head
    KindNurse = 161,        // Bubble Head Nurse

    // Fatal Frame Series
    KindSae = 170,          // Sae Kurosawa Crimson Sacrifice ghost

    // Kuon
    KindGaki = 180,         // Twin Gaki demon

    // Forbidden Siren
    KindShibito = 190,      // Hanuda Shibito Sniper / Stalker
};

// Weapons matching RE4 & guest horror weapons
enum WeaponType : uint8_t
{
    WepUnarmed = 0,
    WepAxe = 1,
    WepPitchfork = 2,
    WepChainsaw = 4,
    WepClaws = 5,
    WepBoulder = 6,
    WepTorch = 7,
    WepScythe = 8,
    WepClub = 9,
    WepSledgehammer = 10,
    WepAcidSprayer = 11,
    WepGreatKnife = 12,
    WepRustyPipe = 13,
    WepGhostCurse = 14,
    WepRifle = 15,
};

// AI states mirroring the RE4 and guest horror state machines
enum class AiState : uint8_t
{
    Idle,
    Perceive,
    Approach,
    Dash,
    AttackMelee,
    AttackAxe,
    AttackPitchfork,
    ChainsawRev,
    ChainsawAdvance,
    ChainsawInstakill,
    GiganteWalk,
    GiganteCharge,
    GiganteGroundSlam,
    GiganteBoulderThrow,
    GarradorListen,
    GarradorCharge,
    GarradorClawSlash,

    // Guest stalker / boss states
    DebilitasCharge,
    DebilitasSlam,
    HewieBark,
    HewiePounce,
    SledgehammerMarch,
    SledgehammerSmash,
    CorroderAcidSpray,
    PyramidHeadMarch,
    PyramidHeadExecution,
    NurseLurch,
    NurseFlurry,
    SaeFloat,
    SaeTeleport,
    SaeChoke,
    GakiSkitter,
    GakiLeap,
    ShibitoAim,
    ShibitoFire,

    Stagger,
    HeavyStagger,
    Death,
    Dead,
};

// 3D vector helper
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    float LengthXZ() const { return std::hypot(x, z); }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float DistanceXZ(const Vec3& o) const { return std::hypot(x - o.x, z - o.z); }
    float Distance(const Vec3& o) const { return (*this - o).Length(); }

    Vec3 Normalized() const
    {
        float len = Length();
        if (len < 1e-5f) return {0.0f, 0.0f, 1.0f};
        return {x / len, y / len, z / len};
    }
};

// Per-enemy runtime state
struct Re4EnemyState
{
    uint32_t address = 0;
    EnemyKind kind = KindGanado;
    WeaponType weapon = WepUnarmed;
    AiState state = AiState::Idle;

    uint32_t stateTimer = 0;
    uint32_t attackCooldown = 0;
    uint32_t specialCooldown = 0;
    uint8_t routeType = 0;          // 0..10 for surrounding lateral offsets (RE4 Route_type)

    bool alert = false;
    bool sawRunning = false;
    int totalDamageTaken = 0;       // Cumulative damage for boss stagger thresholds
    int staggerTimer = 0;

    Vec3 chargeTarget{0.0f, 0.0f, 0.0f};
    Vec3 boulderPos{0.0f, 0.0f, 0.0f};
    Vec3 boulderVel{0.0f, 0.0f, 0.0f};
    bool boulderActive = false;

    uint32_t targetRecord = 0;
    Vec3 lastTargetPos{0.0f, 0.0f, 0.0f};
    int lastHp = 0;
    bool initialized = false;
};

// Check if an enemy kind byte belongs to the RE4 module (>= 100)
inline bool IsRe4Enemy(uint8_t kind)
{
    return kind >= 100;
}

// Perception check: distance and sight-cone check to target (RE4 em10FindCk)
// In RE4: 60-degree half-cone (120-degree field of view), close-proximity awareness, vertical limit
bool CheckSightCone(const Vec3& enemyPos, uint16_t enemyFacing, const Vec3& targetPos,
                    float maxDistance = 1500.0f, float halfFovRadians = 1.0471976f /* PI/3 = 60 deg */,
                    float maxVerticalDiff = 200.0f);

// Compute angular difference between current facing and heading to target (in radians, [-PI, PI])
float ComputeAngleDiff(uint16_t currentFacing, const Vec3& from, const Vec3& to);

// Turn facing towards target by at most turnRate units (0x10000 = 360 deg)
uint16_t TurnTowards(uint16_t currentFacing, const Vec3& from, const Vec3& to, uint16_t turnRate);

// Calculate surrounding/flanking target position using RE4's Route_type offset table
Vec3 CalculateFlankTarget(const Vec3& targetPos, uint16_t targetFacing, const Vec3& enemyPos,
                         uint8_t routeType, float distance);

// Step position forward along facing heading by speed units
Vec3 AdvancePosition(const Vec3& currentPos, uint16_t facing, float speed);

// Check if attack swing hits target (distance + front arc check)
bool CheckAttackHit(const Vec3& enemyPos, uint16_t enemyFacing, const Vec3& targetPos,
                    float attackRange, float attackArcRadians);

// Deal damage to character in Outbreak memory layout
void DealDamage(uint32_t targetRecord, int damage, bool lethal = false);

// Sound event IDs mirroring RE4 & guest horror games SndCall
constexpr int SndAxeSwing = 0x3D;
constexpr int SndChainsawRev = 0x4C;
constexpr int SndChainsawSwing = 0x50;
constexpr int SndChainsawDecap = 0x54;
constexpr int SndGiganteRoar = 0x0A;
constexpr int SndGiganteSlam = 0x8D;
constexpr int SndGarradorRoar = 0xB3;
constexpr int SndDebilitasRoar = 0xC0;
constexpr int SndDebilitasSlam = 0xC1;
constexpr int SndHewieBark = 0xC2;
constexpr int SndSledgehammerSmash = 0xD0;
constexpr int SndAcidSpray = 0xD1;
constexpr int SndPyramidKnifeDrag = 0xE0;
constexpr int SndPyramidExecution = 0xE1;
constexpr int SndGhostLaugh = 0xF0;
constexpr int SndShibitoCry = 0xF5;
constexpr int SndRifleFire = 0xF6;

// Set custom sound callback for telemetry / audio hooks
typedef void (*SoundCallback)(int soundId, const Vec3& pos, uint32_t enemyAddr);
void SetSoundCallback(SoundCallback callback);

// Main dispatch routine called from Outbreak engine stand-in
// c->r[4].uw[0] points to the Outbreak enemy struct
void TickEnemy(Ps2Cpu* c);

// Direct state inspection / manipulation (useful for unit tests and telemetry)
Re4EnemyState* GetEnemyState(uint32_t address);
void Reset();

} // namespace Re4
