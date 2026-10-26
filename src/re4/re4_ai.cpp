// Resident Evil 4 AI routines ported to the Resident Evil Outbreak engine (biohazard-outbreak-mac).
// Implements Ganado villagers, castle zealots, Chainsaw Man (Dr. Salvador), El Gigante boss, and Garrador.
#include "re4_ai.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Re4
{
namespace
{

constexpr double Pi = 3.14159265358979323846;
constexpr double TurnUnits = 65536.0;
constexpr double RadToTurn = TurnUnits / (2.0 * Pi);
constexpr double TurnToRad = (2.0 * Pi) / TurnUnits;

constexpr uint32_t OffsetActive = 0x00;
constexpr uint32_t OffsetKind = 0x03;
constexpr uint32_t OffsetTarget = 0x24;
constexpr uint32_t OffsetPosition = 0x38;
constexpr uint32_t OffsetFacing = 0x92;
constexpr uint32_t OffsetDamageTaken = 0x138;
constexpr uint32_t OffsetHitFlag = 0x13F;
constexpr uint32_t OffsetRoom = 0x1E2;

// Motion indices for Outbreak enemy animation blending
constexpr uint8_t MotionIdle = 0;
constexpr uint8_t MotionWalk = 1;
constexpr uint8_t MotionRun = 2;
constexpr uint8_t MotionAttackMelee = 3;
constexpr uint8_t MotionAttackWeapon = 4;
constexpr uint8_t MotionAttackHeavy = 5;
constexpr uint8_t MotionFlinch = 6;
constexpr uint8_t MotionKnockdown = 7;
constexpr uint8_t MotionDeath = 8;

struct GameLayout
{
    uint32_t characters;
    uint32_t characterSize;
    uint32_t hp;
    uint32_t hpMax;
    uint32_t motion;
};

GameLayout GetLayout(uint32_t enemyAddress)
{
    // File 2 enemy range is 0x403DB0..0x450000
    if (enemyAddress >= 0x403DB0 && enemyAddress < 0x450000)
    {
        return {0x47BD30, 0x1100, 0x540, 0x542, 0x54B};
    }
    // File 1 layout
    return {0x476DD0, 0x10E0, 0x544, 0x546, 0x54F};
}

float ReadF32(uint32_t address)
{
    uint32_t bits = rd32(address);
    float val;
    std::memcpy(&val, &bits, 4);
    return val;
}

void WriteF32(uint32_t address, float val)
{
    uint32_t bits;
    std::memcpy(&bits, &val, 4);
    wr32(address, bits);
}

Vec3 ReadVec3(uint32_t address)
{
    return {ReadF32(address), ReadF32(address + 4), ReadF32(address + 8)};
}

void WriteVec3(uint32_t address, const Vec3& v)
{
    WriteF32(address, v.x);
    WriteF32(address + 4, v.y);
    WriteF32(address + 8, v.z);
}

// Convert direction vector (from -> to) into Outbreak facing heading [0, 0xFFFF]
uint16_t Heading(const Vec3& from, const Vec3& to)
{
    const double angle = std::atan2(static_cast<double>(to.x - from.x), static_cast<double>(to.z - from.z)) * RadToTurn;
    return static_cast<uint16_t>(static_cast<int32_t>(std::lround(angle)) & 0xFFFF);
}

// Normalize angle to [-PI, PI]
float NormalizeRad(float rad)
{
    while (rad > static_cast<float>(Pi)) rad -= static_cast<float>(2.0 * Pi);
    while (rad < static_cast<float>(-Pi)) rad += static_cast<float>(2.0 * Pi);
    return rad;
}

// Global active RE4 enemies table
std::unordered_map<uint32_t, Re4EnemyState> g_enemies;
SoundCallback g_soundCallback = nullptr;

void EmitSound(int soundId, const Vec3& pos, uint32_t enemyAddr)
{
    if (g_soundCallback)
    {
        g_soundCallback(soundId, pos, enemyAddr);
    }
}

// Find closest living character in the same room
uint32_t FindNearestCharacter(const Vec3& enemyPos, int16_t room, const GameLayout& layout)
{
    uint32_t bestRecord = 0;
    float bestDistance = 1e9f;

    for (uint32_t i = 0; i < 4; i++)
    {
        const uint32_t rec = layout.characters + i * layout.characterSize;
        if (rd8(rec + OffsetActive) == 0) continue;

        const int16_t charRoom = static_cast<int16_t>(rd16(rec + OffsetRoom));
        if (charRoom != room) continue;

        const int16_t hp = static_cast<int16_t>(rd16(rec + layout.hp));
        if (hp <= 0) continue;

        const Vec3 charPos = ReadVec3(rec + OffsetPosition);
        const float dist = enemyPos.DistanceXZ(charPos);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            bestRecord = rec;
        }
    }

    return bestRecord;
}

// Check if character made sound this frame (running, attacking, shouting)
bool CheckCharacterSound(uint32_t charRecord, const GameLayout& layout)
{
    if (charRecord == 0 || rd8(charRecord + OffsetActive) == 0) return false;

    // Outbreak action/motion check: running or attacking
    const uint8_t motion = rd8(charRecord + layout.motion);
    if (motion == MotionRun || motion >= MotionAttackMelee)
    {
        return true;
    }

    // Speech byte at 0x1075 (File 1) / 0x1091 (File 2)
    const uint32_t speechOffset = (layout.characterSize == 0x10E0) ? 0x1075 : 0x1091;
    if (rd8(charRecord + speechOffset) != 0)
    {
        return true;
    }

    return false;
}

} // namespace

