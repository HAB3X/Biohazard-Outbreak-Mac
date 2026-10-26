// Unit tests for the RE4 enemy AI module (src/re4/re4_ai.h).
// Tests perception, surround movement, attack strikes, Chainsaw Man instakill,
// El Gigante boss mechanics, Garrador hearing, and MIPS dispatch integration.
#include "re4/re4_ai.h"
#include "ps2/recomp.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Re4;

// Mock PS2 guest memory for headless testing
uint8_t g_mockMemory[32 * 1024 * 1024];
uint8_t* ps2_mem = g_mockMemory;

extern "C" {
u8 ps2_io_read8(u32) { return 0; }
u16 ps2_io_read16(u32) { return 0; }
u32 ps2_io_read32(u32) { return 0; }
u64 ps2_io_read64(u32) { return 0; }
u128 ps2_io_read128(u32) { return 0; }
void ps2_io_write8(u32, u8) {}
void ps2_io_write16(u32, u16) {}
void ps2_io_write32(u32, u32) {}
void ps2_io_write64(u32, u64) {}
void ps2_io_write128(u32, u128) {}
}

namespace
{

int g_failures = 0;

#define CHECK(condition)                                                                       \
    do                                                                                         \
    {                                                                                          \
        if (!(condition))                                                                      \
        {                                                                                      \
            std::printf("%s:%d: CHECK FAILED: %s\n", __FILE__, __LINE__, #condition);         \
            g_failures++;                                                                      \
        }                                                                                      \
    } while (0)

struct SoundEvent
{
    int soundId;
    Vec3 pos;
    uint32_t enemyAddr;
};

std::vector<SoundEvent> g_soundEvents;

void TestSoundCallback(int soundId, const Vec3& pos, uint32_t enemyAddr)
{
    g_soundEvents.push_back({soundId, pos, enemyAddr});
}

constexpr uint32_t PlayerRecord = 0x476DD0;
constexpr uint32_t EnemyRecord = 0x3FFCA0;

void SetupMockMemory()
{
    std::memset(g_mockMemory, 0, sizeof(g_mockMemory));
    g_soundEvents.clear();
    Re4::Reset();
    Re4::SetSoundCallback(TestSoundCallback);

    // Setup local player at PlayerRecord (File 1 layout)
    // Offset 0x00: Active = 1
    g_mockMemory[PlayerRecord + 0] = 1;
    // Offset 0x03: Id = 0 (Player)
    g_mockMemory[PlayerRecord + 3] = 0;
    // Offset 0x38: Position = (1000, 0, 1000)
    float px = 1000.0f, py = 0.0f, pz = 1000.0f;
    std::memcpy(g_mockMemory + PlayerRecord + 0x38, &px, 4);
    std::memcpy(g_mockMemory + PlayerRecord + 0x3C, &py, 4);
    std::memcpy(g_mockMemory + PlayerRecord + 0x40, &pz, 4);
    // Offset 0x92: Facing = 0 (facing +z)
    uint16_t pfacing = 0;
    std::memcpy(g_mockMemory + PlayerRecord + 0x92, &pfacing, 2);
    // Offset 0x1E2: Room = 1
    int16_t proom = 1;
    std::memcpy(g_mockMemory + PlayerRecord + 0x1E2, &proom, 2);
    // Offset 0x544: HP = 2000, Offset 0x546: MaxHP = 2000
    int16_t php = 2000;
    std::memcpy(g_mockMemory + PlayerRecord + 0x544, &php, 2);
    std::memcpy(g_mockMemory + PlayerRecord + 0x546, &php, 2);
    // Offset 0x54F: Motion = 0
    g_mockMemory[PlayerRecord + 0x54F] = 0;
}

void SetupEnemy(uint32_t addr, uint8_t kind, float x, float y, float z, uint16_t facing, int16_t hp)
{
    // Offset 0x00: Active = 1
    g_mockMemory[addr + 0] = 1;
    // Offset 0x03: Kind
    g_mockMemory[addr + 3] = kind;
    // Offset 0x38: Position = (x, y, z)
    std::memcpy(g_mockMemory + addr + 0x38, &x, 4);
    std::memcpy(g_mockMemory + addr + 0x3C, &y, 4);
    std::memcpy(g_mockMemory + addr + 0x40, &z, 4);
    // Offset 0x92: Facing
    std::memcpy(g_mockMemory + addr + 0x92, &facing, 2);
    // Offset 0x1E2: Room = 1
    int16_t room = 1;
    std::memcpy(g_mockMemory + addr + 0x1E2, &room, 2);
    // Offset 0x544: HP
    std::memcpy(g_mockMemory + addr + 0x544, &hp, 2);
    std::memcpy(g_mockMemory + addr + 0x546, &hp, 2);
    // Offset 0x54F: Motion = 0
    g_mockMemory[addr + 0x54F] = 0;
}

// -----------------------------------------------------------------------------
// Test Cases
// -----------------------------------------------------------------------------

void TestEnemyKinds()
{
    // Outbreak native kinds (< 100) are not RE4
    CHECK(!IsRe4Enemy(0));
    CHECK(!IsRe4Enemy(1));  // Zombie
    CHECK(!IsRe4Enemy(14)); // Licker
    CHECK(!IsRe4Enemy(30)); // Tyrant
    CHECK(!IsRe4Enemy(65));

    // RE4 kinds (>= 100)
    CHECK(IsRe4Enemy(KindGanado));     // 100
    CHECK(IsRe4Enemy(KindZealot));     // 101
    CHECK(IsRe4Enemy(KindChainsaw));   // 104
    CHECK(IsRe4Enemy(KindGigante));    // 120
    CHECK(IsRe4Enemy(KindGarrador));   // 122
}

void TestPerceptionSightCone()
{
    const Vec3 enemyPos{0.0f, 0.0f, 0.0f};
    const uint16_t facingNorth = 0; // facing +z (0 rad)

    // Target straight ahead within 15m (1000 units)
    CHECK(CheckSightCone(enemyPos, facingNorth, {0.0f, 0.0f, 1000.0f}));

    // Target too far (> 1500 units)
    CHECK(!CheckSightCone(enemyPos, facingNorth, {0.0f, 0.0f, 1800.0f}));

    // Target inside cone (45 deg = 0.785 rad, within 60 deg cone)
    CHECK(CheckSightCone(enemyPos, facingNorth, {500.0f, 0.0f, 500.0f}));

    // Target outside cone (75 deg, beyond 60 deg cone)
    CHECK(!CheckSightCone(enemyPos, facingNorth, {1000.0f, 0.0f, 260.0f}));

    // Target behind (180 deg) at far distance -> not detected
    CHECK(!CheckSightCone(enemyPos, facingNorth, {0.0f, 0.0f, -600.0f}));

    // Target behind at close proximity (200 units <= 350) -> detected!
    CHECK(CheckSightCone(enemyPos, facingNorth, {0.0f, 0.0f, -200.0f}));

    // Vertical height limit exceeded (> 200 units difference)
    CHECK(!CheckSightCone(enemyPos, facingNorth, {0.0f, 250.0f, 1000.0f}));
}

void TestFlankSurroundMovement()
{
    const Vec3 targetPos{1000.0f, 0.0f, 1000.0f};
    const uint16_t targetFacing = 0;
    const Vec3 enemyPos{1000.0f, 0.0f, 500.0f}; // 500 units south of target
    const uint16_t facingNorth = 0;

    // Route 0: direct advance toward target
    const Vec3 flank0 = CalculateFlankTarget(targetPos, targetFacing, enemyPos, 0, 500.0f);
    CHECK(std::abs(flank0.x - targetPos.x) < 1.0f);
    CHECK(std::abs(flank0.z - targetPos.z) < 1.0f);

    // Route 1: right flank offset (positive lateral)
    const Vec3 flank1 = CalculateFlankTarget(targetPos, targetFacing, enemyPos, 1, 500.0f);
    CHECK(flank1.x > targetPos.x + 50.0f);

    // Route 2: left flank offset (negative lateral)
    const Vec3 flank2 = CalculateFlankTarget(targetPos, targetFacing, enemyPos, 2, 500.0f);
    CHECK(flank2.x < targetPos.x - 50.0f);

    // AdvancePosition moves forward in facing direction
    const Vec3 advanced = AdvancePosition({0.0f, 0.0f, 0.0f}, facingNorth, 10.0f);
    CHECK(std::abs(advanced.x) < 0.01f);
    CHECK(std::abs(advanced.z - 10.0f) < 0.01f);
}

void TestAttackHitDetection()
{
    const Vec3 enemyPos{0.0f, 0.0f, 0.0f};
    const uint16_t facingNorth = 0;

    // Target within 180 units and within 1.0 rad front arc -> hit!
    CHECK(CheckAttackHit(enemyPos, facingNorth, {0.0f, 0.0f, 150.0f}, 180.0f, 1.0f));

    // Target out of range (220 > 180) -> miss
    CHECK(!CheckAttackHit(enemyPos, facingNorth, {0.0f, 0.0f, 220.0f}, 180.0f, 1.0f));

    // Target behind enemy -> miss
    CHECK(!CheckAttackHit(enemyPos, facingNorth, {0.0f, 0.0f, -100.0f}, 180.0f, 1.0f));
}

void TestGanadoBehavior()
{
    SetupMockMemory();
    // Place Ganado at (1000, 0, 400), facing North toward player at (1000, 0, 1000)
    SetupEnemy(EnemyRecord, KindGanado, 1000.0f, 0.0f, 400.0f, 0, 700);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    // Frame 1: Tick should detect player, alert Ganado, switch to Approach
    TickEnemy(&c);
    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);
    CHECK(state->alert);
    CHECK(state->state == AiState::Approach);

    // Run approach ticks: Ganado should advance toward player
    const float initialZ = 400.0f;
    for (int i = 0; i < 30; i++)
    {
        TickEnemy(&c);
    }

    float currentZ;
    std::memcpy(&currentZ, g_mockMemory + EnemyRecord + 0x38 + 8, 4);
    CHECK(currentZ > initialZ); // Position advanced north toward player

    // Test damage reaction & hit stagger
    int16_t playerHpBefore = 2000;
    std::memcpy(&playerHpBefore, g_mockMemory + PlayerRecord + 0x544, 2);

    // Inflict 120 damage on the Ganado via Outbreak DamageTaken offset
    uint16_t dmg = 120;
    std::memcpy(g_mockMemory + EnemyRecord + 0x138, &dmg, 2);
    g_mockMemory[EnemyRecord + 0x13F] = 1;

    TickEnemy(&c);
    CHECK(state->state == AiState::Stagger);
    CHECK(state->totalDamageTaken >= 120);

    // Verify DamageTaken was cleared
    uint16_t clearedDmg;
    std::memcpy(&clearedDmg, g_mockMemory + EnemyRecord + 0x138, 2);
    CHECK(clearedDmg == 0);

    // Test Death
    uint16_t lethalDmg = 9999;
    std::memcpy(g_mockMemory + EnemyRecord + 0x138, &lethalDmg, 2);
    TickEnemy(&c);
    CHECK(state->state == AiState::Death);

    // Advance frames to complete death collapse
    for (int i = 0; i < 45; i++)
    {
        TickEnemy(&c);
    }
    CHECK(state->state == AiState::Dead);
    CHECK(g_mockMemory[EnemyRecord + 0] == 0); // Enemy record marked inactive
}

void TestChainsawManInstakill()
{
    SetupMockMemory();
    // Place Chainsaw Man at (1000, 0, 850), 150 units from player at (1000, 0, 1000)
    SetupEnemy(EnemyRecord, KindChainsaw, 1000.0f, 0.0f, 850.0f, 0, 2500);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    // Tick 1: Starts in ChainsawRev
    TickEnemy(&c);
    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);
    CHECK(state->state == AiState::ChainsawRev);
    CHECK(state->sawRunning);