// Perception check: distance & sight cone check to target (RE4 em10FindCk)
bool CheckSightCone(const Vec3& enemyPos, uint16_t enemyFacing, const Vec3& targetPos,
                    float maxDistance, float halfFovRadians, float maxVerticalDiff)
{
    // Vertical height check (RE4 fabsf(em->pos.y - pPL->pos.y) > 2000.0f)
    if (std::abs(enemyPos.y - targetPos.y) > maxVerticalDiff)
    {
        return false;
    }

    const float dist = enemyPos.DistanceXZ(targetPos);
    if (dist > maxDistance)
    {
        return false;
    }

    // Close-proximity awareness: detects target unconditionally within 3.5m (350 units)
    if (dist <= 350.0f)
    {
        return true;
    }

    // Sight cone check: angle between enemy facing and vector to target
    const float angleDiff = std::abs(ComputeAngleDiff(enemyFacing, enemyPos, targetPos));
    return angleDiff <= halfFovRadians;
}

float ComputeAngleDiff(uint16_t currentFacing, const Vec3& from, const Vec3& to)
{
    const float enemyRad = static_cast<float>(static_cast<int16_t>(currentFacing)) * static_cast<float>(TurnToRad);
    const float targetRad = std::atan2(to.x - from.x, to.z - from.z);
    return NormalizeRad(targetRad - enemyRad);
}

uint16_t TurnTowards(uint16_t currentFacing, const Vec3& from, const Vec3& to, uint16_t turnRate)
{
    const uint16_t targetHeading = Heading(from, to);
    const int16_t delta = static_cast<int16_t>(targetHeading - currentFacing);

    if (delta > static_cast<int16_t>(turnRate))
    {
        return static_cast<uint16_t>(currentFacing + turnRate);
    }
    if (delta < -static_cast<int16_t>(turnRate))
    {
        return static_cast<uint16_t>(currentFacing - turnRate);
    }
    return targetHeading;
}

// Calculate flanking target position using RE4's Route_type offset table (em10.cpp:16509)
Vec3 CalculateFlankTarget(const Vec3& targetPos, uint16_t targetFacing, const Vec3& enemyPos,
                         uint8_t routeType, float distance)
{
    // Scale factor d based on distance: clamps to 400.0f units (4.0m) in Outbreak scale
    const float d = std::clamp(distance, 0.0f, 400.0f) * 0.0025f;

    // RE4 lateral offset table (Route_type 0..10)
    float lateralOffset = 0.0f;
    switch (routeType % 11)
    {
        case 0:  lateralOffset = 0.0f; break;
        case 1:  lateralOffset = 200.0f; break;  // +2.0m right
        case 2:  lateralOffset = -200.0f; break; // -2.0m left
        case 3:  lateralOffset = 300.0f; break;  // +3.0m right
        case 4:  lateralOffset = -300.0f; break; // -3.0m left
        case 5:  lateralOffset = 400.0f; break;  // +4.0m right
        case 6:  lateralOffset = -400.0f; break; // -4.0m left
        case 7:  lateralOffset = 100.0f; break;  // +1.0m right
        case 8:  lateralOffset = -100.0f; break; // -1.0m left
        case 9:  lateralOffset = 250.0f; break;  // +2.5m right
        case 10: lateralOffset = -250.0f; break; // -2.5m left
    }

    if (std::abs(lateralOffset) < 1.0f)
    {
        return targetPos;
    }

    // Direction vector from target to enemy
    float dx = enemyPos.x - targetPos.x;
    float dz = enemyPos.z - targetPos.z;
    const float len = std::hypot(dx, dz);
    if (len < 1e-4f) return targetPos;

    dx /= len;
    dz /= len;

    // Perpendicular vector for flanking (fanning out around the target)
    const float perpX = -dz;
    const float perpZ = dx;

    return {
        targetPos.x + perpX * lateralOffset * d,
        targetPos.y,
        targetPos.z + perpZ * lateralOffset * d
    };
}

Vec3 AdvancePosition(const Vec3& currentPos, uint16_t facing, float speed)
{
    const float rad = static_cast<float>(static_cast<int16_t>(facing)) * static_cast<float>(TurnToRad);
    return {
        currentPos.x + std::sin(rad) * speed,
        currentPos.y,
        currentPos.z + std::cos(rad) * speed
    };
}

bool CheckAttackHit(const Vec3& enemyPos, uint16_t enemyFacing, const Vec3& targetPos,
                    float attackRange, float attackArcRadians)
{
    const float dist = enemyPos.DistanceXZ(targetPos);
    if (dist > attackRange)
    {
        return false;
    }

    const float angleDiff = std::abs(ComputeAngleDiff(enemyFacing, enemyPos, targetPos));
    return angleDiff <= attackArcRadians;
}

void DealDamage(uint32_t targetRecord, int damage, bool lethal)
{
    if (targetRecord == 0 || rd8(targetRecord + OffsetActive) == 0) return;

    const GameLayout layout = GetLayout(targetRecord);
    const int16_t currentHp = static_cast<int16_t>(rd16(targetRecord + layout.hp));
    const int16_t newHp = lethal ? 0 : static_cast<int16_t>(std::max(0, currentHp - damage));

    wr16(targetRecord + layout.hp, static_cast<uint16_t>(newHp));
    wr16(targetRecord + OffsetDamageTaken, static_cast<uint16_t>(lethal ? 0x7FFF : damage));
    wr8(targetRecord + OffsetHitFlag, 1);
}

void SetSoundCallback(SoundCallback callback)
{
    g_soundCallback = callback;
}

Re4EnemyState* GetEnemyState(uint32_t address)
{
    auto it = g_enemies.find(address);
    if (it != g_enemies.end())
    {
        return &it->second;
    }
    return nullptr;
}

void Reset()
{
    g_enemies.clear();
}

// -----------------------------------------------------------------------------
// Archetype Logic Implementations
// -----------------------------------------------------------------------------

namespace
{

void InitEnemy(Re4EnemyState& enemy, uint32_t at, uint8_t kind, const GameLayout& layout)
{
    enemy.address = at;
    enemy.kind = static_cast<EnemyKind>(kind);
    enemy.stateTimer = 0;
    enemy.attackCooldown = 0;
    enemy.specialCooldown = 0;
    enemy.routeType = static_cast<uint8_t>((at / 16) % 11);
    enemy.alert = false;
    enemy.sawRunning = false;
    enemy.totalDamageTaken = 0;
    enemy.staggerTimer = 0;
    enemy.targetRecord = 0;
    enemy.boulderActive = false;
    enemy.lastHp = static_cast<int16_t>(rd16(at + layout.hp));
    enemy.initialized = true;

    // Archetype setup
    switch (enemy.kind)
    {
        case KindChainsaw:
            enemy.weapon = WepChainsaw;
            enemy.state = AiState::ChainsawRev;
            break;
        case KindGigante:
            enemy.weapon = WepBoulder;
            enemy.state = AiState::GiganteWalk;
            break;
        case KindGarrador:
            enemy.weapon = WepClaws;
            enemy.state = AiState::GarradorListen;
            break;
        case KindDebilitas:
            enemy.weapon = WepClub;
            enemy.state = AiState::Approach;
            break;
        case KindHewie:
            enemy.weapon = WepUnarmed;
            enemy.state = AiState::Idle;
            break;
        case KindSledgehammer:
            enemy.weapon = WepSledgehammer;
            enemy.state = AiState::SledgehammerMarch;
            break;
        case KindCorroder:
            enemy.weapon = WepAcidSprayer;
            enemy.state = AiState::Approach;
            break;
        case KindPyramidHead:
            enemy.weapon = WepGreatKnife;
            enemy.state = AiState::PyramidHeadMarch;
            break;
        case KindNurse:
            enemy.weapon = WepRustyPipe;
            enemy.state = AiState::NurseLurch;
            break;
        case KindSae:
            enemy.weapon = WepGhostCurse;
            enemy.state = AiState::SaeFloat;
            break;
        case KindGaki:
            enemy.weapon = WepClaws;
            enemy.state = AiState::GakiSkitter;
            break;
        case KindShibito:
            enemy.weapon = WepRifle;
            enemy.state = AiState::ShibitoAim;
            break;
        case KindZealot:
            enemy.weapon = ((at % 2) == 0) ? WepScythe : WepAxe;
            enemy.state = AiState::Idle;
            break;
        case KindGanado:
        default:
            enemy.weapon = static_cast<WeaponType>(((at / 4) % 3 == 0) ? WepPitchfork : (((at / 4) % 3 == 1) ? WepAxe : WepUnarmed));
            enemy.state = AiState::Idle;
            break;
    }
}

// --- Ganado Villager & Zealot Logic (em10, em14) ---
void TickGanado(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                uint8_t& motion, const GameLayout& layout)
{
    // Perception check
    if (!enemy.alert)
    {
        motion = MotionIdle;
        if (enemy.targetRecord != 0)
        {
            const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
            if (CheckSightCone(pos, facing, targetPos, 1500.0f, 1.0471976f /* 60 deg */, 200.0f))
            {
                enemy.alert = true;
                enemy.state = AiState::Approach;
                enemy.stateTimer = 0;
            }
        }
        return;
    }

    if (enemy.targetRecord == 0)
    {
        enemy.alert = false;
        enemy.state = AiState::Idle;
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const uint16_t targetFacing = rd16(enemy.targetRecord + OffsetFacing);
    const float dist = pos.DistanceXZ(targetPos);

    // Flanking & surround approach
    if (enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        const Vec3 flankGoal = CalculateFlankTarget(targetPos, targetFacing, pos, enemy.routeType, dist);
        facing = TurnTowards(facing, pos, flankGoal, 0x0500); // ~2.8 deg/frame
        pos = AdvancePosition(pos, facing, 2.2f); // Walk speed

        // Attack distance checks
        const float atkRange = (enemy.weapon == WepPitchfork) ? 280.0f : ((enemy.weapon == WepAxe) ? 220.0f : 180.0f);
        if (dist <= atkRange && enemy.attackCooldown == 0)
        {
            if (enemy.weapon == WepPitchfork)
            {
                enemy.state = AiState::AttackPitchfork;
            }
            else if (enemy.weapon == WepAxe || enemy.weapon == WepScythe)
            {
                enemy.state = AiState::AttackAxe;
            }
            else
            {
                enemy.state = AiState::AttackMelee;
            }
            enemy.stateTimer = 0;
        }
        return;
    }

    // Melee attack swings
    if (enemy.state == AiState::AttackMelee)
    {
        motion = MotionAttackMelee;
        facing = TurnTowards(facing, pos, targetPos, 0x0200);

        if (enemy.stateTimer == 15) // Strike frame
        {
            if (CheckAttackHit(pos, facing, targetPos, 180.0f, 1.2f))
            {
                DealDamage(enemy.targetRecord, 180);
            }
        }
        else if (enemy.stateTimer >= 35) // Attack complete
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 40;
        }
        return;
    }

    // Axe / Scythe strikes
    if (enemy.state == AiState::AttackAxe)
    {
        motion = MotionAttackWeapon;
        facing = TurnTowards(facing, pos, targetPos, 0x0200);

        if (enemy.stateTimer == 18) // Strike frame
        {
            EmitSound(SndAxeSwing, pos, at);
            if (CheckAttackHit(pos, facing, targetPos, 220.0f, 1.3f))
            {
                DealDamage(enemy.targetRecord, 320);
            }
        }
        else if (enemy.stateTimer >= 40)
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 45;
        }
        return;
    }