    // Check rev sound was emitted
    bool revSoundFound = false;
    for (const auto& ev : g_soundEvents)
    {
        if (ev.soundId == SndChainsawRev) revSoundFound = true;
    }
    CHECK(revSoundFound);

    // Advance past rev warmup (45 frames) to reach ChainsawAdvance
    for (int i = 0; i < 50; i++)
    {
        TickEnemy(&c);
    }
    CHECK(state->state == AiState::ChainsawAdvance || state->state == AiState::ChainsawInstakill);

    // Run until instakill swing triggers and completes
    for (int i = 0; i < 40; i++)
    {
        TickEnemy(&c);
    }

    // Player HP should now be 0 (instant kill decapitation!)
    int16_t playerHp = -1;
    std::memcpy(&playerHp, g_mockMemory + PlayerRecord + 0x544, 2);
    CHECK(playerHp == 0);

    // Check decapitation sound was emitted
    bool decapSoundFound = false;
    for (const auto& ev : g_soundEvents)
    {
        if (ev.soundId == SndChainsawDecap) decapSoundFound = true;
    }
    CHECK(decapSoundFound);
}

void TestElGiganteBoss()
{
    SetupMockMemory();
    // Place El Gigante at (1000, 0, 200), 800 units south of player at (1000, 0, 1000)
    SetupEnemy(EnemyRecord, KindGigante, 1000.0f, 0.0f, 200.0f, 0, 6000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    // Frame 1: Gigante is at long distance (800 units > 600), so chooses BoulderThrow
    TickEnemy(&c);
    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);
    CHECK(state->state == AiState::GiganteBoulderThrow);

    // Advance boulder throw animation until projectile launches and hits
    for (int i = 0; i < 80; i++)
    {
        TickEnemy(&c);
    }

    // Player should have taken damage from boulder explosion
    int16_t playerHp;
    std::memcpy(&playerHp, g_mockMemory + PlayerRecord + 0x544, 2);
    CHECK(playerHp < 2000);

    // Move Gigante to medium range (450 units) and reset special cooldown
    state->state = AiState::GiganteWalk;
    state->stateTimer = 0;
    state->specialCooldown = 0;
    float newZ = 550.0f;
    std::memcpy(g_mockMemory + EnemyRecord + 0x38 + 8, &newZ, 4);

    // Should choose charge run
    TickEnemy(&c);
    CHECK(state->state == AiState::GiganteCharge);

    // Test Ground Slam at close range (200 units)
    state->attackCooldown = 0;
    state->state = AiState::GiganteWalk;
    newZ = 800.0f;
    std::memcpy(g_mockMemory + EnemyRecord + 0x38 + 8, &newZ, 4);

    TickEnemy(&c);
    CHECK(state->state == AiState::GiganteGroundSlam);

    // Advance ground slam to impact frame
    for (int i = 0; i < 30; i++)
    {
        TickEnemy(&c);
    }

    bool slamSoundFound = false;
    for (const auto& ev : g_soundEvents)
    {
        if (ev.soundId == SndGiganteSlam) slamSoundFound = true;
    }
    CHECK(slamSoundFound);

    // Test Heavy Stagger: inflict 850 cumulative damage
    uint16_t heavyDmg = 850;
    std::memcpy(g_mockMemory + EnemyRecord + 0x138, &heavyDmg, 2);
    g_mockMemory[EnemyRecord + 0x13F] = 1;

    TickEnemy(&c);
    CHECK(state->state == AiState::HeavyStagger);
}

void TestGarradorBlindHearing()
{
    SetupMockMemory();
    // Place Garrador at (1000, 0, 500), player at (1000, 0, 1000)
    SetupEnemy(EnemyRecord, KindGarrador, 1000.0f, 0.0f, 500.0f, 0, 2000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    // Player is standing still (motion = 0, no sound)
    g_mockMemory[PlayerRecord + 0x54F] = 0;

    TickEnemy(&c);
    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);
    // Garrador is blind, so he stays listening/wandering
    CHECK(state->state == AiState::GarradorListen);

    // Now player makes sound (runs: motion = MotionRun = 2)
    g_mockMemory[PlayerRecord + 0x54F] = 2;

    TickEnemy(&c);
    // Garrador hears the sound and charges toward the source!
    CHECK(state->state == AiState::GarradorCharge);

    bool garradorSoundFound = false;
    for (const auto& ev : g_soundEvents)
    {
        if (ev.soundId == SndGarradorRoar) garradorSoundFound = true;
    }
    CHECK(garradorSoundFound);
}

void TestDebilitasChargeAndSlam()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindDebilitas, 1000.0f, 0.0f, 850.0f, 0, 3000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    int16_t initialPlayerHp = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);

    // Run ticks until Debilitas performs slam
    for (int i = 0; i < 25; i++)
    {
        TickEnemy(&c);
    }

    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);

    int16_t damagedPlayerHp = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(damagedPlayerHp < initialPlayerHp);
}