    // Pitchfork thrust
    if (enemy.state == AiState::AttackPitchfork)
    {
        motion = MotionAttackHeavy;
        facing = TurnTowards(facing, pos, targetPos, 0x0180);

        if (enemy.stateTimer == 20) // Lunge strike frame
        {
            EmitSound(SndAxeSwing, pos, at);
            // Longer reach, narrower thrust arc
            if (CheckAttackHit(pos, facing, targetPos, 280.0f, 0.85f))
            {
                DealDamage(enemy.targetRecord, 450);
            }
        }
        else if (enemy.stateTimer >= 45)
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 50;
        }
        return;
    }
}

// --- Chainsaw Man / Dr. Salvador Logic (em10 type 4, em14) ---
void TickChainsaw(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                  uint8_t& motion, const GameLayout& layout)
{
    // Starting & revving chainsaw
    if (enemy.state == AiState::ChainsawRev)
    {
        motion = MotionAttackHeavy;
        if (!enemy.sawRunning)
        {
            EmitSound(SndChainsawRev, pos, at);
            enemy.sawRunning = true;
        }
        if (enemy.stateTimer >= 45)
        {
            enemy.state = AiState::ChainsawAdvance;
            enemy.stateTimer = 0;
        }
        return;
    }

    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    // Slow, ominous advance (Route_type = 0, never backs off)
    if (enemy.state == AiState::ChainsawAdvance)
    {
        motion = MotionWalk;
        facing = TurnTowards(facing, pos, targetPos, 0x0350); // Steady menacing turn
        pos = AdvancePosition(pos, facing, 1.85f); // Deliberate pacing

        // Periodic saw idle revving sound
        if ((enemy.stateTimer % 90) == 0)
        {
            EmitSound(SndChainsawRev, pos, at);
        }

        // Instakill swing trigger
        if (dist <= 220.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::ChainsawInstakill;
            enemy.stateTimer = 0;
            EmitSound(SndChainsawSwing, pos, at);
        }
        return;
    }

    // Instakill chainsaw overhead swing (em10_R1_C_SawCriAtk / plem10_C_SawCriHit)
    if (enemy.state == AiState::ChainsawInstakill)
    {
        motion = MotionAttackHeavy;
        facing = TurnTowards(facing, pos, targetPos, 0x0150);

        if (enemy.stateTimer == 22) // Decapitation strike frame
        {
            if (CheckAttackHit(pos, facing, targetPos, 220.0f, 1.35f))
            {
                EmitSound(SndChainsawDecap, pos, at);
                DealDamage(enemy.targetRecord, 9999, /*lethal=*/true);
            }
        }
        else if (enemy.stateTimer >= 55) // Recovery complete
        {
            enemy.state = AiState::ChainsawAdvance;
            enemy.attackCooldown = 60;
        }
        return;
    }
}