void TestSledgehammerMarchAndSmash()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindSledgehammer, 1000.0f, 0.0f, 850.0f, 0, 4000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    int16_t hpBefore = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);

    for (int i = 0; i < 30; i++)
    {
        TickEnemy(&c);
    }

    int16_t hpAfter = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(hpAfter <= hpBefore - 900);
}

void TestCorroderAcidSpray()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindCorroder, 1000.0f, 0.0f, 800.0f, 0, 2500);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    int16_t hpBefore = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);

    for (int i = 0; i < 20; i++)
    {
        TickEnemy(&c);
    }

    int16_t hpAfter = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(hpAfter < hpBefore);
}

void TestPyramidHeadExecution()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindPyramidHead, 1000.0f, 0.0f, 850.0f, 0, 5000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    for (int i = 0; i < 35; i++)
    {
        TickEnemy(&c);
    }

    int16_t hpAfter = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(hpAfter <= 500);
}

void TestSaeTeleportAndChoke()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindSae, 1000.0f, 0.0f, 100.0f, 0, 2000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    TickEnemy(&c);
    Re4EnemyState* state = GetEnemyState(EnemyRecord);
    CHECK(state != nullptr);
    CHECK(state->state == AiState::SaeTeleport);

    for (int i = 0; i < 20; i++)
    {
        TickEnemy(&c);
    }

    float ez = *reinterpret_cast<float*>(g_mockMemory + EnemyRecord + 0x40);
    CHECK(std::abs(ez - 880.0f) < 50.0f);
}

void TestGakiSkitterAndLeap()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindGaki, 1000.0f, 0.0f, 850.0f, 0, 1000);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    int16_t hpBefore = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);

    for (int i = 0; i < 20; i++)
    {
        TickEnemy(&c);
    }

    int16_t hpAfter = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(hpAfter < hpBefore);
}

void TestShibitoSniper()
{
    SetupMockMemory();
    SetupEnemy(EnemyRecord, KindShibito, 1000.0f, 0.0f, 200.0f, 0, 1200);

    Ps2Cpu c{};
    c.r[4].uw[0] = EnemyRecord;

    int16_t hpBefore = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);

    for (int i = 0; i < 45; i++)
    {
        TickEnemy(&c);
    }

    int16_t hpAfter = *reinterpret_cast<int16_t*>(g_mockMemory + PlayerRecord + 0x544);
    CHECK(hpAfter <= hpBefore - 750);
}

} // namespace

int main()
{
    std::printf("Running RE4 enemy AI unit tests...\n");

    TestEnemyKinds();
    TestPerceptionSightCone();
    TestFlankSurroundMovement();
    TestAttackHitDetection();
    TestGanadoBehavior();
    TestChainsawManInstakill();
    TestElGiganteBoss();
    TestGarradorBlindHearing();
    TestDebilitasChargeAndSlam();
    TestSledgehammerMarchAndSmash();
    TestCorroderAcidSpray();
    TestPyramidHeadExecution();
    TestSaeTeleportAndChoke();
    TestGakiSkitterAndLeap();
    TestShibitoSniper();

    if (g_failures == 0)
    {
        std::printf("All RE4 enemy AI tests passed successfully!\n");
        return 0;
    }
    else
    {
        std::printf("%d test check(s) failed!\n", g_failures);
        return 1;
    }
}