// --- El Gigante Boss Logic (em20, em2b) ---
void TickGigante(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                 uint8_t& motion, const GameLayout& layout)
{
    // Heavy damage stagger (kneeling/stumble after taking >= 800 damage)
    if (enemy.state == AiState::HeavyStagger)
    {
        motion = MotionKnockdown;
        if (enemy.stateTimer >= 100) // Vulnerability window expires
        {
            EmitSound(SndGiganteRoar, pos, at);
            enemy.state = AiState::GiganteWalk;
            enemy.stateTimer = 0;
            enemy.totalDamageTaken = 0;
        }
        return;
    }

    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    // Charge run (em2b_R1_DashAtk)
    if (enemy.state == AiState::GiganteCharge)
    {
        if (enemy.stateTimer < 20) // Wind-up telegraph
        {
            motion = MotionAttackHeavy;
            facing = TurnTowards(facing, pos, targetPos, 0x0400);
            if (enemy.stateTimer <= 1)
            {
                EmitSound(SndGiganteRoar, pos, at);
                enemy.chargeTarget = targetPos;
            }
        }
        else if (enemy.stateTimer < 60) // High-speed rushing charge
        {
            motion = MotionRun;
            pos = AdvancePosition(pos, facing, 7.5f);

            // Bowling over characters in charge path
            for (uint32_t i = 0; i < 4; i++)
            {
                const uint32_t rec = layout.characters + i * layout.characterSize;
                if (rd8(rec + OffsetActive) == 0) continue;
                if (static_cast<int16_t>(rd16(rec + OffsetRoom)) != room) continue;

                const Vec3 charPos = ReadVec3(rec + OffsetPosition);
                if (pos.DistanceXZ(charPos) <= 250.0f)
                {
                    DealDamage(rec, 750);
                }
            }
        }
        else // Braking recovery
        {
            motion = MotionIdle;
            if (enemy.stateTimer >= 80)
            {
                enemy.state = AiState::GiganteWalk;
                enemy.specialCooldown = 180;
            }
        }
        return;
    }

    // Ground slam (em2b_R1_Stamp / Punch)
    if (enemy.state == AiState::GiganteGroundSlam)
    {
        motion = MotionAttackHeavy;
        facing = TurnTowards(facing, pos, targetPos, 0x0150);

        if (enemy.stateTimer == 25) // Ground impact frame
        {
            EmitSound(SndGiganteSlam, pos, at);

            // Shockwave AoE hitting all characters in radius
            for (uint32_t i = 0; i < 4; i++)
            {
                const uint32_t rec = layout.characters + i * layout.characterSize;
                if (rd8(rec + OffsetActive) == 0) continue;
                if (static_cast<int16_t>(rd16(rec + OffsetRoom)) != room) continue;

                const Vec3 charPos = ReadVec3(rec + OffsetPosition);
                if (pos.DistanceXZ(charPos) <= 400.0f)
                {
                    DealDamage(rec, 600);
                }
            }
        }
        else if (enemy.stateTimer >= 55)
        {
            enemy.state = AiState::GiganteWalk;
            enemy.attackCooldown = 120;
        }
        return;
    }

    // Boulder throw (em2b_R1_ThrowRock)
    if (enemy.state == AiState::GiganteBoulderThrow)
    {
        motion = MotionAttackWeapon;
        facing = TurnTowards(facing, pos, targetPos, 0x0150);

        if (!enemy.boulderActive && enemy.stateTimer < 30)
        {
            enemy.boulderActive = true;
            enemy.boulderPos = pos + Vec3(0.0f, 200.0f, 0.0f);
        }
        else if (enemy.stateTimer == 30) // Boulder release
        {
            const Vec3 dir = (targetPos - enemy.boulderPos).Normalized();
            enemy.boulderVel = dir * 25.0f;
            EmitSound(SndGiganteRoar, pos, at);
        }
        else if (enemy.stateTimer > 30 && enemy.boulderActive) // Boulder in flight
        {
            enemy.boulderPos += enemy.boulderVel;

            if (enemy.boulderPos.DistanceXZ(targetPos) <= 180.0f || enemy.stateTimer >= 65)
            {
                EmitSound(SndGiganteSlam, enemy.boulderPos, at);
                // Boulder explosion AoE
                for (uint32_t i = 0; i < 4; i++)
                {
                    const uint32_t rec = layout.characters + i * layout.characterSize;
                    if (rd8(rec + OffsetActive) == 0) continue;
                    if (static_cast<int16_t>(rd16(rec + OffsetRoom)) != room) continue;

                    const Vec3 charPos = ReadVec3(rec + OffsetPosition);
                    if (enemy.boulderPos.DistanceXZ(charPos) <= 300.0f)
                    {
                        DealDamage(rec, 650);
                    }
                }
                enemy.boulderActive = false;
            }
        }
        else if (enemy.stateTimer >= 75)
        {
            enemy.state = AiState::GiganteWalk;
            enemy.specialCooldown = 200;
        }
        return;
    }

    // Walking & Combat Decision
    motion = MotionWalk;
    facing = TurnTowards(facing, pos, targetPos, 0x0300);
    pos = AdvancePosition(pos, facing, 2.5f);

    if (dist > 600.0f && enemy.specialCooldown == 0)
    {
        enemy.state = AiState::GiganteBoulderThrow;
        enemy.stateTimer = 0;
        enemy.boulderActive = true;
        enemy.boulderPos = pos + Vec3(0.0f, 200.0f, 0.0f);
    }
    else if (dist > 300.0f && dist <= 600.0f && enemy.specialCooldown == 0)
    {
        enemy.state = AiState::GiganteCharge;
        enemy.stateTimer = 0;
        enemy.chargeTarget = targetPos;
        EmitSound(SndGiganteRoar, pos, at);
    }
    else if (dist <= 300.0f && enemy.attackCooldown == 0)
    {
        enemy.state = AiState::GiganteGroundSlam;
        enemy.stateTimer = 0;
    }
}

// --- Garrador Logic (em22, em1c) ---
void TickGarrador(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                  uint8_t& motion, const GameLayout& layout)
{
    // Blind hearing: Garrador only detects sound (running, attacking, shouting)
    if (enemy.state == AiState::GarradorListen)
    {
        motion = MotionWalk;
        pos = AdvancePosition(pos, facing, 1.0f); // Cautious stalking pace

        if (enemy.targetRecord != 0 && CheckCharacterSound(enemy.targetRecord, layout))
        {
            enemy.chargeTarget = ReadVec3(enemy.targetRecord + OffsetPosition);
            enemy.state = AiState::GarradorCharge;
            enemy.stateTimer = 0;
            EmitSound(SndGarradorRoar, pos, at);
        }
        return;
    }

    // Frenzied charge towards sound source
    if (enemy.state == AiState::GarradorCharge)
    {
        motion = MotionRun;
        facing = TurnTowards(facing, pos, enemy.chargeTarget, 0x0600);
        pos = AdvancePosition(pos, facing, 5.5f);

        const float distToSound = pos.DistanceXZ(enemy.chargeTarget);
        if (enemy.targetRecord != 0 && pos.DistanceXZ(ReadVec3(enemy.targetRecord + OffsetPosition)) <= 200.0f)
        {
            enemy.state = AiState::GarradorClawSlash;
            enemy.stateTimer = 0;
        }
        else if (distToSound <= 50.0f || enemy.stateTimer >= 80) // Reached destination or wall
        {
            // Claws briefly stuck in wall / recovery
            enemy.state = AiState::GarradorListen;
            enemy.stateTimer = 0;
            enemy.attackCooldown = 40;
        }
        return;
    }

    // Dual-claw frenzy slash
    if (enemy.state == AiState::GarradorClawSlash)
    {
        motion = MotionAttackHeavy;
        if (enemy.targetRecord != 0)
        {
            const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
            facing = TurnTowards(facing, pos, targetPos, 0x0200);

            if (enemy.stateTimer == 15) // Strike frame
            {
                if (CheckAttackHit(pos, facing, targetPos, 200.0f, 1.5f))
                {
                    DealDamage(enemy.targetRecord, 800);
                }
            }
        }

        if (enemy.stateTimer >= 45)
        {
            enemy.state = AiState::GarradorListen;
            enemy.attackCooldown = 60;
        }
        return;
    }
}

// --- Debilitas Logic (Haunting Ground / Demento) ---
void TickDebilitas(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                   uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        facing = TurnTowards(facing, pos, targetPos, 0x0300);
        pos = AdvancePosition(pos, facing, 2.0f);

        if (dist <= 600.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::DebilitasCharge;
            enemy.stateTimer = 0;
            EmitSound(SndDebilitasRoar, pos, at);
        }
        return;
    }

    if (enemy.state == AiState::DebilitasCharge)
    {
        motion = MotionRun;
        facing = TurnTowards(facing, pos, targetPos, 0x0500);
        pos = AdvancePosition(pos, facing, 4.5f);

        if (dist <= 180.0f)
        {
            enemy.state = AiState::DebilitasSlam;
            enemy.stateTimer = 0;
        }
        else if (enemy.stateTimer >= 90)
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 40;
        }
        return;
    }

    if (enemy.state == AiState::DebilitasSlam)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer == 18)
        {
            EmitSound(SndDebilitasSlam, pos, at);
            if (CheckAttackHit(pos, facing, targetPos, 220.0f, 1.4f))
            {
                DealDamage(enemy.targetRecord, 600);
            }
        }
        if (enemy.stateTimer >= 45)
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 50;
        }
        return;
    }
}

// --- Hewie Logic (Haunting Ground Dog Companion) ---
void TickHewie(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
               uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::Idle || enemy.state == AiState::Approach)
    {
        facing = TurnTowards(facing, pos, targetPos, 0x0400);
        if (dist > 300.0f)
        {
            motion = MotionRun;
            pos = AdvancePosition(pos, facing, 4.0f);
        }
        else if (dist > 150.0f)
        {
            motion = MotionWalk;
            pos = AdvancePosition(pos, facing, 2.0f);
        }
        else
        {
            motion = MotionIdle;
            if (enemy.stateTimer % 90 == 0)
            {
                EmitSound(SndHewieBark, pos, at);
            }
        }
        return;
    }

    if (enemy.state == AiState::HewiePounce)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer < 10)
        {
            pos = AdvancePosition(pos, facing, 6.0f);
        }
        if (enemy.stateTimer == 12)
        {
            if (CheckAttackHit(pos, facing, targetPos, 160.0f, 1.2f))
            {
                DealDamage(enemy.targetRecord, 250);
            }
        }
        if (enemy.stateTimer >= 35)
        {
            enemy.state = AiState::Idle;
            enemy.attackCooldown = 40;
        }
        return;
    }
}

// --- Sledgehammer Logic (Clock Tower 3) ---
void TickSledgehammer(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                      uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::SledgehammerMarch || enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        facing = TurnTowards(facing, pos, targetPos, 0x0250);
        pos = AdvancePosition(pos, facing, 1.8f);

        if (dist <= 190.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::SledgehammerSmash;
            enemy.stateTimer = 0;
        }
        return;
    }

    if (enemy.state == AiState::SledgehammerSmash)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer == 22) // Hammer overhead drop impact
        {
            EmitSound(SndSledgehammerSmash, pos, at);
            if (CheckAttackHit(pos, facing, targetPos, 220.0f, 1.6f))
            {
                DealDamage(enemy.targetRecord, 900);
            }
        }
        if (enemy.stateTimer >= 55)
        {
            enemy.state = AiState::SledgehammerMarch;
            enemy.attackCooldown = 50;
        }
        return;
    }
}

// --- Corroder Logic (Clock Tower 3) ---
void TickCorroder(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                  uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::Approach)
    {
        motion = MotionRun;
        facing = TurnTowards(facing, pos, targetPos, 0x0500);
        pos = AdvancePosition(pos, facing, 4.2f);

        if (dist <= 300.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::CorroderAcidSpray;
            enemy.stateTimer = 0;
            EmitSound(SndAcidSpray, pos, at);
        }
        return;
    }

    if (enemy.state == AiState::CorroderAcidSpray)
    {
        motion = MotionAttackWeapon;
        facing = TurnTowards(facing, pos, targetPos, 0x0150);

        if (enemy.stateTimer >= 10 && enemy.stateTimer <= 35 && (enemy.stateTimer % 5 == 0))
        {
            if (CheckAttackHit(pos, facing, targetPos, 350.0f, 1.2f))
            {
                DealDamage(enemy.targetRecord, 120);
            }
        }

        if (enemy.stateTimer >= 50)
        {
            enemy.state = AiState::Approach;
            enemy.attackCooldown = 60;
        }
        return;
    }
}

// --- Pyramid Head Logic (Silent Hill 2) ---
void TickPyramidHead(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                     uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::PyramidHeadMarch || enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        facing = TurnTowards(facing, pos, targetPos, 0x0180);
        pos = AdvancePosition(pos, facing, 1.2f); // Slow unstoppable advance

        if (enemy.stateTimer % 40 == 0)
        {
            EmitSound(SndPyramidKnifeDrag, pos, at);
        }

        if (dist <= 220.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::PyramidHeadExecution;
            enemy.stateTimer = 0;
            EmitSound(SndPyramidExecution, pos, at);
        }
        return;
    }

    if (enemy.state == AiState::PyramidHeadExecution)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer == 28) // Great Knife execution swing
        {
            if (CheckAttackHit(pos, facing, targetPos, 260.0f, 2.2f))
            {
                DealDamage(enemy.targetRecord, 1500, /*lethal=*/true);
            }
        }
        if (enemy.stateTimer >= 65)
        {
            enemy.state = AiState::PyramidHeadMarch;
            enemy.attackCooldown = 70;
        }
        return;
    }
}

// --- Bubble Head Nurse Logic (Silent Hill 2) ---
void TickNurse(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
               uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::NurseLurch || enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        // Jerky erratic twitching angle
        const uint16_t twitch = static_cast<uint16_t>((std::sin(enemy.stateTimer * 0.4f) * 2000.0f));
        facing = TurnTowards(facing + twitch, pos, targetPos, 0x0400);
        pos = AdvancePosition(pos, facing, (enemy.stateTimer % 20 < 10) ? 3.0f : 0.8f);

        if (dist <= 150.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::NurseFlurry;
            enemy.stateTimer = 0;
        }
        return;
    }

    if (enemy.state == AiState::NurseFlurry)
    {
        motion = MotionAttackMelee;
        if (enemy.stateTimer == 12 || enemy.stateTimer == 22)
        {
            if (CheckAttackHit(pos, facing, targetPos, 160.0f, 1.4f))
            {
                DealDamage(enemy.targetRecord, 250);
            }
        }
        if (enemy.stateTimer >= 36)
        {
            enemy.state = AiState::NurseLurch;
            enemy.attackCooldown = 35;
        }
        return;
    }
}

// --- Sae Kurosawa Logic (Fatal Frame II) ---
void TickSae(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
             uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::SaeFloat || enemy.state == AiState::Approach)
    {
        motion = MotionWalk;
        facing = TurnTowards(facing, pos, targetPos, 0x0300);
        pos = AdvancePosition(pos, facing, 2.2f);

        if (dist > 500.0f && enemy.specialCooldown == 0)
        {
            enemy.state = AiState::SaeTeleport;
            enemy.stateTimer = 0;
            return;
        }

        if (dist <= 160.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::SaeChoke;
            enemy.stateTimer = 0;
            EmitSound(SndGhostLaugh, pos, at);
        }
        return;
    }

    if (enemy.state == AiState::SaeTeleport)
    {
        motion = MotionIdle;
        if (enemy.stateTimer == 15) // Reappear behind player
        {
            const uint16_t targetFacing = rd16(enemy.targetRecord + OffsetFacing);
            pos = AdvancePosition(targetPos, static_cast<uint16_t>(targetFacing + 0x8000), 120.0f);
            facing = targetFacing;
            EmitSound(SndGhostLaugh, pos, at);
        }
        if (enemy.stateTimer >= 30)
        {
            enemy.state = AiState::SaeFloat;
            enemy.specialCooldown = 120;
        }
        return;
    }

    if (enemy.state == AiState::SaeChoke)
    {
        motion = MotionAttackMelee;
        if (enemy.stateTimer >= 10 && (enemy.stateTimer % 6 == 0))
        {
            DealDamage(enemy.targetRecord, 100);
        }
        if (enemy.stateTimer >= 40)
        {
            enemy.state = AiState::SaeFloat;
            enemy.attackCooldown = 50;
        }
        return;
    }
}

// --- Twin Gaki Logic (Kuon) ---
void TickGaki(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
              uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::GakiSkitter || enemy.state == AiState::Approach)
    {
        motion = MotionRun;
        facing = TurnTowards(facing, pos, targetPos, 0x0600);
        pos = AdvancePosition(pos, facing, 4.8f); // Fast skittering crawl

        if (dist <= 240.0f && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::GakiLeap;
            enemy.stateTimer = 0;
        }
        return;
    }

    if (enemy.state == AiState::GakiLeap)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer < 12)
        {
            pos = AdvancePosition(pos, facing, 6.0f);
        }
        if (enemy.stateTimer == 14)
        {
            if (CheckAttackHit(pos, facing, targetPos, 180.0f, 1.4f))
            {
                DealDamage(enemy.targetRecord, 380);
            }
        }
        if (enemy.stateTimer >= 32)
        {
            enemy.state = AiState::GakiSkitter;
            enemy.attackCooldown = 40;
        }
        return;
    }
}

// --- Shibito Sniper Logic (Forbidden Siren) ---
void TickShibito(Re4EnemyState& enemy, uint32_t at, Vec3& pos, uint16_t& facing, int16_t room,
                 uint8_t& motion, const GameLayout& layout)
{
    if (enemy.targetRecord == 0)
    {
        motion = MotionIdle;
        return;
    }

    const Vec3 targetPos = ReadVec3(enemy.targetRecord + OffsetPosition);
    const float dist = pos.DistanceXZ(targetPos);

    if (enemy.state == AiState::ShibitoAim || enemy.state == AiState::Idle)
    {
        motion = MotionAttackWeapon;
        facing = TurnTowards(facing, pos, targetPos, 0x0300);

        if (enemy.stateTimer == 1)
        {
            EmitSound(SndShibitoCry, pos, at);
        }

        // Aiming delay before firing
        if (enemy.stateTimer >= 35 && enemy.attackCooldown == 0)
        {
            enemy.state = AiState::ShibitoFire;
            enemy.stateTimer = 0;
        }
        return;
    }

    if (enemy.state == AiState::ShibitoFire)
    {
        motion = MotionAttackHeavy;
        if (enemy.stateTimer == 5)
        {
            EmitSound(SndRifleFire, pos, at);
            // Long range raycast hit
            if (dist <= 2500.0f && std::abs(ComputeAngleDiff(facing, pos, targetPos)) <= 0.35f)
            {
                DealDamage(enemy.targetRecord, 750);
            }
        }

        if (enemy.stateTimer >= 25)
        {
            enemy.state = AiState::ShibitoAim;
            enemy.attackCooldown = 60;
        }
        return;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Engine Dispatch Integration
// -----------------------------------------------------------------------------

void TickEnemy(Ps2Cpu* c)
{
    if (c == nullptr) return;

    const uint32_t at = c->r[4].uw[0];
    if (at == 0 || rd8(at + OffsetActive) == 0) return;

    const uint8_t kind = rd8(at + OffsetKind);
    if (!IsRe4Enemy(kind)) return;

    const GameLayout layout = GetLayout(at);
    Re4EnemyState& enemy = g_enemies[at];

    if (!enemy.initialized)
    {
        InitEnemy(enemy, at, kind, layout);
    }

    // Read current state from Outbreak memory
    Vec3 pos = ReadVec3(at + OffsetPosition);
    uint16_t facing = rd16(at + OffsetFacing);
    const int16_t room = static_cast<int16_t>(rd16(at + OffsetRoom));
    uint8_t motion = rd8(at + layout.motion);
    int16_t currentHp = static_cast<int16_t>(rd16(at + layout.hp));

    // Handle damage reaction & hit staggering
    const uint16_t dmgTaken = rd16(at + OffsetDamageTaken);
    const uint8_t hitFlag = rd8(at + OffsetHitFlag);
    const bool tookDamage = (dmgTaken > 0 || hitFlag != 0 || currentHp < enemy.lastHp);

    if (tookDamage)
    {
        int delta = (dmgTaken > 0) ? dmgTaken : (enemy.lastHp - currentHp);
        if (delta <= 0) delta = 50;

        if (currentHp == enemy.lastHp)
        {
            currentHp = static_cast<int16_t>(std::max(0, currentHp - delta));
            wr16(at + layout.hp, static_cast<uint16_t>(currentHp));
        }

        wr16(at + OffsetDamageTaken, 0);
        wr8(at + OffsetHitFlag, 0);

        enemy.totalDamageTaken += delta;
        enemy.lastHp = currentHp;

        if (currentHp <= 0)
        {
            enemy.state = AiState::Death;
            enemy.stateTimer = 0;
        }
        else if (enemy.kind == KindGigante && enemy.totalDamageTaken >= 800)
        {
            enemy.state = AiState::HeavyStagger;
            enemy.stateTimer = 0;
        }
        else if (enemy.kind != KindGigante && (enemy.kind != KindChainsaw || delta >= 150))
        {
            // Enter hit stagger reaction
            enemy.state = AiState::Stagger;
            enemy.stateTimer = 0;
            enemy.staggerTimer = 25;
        }
    }

    enemy.lastHp = currentHp;

    // Handle Death
    if (enemy.state == AiState::Death)
    {
        motion = MotionDeath;
        enemy.stateTimer++;
        if (enemy.stateTimer >= 40) // Death collapse complete
        {
            wr8(at + OffsetActive, 0);
            enemy.state = AiState::Dead;
        }
        wr8(at + layout.motion, motion);
        return;
    }

    if (enemy.state == AiState::Dead)
    {
        return;
    }

    // Hit staggering update
    if (enemy.state == AiState::Stagger)
    {
        motion = MotionFlinch;
        // Small recoil push back
        if (enemy.stateTimer < 5)
        {
            pos = AdvancePosition(pos, static_cast<uint16_t>(facing + 0x8000), 1.2f);
        }

        enemy.stateTimer++;
        if (enemy.stateTimer >= static_cast<uint32_t>(enemy.staggerTimer))
        {
            enemy.state = (enemy.kind == KindChainsaw) ? AiState::ChainsawAdvance : AiState::Approach;
            enemy.stateTimer = 0;
        }

        WriteVec3(at + OffsetPosition, pos);
        wr16(at + OffsetFacing, facing);
        wr8(at + layout.motion, motion);
        return;
    }

    // Decrement cooldowns
    if (enemy.attackCooldown > 0) enemy.attackCooldown--;
    if (enemy.specialCooldown > 0) enemy.specialCooldown--;

    // Target acquisition
    enemy.targetRecord = FindNearestCharacter(pos, room, layout);
    wr32(at + OffsetTarget, enemy.targetRecord);

    // Dispatch archetype behavior
    switch (enemy.kind)
    {
        case KindGanado:
        case KindZealot:
            TickGanado(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindChainsaw:
            TickChainsaw(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindGigante:
            TickGigante(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindGarrador:
            TickGarrador(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindDebilitas:
            TickDebilitas(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindHewie:
            TickHewie(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindSledgehammer:
            TickSledgehammer(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindCorroder:
            TickCorroder(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindPyramidHead:
            TickPyramidHead(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindNurse:
            TickNurse(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindSae:
            TickSae(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindGaki:
            TickGaki(enemy, at, pos, facing, room, motion, layout);
            break;
        case KindShibito:
            TickShibito(enemy, at, pos, facing, room, motion, layout);
            break;
    }

    enemy.stateTimer++;

    // Write back updated position, facing, motion to Outbreak guest memory
    WriteVec3(at + OffsetPosition, pos);
    wr16(at + OffsetFacing, facing);
    wr8(at + layout.motion, motion);
}

} // namespace Re4
