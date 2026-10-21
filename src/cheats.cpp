// The cheats (cheats.h): the merged list, the data cheats written each game frame, and the overrides that stand for the code
// patches.
//
// Adapted from REO-Cheats and REO-GreatestHits-Cheats by RaccoonRecomp (MIT): their decoding of each code, its fixes for the
// disc, its rules (only the bits the game itself sets in the save, jokers on the exact buttons, the first of several cheats on
// one value wins). The codes are by Codejunkies, Code Master, Lajos Szalay, Jay007, VirusPunk, Jarnold83, MadCatz, bungholio
// and 47iscool.
#include "cheats.h"

#include "game_pad.h"
#include "mod_settings.h"

#include <array>
#include <bit>
#include <cstdio>
#include <string>
#include <vector>

namespace Cheats
{
// File 1 (SLPM-65428): each address found from the US disc's by the code that uses it (docs/mods.md)
const Layout File1 = {.name = "file1",
                      .playMode = 0x3065C2,
                      .characters = 0x476DD0,
                      .characterSize = 0x10E0,
                      .items = 0x3967D0,
                      .padFrames = 0x258698,
                      .gaugeMaxima = 0x6E6C90,
                      .results = 0x389654,
                      .files = 0x48AE10,
                      .save = 0x301D60,
                      .scenarios = 0x305DD8,
                      .available = 0x305DF4,
                      .owned = 0x305E54,
                      .deathOfflineReturn = 0x62E374,
                      .enemyMoveReturn = 0x6644B8,
                      .field = {.hp = 0x544,
                                .hpMax = 0x546,
                                .speed0 = 0x0F0,
                                .scale = 0x0B4,
                                .speed1 = 0xBA8,
                                .speed2 = 0xBC0,
                                .damage = 0xBC4,
                                .characterType = 0xB98,
                                .animation = 0xB9B,
                                .status = 0xBA0,
                                .unnoticed = 0xBA1,
                                .untouchable = 0xBA2,
                                .rapidFire = 0xBA3,
                                .gauge = 0xBAC,
                                .gauge2 = 0xBB8,
                                .model = 0xBB4,
                                .altCostume = 0xBBC,
                                .wallsA = 0xCB0,
                                .wallsB = 0xCCC}};

// File 2 (SLPM-65692): each function and field matched to File 1's by the code that uses it (docs/mods.md). Its save data,
// results and scenario select are laid out otherwise (their cheats aren't offered), and so are the flags word's bits
const Layout File2 = {.name = "file2",
                      .playMode = 0x3137D2,
                      .characters = 0x47BD30,
                      .characterSize = 0x1100,
                      .items = 0x397B40,
                      .padFrames = 0x267918,
                      .gaugeMaxima = 0x738520,
                      .results = 0,
                      .files = 0,
                      .save = 0,
                      .scenarios = 0,
                      .available = 0,
                      .owned = 0,
                      .deathOfflineReturn = 0x6335B8,
                      .enemyMoveReturn = 0x674EB0,
                      .field = {.hp = 0x540,
                                .hpMax = 0x542,
                                .speed0 = 0x0F0,
                                .scale = 0x0B4,
                                .speed1 = 0xBC0,
                                .speed2 = 0xBD8,
                                .damage = 0xBDC,
                                .characterType = 0xBB0,
                                .animation = 0xBB2,
                                .status = 0,
                                .unnoticed = 0,
                                .untouchable = 0,
                                .rapidFire = 0,
                                .gauge = 0xBC4,
                                .gauge2 = 0xBCC,
                                .model = 0xBC8,
                                .altCostume = 0,
                                .wallsA = 0xCC8,
                                .wallsB = 0xCE4}};

namespace
{
const Layout* g_layout = nullptr;
const CharacterFields& F() { return g_layout->field; }

// An item record's count (s16)
constexpr uint32_t ItemSize = 60, ItemCount = 0x24;
// The results' fields after the result points
constexpr uint32_t ResultDamaged = 0x40, ResultWeapon = 0x41, ResultSurvivors = 0x42, ResultEvents1 = 0x44,
                   ResultEvents2 = 0x48, ResultBestPoints = 0x51, ResultBestTime = 0x52, ResultRank = 0x53;
// The save block's fields after the points: the costumes (a byte a character: bit 0 Type B, bit 1 Type C), the NPCs, the
// extra modes' flags
constexpr uint32_t SaveCostumes = 0x04, SaveNpcs = 0x0C, SaveModes = 0x1C;

constexpr const char* Tab = "Cheats";
constexpr const char* GroupHealth = "Health and infection";
constexpr const char* GroupWeapons = "Weapons and items";
constexpr const char* GroupMoving = "Movement and time";
constexpr const char* GroupPartners = "Partners";
constexpr const char* GroupPlayer = "Player 1";
constexpr const char* GroupResults = "Scenario results";
constexpr const char* GroupSave = "Collection and unlocks (saved)";

const char* const OffOn[] = {"Off", "On", nullptr};

// The rows' choices (labels) and what each writes
const char* const HealthLabels[] = {"Off", "Held at the maximum", "9999 and maximum 9999", nullptr};
const char* const GaugeLabels[] = {"Off", "0 %", "10 %", "25 %", "50 %", "75 %", "90 %", "99 %", nullptr};
const uint32_t GaugePercent[] = {0, 0, 10, 25, 50, 75, 90, 99};
const char* const Gauge2Labels[] = {"Off", "4FFFFFFF (gauge stays at 0 %)", "00000001", nullptr};
const uint32_t Gauge2Values[] = {0, 0x4FFFFFFF, 0x00000001};
const char* const AmmoLabels[] = {"Off", "Infinite", "Extra (a shot adds one)", "Max (99)", nullptr};
const char* const DamageLabels[] = {"Off", "x0.5", "x2", "x3", "x5", "x10", "x100", "Every enemy dies", nullptr};
const uint32_t DamageValues[] = {0, 0x3F000000, 0x40000000, 0x40400000, 0x40A00000, 0x41200000, 0x42C80000, 0x4FFFFFFF};
const char* const SpeedLabels[] = {"Off", "Fast (1.5)", "Super fast (2.0)", nullptr};
const uint32_t SpeedValues[] = {0, 0x3FC00000, 0x40000000};
const char* const Speed1Labels[] = {"Off", "Super slow", "Slow", "Medium", "Fast", "Super fast (through walls)", nullptr};
const uint32_t Speed1Values[] = {0, 0x3FF00000, 0x40000000, 0x40400000, 0x40800000, 0x4FFFFFFF};
const char* const Speed2Labels[] = {"Off", "x0.5", "x1.5", "x2", "x3", "x4", nullptr};
const uint32_t Speed2Values[] = {0, 0x3F000000, 0x3FC00000, 0x40000000, 0x40400000, 0x40800000};
const char* const WallsLabels[] = {"Off", "On", "R2+Up on, R2+Down off", nullptr};
const char* const TimeLabels[] = {"Off", "Frozen", "Frozen while L2 is held", nullptr};
const char* const PartnerLabels[] = {"Off",   "Kevin", "Mark", "Jim",   "George",    "David",
                                     "Alyssa", "Yoko", "Cindy", "No partner", nullptr};
const char* const CharacterLabels[] = {"Off", "Kevin", "Mark", "Jim", "George", "David", "Alyssa", "Yoko", "Cindy", nullptr};
const char* const CostumeLabels[] = {"Off", "0", "1 (Kevin: white T-shirt, jeans)", "2", "3", "4", "5", nullptr};
// bungholio's Player 1 Character Model Modifier list, without the models he marks "CANNOT LOAD" and the empty 0050
const char* const ModelLabels[] = {
    "Off", "0000 Normal", "0001 Mac", "0002 Rodrigez", "0003 Conrad", "0004 Hunk:B", "0005 Hunk", "0006 Miguel",
    "0007 Unfinished character", "0008 U.S.S.1", "000a Arnold", "000b Matt", "000c Billy", "000d Hursh", "000e Doctor",
    "0011 Peter", "0012 Marvin", "0013 Fred", "0014 Andy", "0016 Jean", "0017 Tonny", "0018 Injured cop", "0019 Keeper1",
    "001a Keeper2", "001b Austin", "001c Clint", "001d Bone", "001e Bob", "001f Bloody guy", "0020 Nathan", "0021 Samuel",
    "0023 Will", "0024 Zombie Will?", "0025 Roger", "0027 Carter", "0028 Greg", "0029 Scholar1", "002a Scholar2", "002b Jake",
    "002c Gary", "002d Man9", "002f Micky", "0030 Al", "0031 Maskman", "0032 Al:B", "0033 Ben", "0034 Little girl",
    "0035 Regan", "0036 Regan:B", "0037 Monica", "0038 Rinda", "0039 Rita", "003b Mary", "003c Kate", "003f Danny",
    "0040 Danny:B", "0041 Gill", "0042 Gill:B", "0046 Bald doctor", "0049 Cindy", "004a Doctor4", "0051 Kurt", "0052 Kurt:B",
    nullptr};
const char* const StatusLabels[] = {"Off", "00 fine", "01 poison", "02 bleeding", "03 bleeding", "04 fine", "05 poison",
                                    "06 bleeding", "07 bleeding", "08 fine", "09 poison", "0C fine", "10 fine (the AI ignores you)",
                                    "14 fine", "18 fine", "1C fine", "20 fine (no start menu)", "24 fine (no start menu)", "28",
                                    "FF (falls, can't move)", nullptr};
const uint8_t StatusValues[] = {0,    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0xFF};
const char* const SurvivorLabels[] = {"Off", "0 of 3", "1 of 3", "2 of 3", "3 of 3", nullptr};
const char* const RankLabels[] = {"Off", "F", "E", "D", "C", "B", "A", "S", nullptr};
const char* const RecordLabels[] = {"Off", "Not a new record", "A new record", nullptr};
const char* const ResultPointLabels[] = {"Off", "0", "10,000", "100,000", "1,000,000", "99,999,999", nullptr};
const uint32_t ResultPointValues[] = {0, 0, 10000, 100000, 1000000, 99999999};
const char* const EventLabels[] = {"Off", "All (FFFFFFFF)", "None (0)", nullptr};
const char* const PointLabels[] = {"Off", "Low half held at 65,535", "99,999", "3,276,800", "99,999,999", nullptr};
const char* const CostumeUnlockLabels[] = {"Off",          "All",          "Kevin B",      "Mark B",      "Jim B",
                                           "George B",     "David B",      "Alyssa B",     "Alyssa C",    "Alyssa B and C",
                                           "Yoko B",       "Yoko C",       "Yoko B and C", "Cindy B",     "Cindy C",
                                           "Cindy B and C", nullptr};
// Each choice's character (0 Kevin .. 7 Cindy) and bits (1 Type B, 2 Type C); "All" is the game's own costume bits
const uint8_t CostumeUnlockCharacter[] = {0, 0, 0, 1, 2, 3, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7};
const uint8_t CostumeUnlockBits[] = {0, 0, 1, 1, 1, 1, 1, 1, 2, 3, 1, 2, 3, 1, 2, 3};
// The animation picker's value: 0-255 (labels made at start-up)
std::vector<std::string> g_animationText;
std::vector<const char*> g_animationLabels;

// The save's bits the game's own unlocks and purchases set (RaccoonRecomp's reading of the collection code)
constexpr uint32_t CostumeWords[2] = {0x01010101, 0x03030301};
constexpr uint32_t NpcWords[3] = {0x72400C40, 0xFF03C200, 0x03FFC0FB};
constexpr uint32_t ModeFlags = 0x05000000; // the flags' last byte: bits 0x01 (lone wolf) and 0x04 (infinity mode)
constexpr uint32_t ScenarioBits = 0x1F;
constexpr uint32_t CollectionBits[6] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFBFE1FFF, 0x00079FFF};
// A purchase of items 0x00-0x0B also sets their bits in the two arrays after the owned bits
constexpr uint32_t OwnedExtra = 0x20, OwnedExtra2 = 0x40, OwnedExtraBits = 0x00000FFF;
constexpr uint32_t PointsMaximum = 99999999;

enum Row : int
{
    RowHealth,
    RowEveryoneHealth,
    RowNoEnemyDamage,
    RowUntouchable,
    RowUnnoticed,
    RowNoZombieOverTime,
    RowNoZombieBitten,
    RowVirusNeverUp,
    RowGauge,
    RowGauge2,
    RowInfection100,
    RowZombieAfterDeath,
    RowAmmo,
    RowItems,
    RowOneHit,
    RowDamage,
    RowRapidFire,
    RowSpeed,
    RowSpeed1,
    RowSpeed2,
    RowGiant,
    RowWalls,
    RowZombiesStill,
    RowTime,
    RowPartner2,
    RowPartner3,
    RowAlone,
    RowCharacter,
    RowModel,
    RowAltCostume,
    RowStatus,
    RowAnimation,
    RowNoDamageClear,
    RowNoWeaponClear,
    RowSurvivors,
    RowRank,
    RowBestPoints,
    RowBestTime,
    RowResultPoints,
    RowEvents1,
    RowEvents2,
    RowFiles,
    RowPoints,
    RowAvailable,
    RowOwned,
    RowOpened,
    RowScenarios,
    RowScenariiOpened,
    RowInfinity,
    RowCostumes,
    RowNpcs,
    RowCount,
};
std::array<int, RowCount> g_rows;

// A row's choice (a row this game hasn't: 0, off)
int Choice(Row row) { return g_rows[row] >= 0 ? ModSettings::Choice(g_rows[row]) : 0; }

// The game is in single play with a scenario set up (the player's maximum hit points are set when the character is made)
bool Single() { return g_layout != nullptr && rd8(g_layout->playMode) == 0; }
uint32_t Record(int n) { return g_layout->characters + static_cast<uint32_t>(n) * g_layout->characterSize; }
bool InScenario() { return Single() && rd16(Record(0) + F().hpMax) != 0; }
// The save data is made or loaded (the game sets the first scenario's bit then)
bool SaveReady() { return Single() && g_layout->scenarios != 0 && (rd32(g_layout->scenarios) & 1) != 0; }
// A cheat of this row is on and the game in single play: what the overrides ask
bool On(Row row) { return Single() && Choice(row) != 0; }

// A joker: exactly these buttons held (as the codes test the pad's word)
bool Exactly(uint32_t buttons)
{
    GamePad::State pad = GamePad::Read(g_layout->padFrames);
    return pad.valid && pad.held == buttons;
}

// A value held at an address while its cheat is on: the game's value kept and put back when it's switched off (unless the
// game changed it meanwhile)
struct Held
{
    uint32_t at = 0;
    int bytes = 0;
    uint32_t original = 0;
    uint32_t value = 0;
    bool active = false;
};

uint32_t Read(uint32_t at, int bytes) { return bytes == 1 ? rd8(at) : bytes == 2 ? rd16(at) : rd32(at); }
void Write(uint32_t at, int bytes, uint32_t v)
{
    if (bytes == 1)
    {
        wr8(at, static_cast<uint8_t>(v));
    }
    else if (bytes == 2)
    {
        wr16(at, static_cast<uint16_t>(v));
    }
    else
    {
        wr32(at, v);
    }
}

void Release(Held& h)
{
    if (h.active && Read(h.at, h.bytes) == h.value)
    {
        Write(h.at, h.bytes, h.original);
    }
    h.active = false;
}

void Hold(Held& h, uint32_t at, int bytes, uint32_t value)
{
    if (h.active && h.at != at)
    {
        Release(h);
    }
    if (!h.active)
    {
        h.at = at;
        h.bytes = bytes;
        h.original = Read(at, bytes);
        h.active = true;
    }
    h.value = value;
    if (Read(at, bytes) != value)
    {
        Write(at, bytes, value);
    }
}

// Held while `on`, released when not
void HoldIf(Held& h, bool on, uint32_t at, int bytes, uint32_t value)
{
    if (on)
    {
        Hold(h, at, bytes, value);
    }
    else
    {
        Release(h);
    }
}

void OrWord(uint32_t at, uint32_t bits)
{
    if ((rd32(at) & bits) != bits)
    {
        wr32(at, rd32(at) | bits);
    }
}

enum HeldSlot : int
{
    HeldHp,
    HeldHpMax,
    HeldGauge,
    HeldGauge2,
    HeldDamage,
    HeldRapid,
    HeldUntouchable,
    HeldUnnoticed,
    HeldSpeed0,
    HeldSpeed1,
    HeldSpeed2,
    HeldScaleX,
    HeldScaleY,
    HeldScaleZ,
    HeldWallsA,
    HeldWallsB,
    HeldCharacter,
    HeldModel,
    HeldAltCostume,
    HeldStatus,
    HeldAnimation,
    HeldCount,
};
std::array<Held, HeldCount> g_held;

bool g_unnoticed = false;   // the L2+Up / L2+Down joker's state
bool g_walkJoker = false;   // the R2+Up / R2+Down joker's state
bool g_infection = false;   // L1+Select held (nothing pressed lets it go)

void ReleaseAll()
{
    for (Held& h : g_held)
    {
        Release(h);
    }
    g_unnoticed = g_walkJoker = g_infection = false;
}

// The player's record's cheats
void PlayerCheats()
{
    const uint32_t p = Record(0);
    // Health: Codejunkies' Infinite Health (HP held at the maximum: what its code does, lh max / sh HP; MadCatz' 2300/2300 is
    // the same for Kevin) or Infinite Health & Max Health (9999 both)
    int health = Choice(RowHealth);
    HoldIf(g_held[HeldHp], health != 0, p + F().hp, 2, health == 2 ? 9999u : rd16(p + F().hpMax));
    HoldIf(g_held[HeldHpMax], health == 2, p + F().hpMax, 2, 9999);
    // The virus gauge held at a share of the character's maximum (bungholio's #1; Codejunkies' Zero Virus Gauge at 0 %)
    int gauge = Choice(RowGauge);
    uint32_t maximum = rd32(g_layout->gaugeMaxima + 4u * (rd8(p + F().characterType) & 7));
    HoldIf(g_held[HeldGauge], gauge != 0, p + F().gauge, 4, static_cast<uint32_t>(static_cast<uint64_t>(maximum) * GaugePercent[gauge % 8] / 100));
    int gauge2 = Choice(RowGauge2);
    HoldIf(g_held[HeldGauge2], gauge2 != 0, p + F().gauge2, 4, Gauge2Values[gauge2 % 3]);
    int damage = Choice(RowDamage);
    HoldIf(g_held[HeldDamage], damage != 0, p + F().damage, 4, DamageValues[damage % 8]);
    HoldIf(g_held[HeldRapid], Choice(RowRapidFire) != 0 && Exactly(GamePad::R1 | GamePad::Cross), p + F().rapidFire, 1, 0xFF);
    HoldIf(g_held[HeldUntouchable], Choice(RowUntouchable) != 0, p + F().untouchable, 1, 0x20);
    // Enemies Don't Notice: L2+Up sets the byte, L2+Down clears it, as written
    if (Choice(RowUnnoticed) != 0)
    {
        if (Exactly(GamePad::L2 | GamePad::Up))
        {
            g_unnoticed = true;
        }
        else if (Exactly(GamePad::L2 | GamePad::Down))
        {
            g_unnoticed = false;
        }
    }
    else
    {
        g_unnoticed = false;
    }
    HoldIf(g_held[HeldUnnoticed], g_unnoticed, p + F().unnoticed, 1, 1);
    // Speed: Codejunkies' two factors, else bungholio's
    int speed = Choice(RowSpeed);
    HoldIf(g_held[HeldSpeed0], speed != 0, p + F().speed0, 4, SpeedValues[speed % 3]);
    int speed2 = Choice(RowSpeed2);
    HoldIf(g_held[HeldSpeed2], speed != 0 || speed2 != 0, p + F().speed2, 4,
           speed != 0 ? SpeedValues[speed % 3] : Speed2Values[speed2 % 6]);
    int speed1 = Choice(RowSpeed1);
    HoldIf(g_held[HeldSpeed1], speed1 != 0, p + F().speed1, 4, Speed1Values[speed1 % 6]);
    bool giant = Choice(RowGiant) != 0;
    HoldIf(g_held[HeldScaleX], giant, p + F().scale, 4, 0x3FC00000);
    HoldIf(g_held[HeldScaleY], giant, p + F().scale + 4, 4, 0x3FC00000);
    HoldIf(g_held[HeldScaleZ], giant, p + F().scale + 8, 4, 0x3FC00000);
    bool walls = Choice(RowWalls) == 1;
    HoldIf(g_held[HeldWallsA], walls, p + F().wallsA, 4, 0xFFFFFFFF);
    HoldIf(g_held[HeldWallsB], walls, p + F().wallsB, 4, 0xFFFFFFFF);
    if (Choice(RowWalls) == 2)
    {
        if (Exactly(GamePad::R2 | GamePad::Up))
        {
            g_walkJoker = true;
        }
        else if (Exactly(GamePad::R2 | GamePad::Down))
        {
            g_walkJoker = false;
        }
    }
    else
    {
        g_walkJoker = false;
    }
    if (Choice(RowInfection100) != 0)
    {
        GamePad::State pad = GamePad::Read(g_layout->padFrames);
        if (pad.valid && pad.held == (GamePad::L1 | GamePad::Select))
        {
            g_infection = true;
        }
        else if (pad.valid && pad.held == 0)
        {
            g_infection = false;
        }
    }
    else
    {
        g_infection = false;
    }
    int character = Choice(RowCharacter);
    HoldIf(g_held[HeldCharacter], character != 0, p + F().characterType, 1, static_cast<uint32_t>(character - 1));
    int model = Choice(RowModel);
    uint32_t modelValue = model != 0 ? std::stoul(std::string(ModelLabels[model]).substr(0, 4), nullptr, 16) : 0;
    HoldIf(g_held[HeldModel], model != 0, p + F().model, 2, modelValue);
    int costume = Choice(RowAltCostume);
    HoldIf(g_held[HeldAltCostume], costume != 0, p + F().altCostume, 1, static_cast<uint32_t>(costume - 1));
    int status = Choice(RowStatus);
    HoldIf(g_held[HeldStatus], status != 0, p + F().status, 1, StatusValues[status % 20]);
    int animation = Choice(RowAnimation);
    HoldIf(g_held[HeldAnimation], animation != 0, p + F().animation, 1, static_cast<uint32_t>(animation - 1));
}

// 47iscool's partner choices: a partner's character written only while it isn't made yet (its maximum hit points 0), so a
// choice applies from the next scenario and a partner is never changed in the middle of one; 0xFF is no partner
void Partners()
{
    bool alone = Choice(RowAlone) != 0;
    for (int n = 1; n <= 2; n++)
    {
        int choice = Choice(n == 1 ? RowPartner2 : RowPartner3);
        uint32_t type = alone ? 0xFF : choice == 0 ? 0x100 : choice == 9 ? 0xFF : static_cast<uint32_t>(choice - 1);
        uint32_t r = Record(n);
        if (type <= 0xFF && rd16(r + F().hpMax) == 0 && rd8(r + F().characterType) != type)
        {
            wr8(r + F().characterType, static_cast<uint8_t>(type));
        }
    }
}

// bungholio's results codes: held while on
void Results()
{
    const uint32_t r = g_layout->results;
    if (Choice(RowNoDamageClear) != 0 && rd8(r + ResultDamaged) != 0)
    {
        wr8(r + ResultDamaged, 0);
    }
    if (Choice(RowNoWeaponClear) != 0 && rd8(r + ResultWeapon) != 0)
    {
        wr8(r + ResultWeapon, 0);
    }
    if (int v = Choice(RowSurvivors); v != 0)
    {
        wr8(r + ResultSurvivors, static_cast<uint8_t>(v - 1));
    }
    if (int v = Choice(RowRank); v != 0)
    {
        wr8(r + ResultRank, static_cast<uint8_t>(v - 1));
    }
    if (int v = Choice(RowBestPoints); v != 0)
    {
        wr8(r + ResultBestPoints, v == 2 ? 0xFF : 0);
    }
    if (int v = Choice(RowBestTime); v != 0)
    {
        wr8(r + ResultBestTime, v == 2 ? 0xFF : 0);
    }
    if (int v = Choice(RowResultPoints); v != 0)
    {
        wr32(r, ResultPointValues[v % 6]);
    }
    if (int v = Choice(RowEvents1); v != 0)
    {
        wr32(r + ResultEvents1, v == 1 ? 0xFFFFFFFF : 0);
    }
    if (int v = Choice(RowEvents2); v != 0)
    {
        wr32(r + ResultEvents2, v == 1 ? 0xFFFFFFFF : 0);
    }
    if (Choice(RowFiles) != 0)
    {
        wr16(g_layout->files, 0xFFFF);
    }
}

// The save's cheats: only the values the game's own code can make, never past its limits; what they unlock stays unlocked
void SaveData()
{
    const uint32_t s = g_layout->save;
    switch (Choice(RowPoints))
    {
    case 1:
        if ((rd32(s) | 0xFFFF) <= PointsMaximum)
        {
            wr32(s, rd32(s) | 0xFFFF); // Code Master's: the low half held at FFFF
        }
        break;
    case 2: wr32(s, 99999); break;          // MadCatz' Max Result Points
    case 3: wr32(s, 0x00320000); break;     // Codejunkies' Max Collection Pts
    case 4: wr32(s, PointsMaximum); break;  // Code Master's Max Reward Points
    default: break;
    }
    if (Choice(RowAvailable) != 0)
    {
        for (int i = 0; i < 6; i++)
        {
            OrWord(g_layout->available + 4u * i, CollectionBits[i]);
        }
    }
    if (Choice(RowOwned) != 0)
    {
        for (int i = 0; i < 6; i++)
        {
            OrWord(g_layout->owned + 4u * i, CollectionBits[i]);
        }
        OrWord(g_layout->owned + OwnedExtra, OwnedExtraBits);
        OrWord(g_layout->owned + OwnedExtra2, OwnedExtraBits);
    }
    if (Choice(RowScenarios) != 0)
    {
        OrWord(g_layout->scenarios, ScenarioBits);
    }
    if (Choice(RowInfinity) != 0)
    {
        OrWord(s + SaveModes, ModeFlags);
    }
    if (int v = Choice(RowCostumes); v == 1)
    {
        OrWord(s + SaveCostumes, CostumeWords[0]);
        OrWord(s + SaveCostumes + 4, CostumeWords[1]);
    }
    else if (v > 1 && v < static_cast<int>(sizeof(CostumeUnlockBits)))
    {
        uint32_t at = s + SaveCostumes + CostumeUnlockCharacter[v];
        if ((rd8(at) & CostumeUnlockBits[v]) != CostumeUnlockBits[v])
        {
            wr8(at, rd8(at) | CostumeUnlockBits[v]);
        }
    }
    if (Choice(RowNpcs) != 0)
    {
        for (int i = 0; i < 3; i++)
        {
            OrWord(s + SaveNpcs + 4u * i, NpcWords[i]);
        }
    }
}

// A row offered when this game has what it changes
void Add(Row row, const char* group, const char* name, const char* description, const char* const* choices,
         bool available = true)
{
    g_rows[row] = available ? ModSettings::Add({Tab, group, name, description, choices, 0}) : -1;
}

// The item record an item function's a0 names
uint32_t ItemRecord(Ps2Cpu* c) { return g_layout->items + (c->r[4].uw[0] & 0xFF) * ItemSize; }
}

void Use(const Layout& layout) { g_layout = &layout; }

void AddSettings()
{
    Add(RowHealth, GroupHealth, "Infinite health",
        "Your HP held: at your maximum, or 9999 with maximum 9999 (Codejunkies; MadCatz)", HealthLabels);
    Add(RowEveryoneHealth, GroupHealth, "Infinite health for everyone",
        "Every character's HP set to 9999 at the death check (Code Master, Lajos Szalay, Jay007, VirusPunk...)", OffOn);
    Add(RowNoEnemyDamage, GroupHealth, "Enemies don't damage you",
        "Enemy attacks take no HP from you or the partners; bites still infect (Code Master et al.)", OffOn);
    Add(RowUntouchable, GroupHealth, "Enemies can't touch you", "Enemies can't hit or grab you (bungholio)", OffOn,
        F().untouchable != 0);
    Add(RowUnnoticed, GroupHealth, "Enemies don't notice you",
        "L2+Up on, L2+Down off; buggy, the author warns: no status screen or firing while on (bungholio)", OffOn,
        F().unnoticed != 0);
    Add(RowNoZombieOverTime, GroupHealth, "Don't become a zombie over time",
        "The virus gauge doesn't rise with time, for anyone (Code Master et al.)", OffOn);
    Add(RowNoZombieBitten, GroupHealth, "Don't become a zombie when bitten",
        "Bites don't raise the virus gauge, for anyone (Code Master et al.)", OffOn);
    Add(RowVirusNeverUp, GroupHealth, "Virus level never goes up",
        "Every character's virus gauge stays at 0 (MadCatz)", OffOn);
    Add(RowGauge, GroupHealth, "Your virus gauge held at",
        "Of your character's maximum; 100 % would kill (bungholio; Codejunkies' Zero Virus Gauge is 0 %)", GaugeLabels);
    Add(RowGauge2, GroupHealth, "Your virus value #2", "The author's values (bungholio)", Gauge2Labels);
    Add(RowInfection100, GroupHealth, "L1+Select: infection 100 %",
        "Holding L1+Select kills by infection: every gauge to 100 % (Code Master et al.)", OffOn);
    Add(RowZombieAfterDeath, GroupHealth, "Become a zombie after death",
        "Dying, you play on as a zombie instead of YOU DIED (Code Master et al.)", OffOn);
    Add(RowAmmo, GroupWeapons, "Ammo",
        "Infinite: shots use no rounds (Code Master; Codejunkies; MadCatz). Extra and Max: Code Master et al.", AmmoLabels);
    Add(RowItems, GroupWeapons, "Infinite item usage", "Using an item doesn't use it up, for anyone (Code Master et al.)",
        OffOn);
    Add(RowOneHit, GroupWeapons, "One-hit kills", "Any hit kills an enemy (Code Master et al.)", OffOn);
    Add(RowDamage, GroupWeapons, "Your damage", "Your hits' damage multiplied (bungholio)", DamageLabels);
    Add(RowRapidFire, GroupWeapons, "Rapid fire", "Holding R1+Cross fires continuously (bungholio)", OffOn,
        F().rapidFire != 0);
    Add(RowSpeed, GroupMoving, "Hero speed", "Fast Hero and Super Fast Hero (Codejunkies)", SpeedLabels);
    Add(RowSpeed1, GroupMoving, "Speed #1", "Turning while moving goes at normal speed (bungholio)", Speed1Labels);
    Add(RowSpeed2, GroupMoving, "Speed #2", "A speed multiplier (bungholio)", Speed2Labels);
    Add(RowGiant, GroupMoving, "Giant hero", "You're drawn 1.5 times as big (Codejunkies)", OffOn);
    Add(RowWalls, GroupMoving, "Walk through walls",
        "On: always (bungholio). R2+Up on, R2+Down off, height kept: \"works most of the time\" (47iscool)", WallsLabels);
    Add(RowZombiesStill, GroupMoving, "Zombies don't move",
        "Enemies stay where they are (they still attack next to you) (Codejunkies)", OffOn);
    Add(RowTime, GroupMoving, "Escape time", "The event timers stop counting down (MadCatz; Codejunkies)", TimeLabels);
    Add(RowPartner2, GroupPartners, "First partner", "From the next scenario on (47iscool)", PartnerLabels);
    Add(RowPartner3, GroupPartners, "Second partner", "From the next scenario on (47iscool)", PartnerLabels);
    Add(RowAlone, GroupPartners, "Go it alone", "No partners from the next scenario on (47iscool)", OffOn);
    Add(RowCharacter, GroupPlayer, "Character type", "Who you are; others don't exist (bungholio)", CharacterLabels);
    Add(RowModel, GroupPlayer, "Character model", "Only your looks; the voice and moves stay (bungholio)", ModelLabels);
    Add(RowAltCostume, GroupPlayer, "Alternative costume", "(bungholio)", CostumeLabels,
        F().altCostume != 0);
    Add(RowStatus, GroupPlayer, "Status", "\"Some killed me instantly... useless\" (bungholio)", StatusLabels,
        F().status != 0);
    g_animationText.clear();
    g_animationLabels = {"Off"};
    for (int v = 0; v < 256; v++)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "%02X", v);
        g_animationText.emplace_back(text);
    }
    for (const std::string& t : g_animationText)
    {
        g_animationLabels.push_back(t.c_str());
    }
    g_animationLabels.push_back(nullptr);
    Add(RowAnimation, GroupPlayer, "Animation", "\"Mostly useless\": limp, collapse, walk backwards (bungholio)",
        g_animationLabels.data());
    Add(RowNoDamageClear, GroupResults, "No damage clear", "The scenario ends as a no-damage clear (bungholio)", OffOn,
        g_layout->results != 0);
    Add(RowNoWeaponClear, GroupResults, "No weapon clear", "The scenario ends as a no-weapon clear (bungholio)", OffOn,
        g_layout->results != 0);
    Add(RowSurvivors, GroupResults, "Survivors", "The results' survivors (bungholio)", SurvivorLabels,
        g_layout->results != 0);
    Add(RowRank, GroupResults, "Time rank", "The results' play-time rank (bungholio)", RankLabels,
        g_layout->results != 0);
    Add(RowBestPoints, GroupResults, "Result points record", "Whether they're a new best (bungholio)", RecordLabels,
        g_layout->results != 0);
    Add(RowBestTime, GroupResults, "Clear time record", "Whether it's a new best (bungholio)", RecordLabels,
        g_layout->results != 0);
    Add(RowResultPoints, GroupResults, "Result points", "The scenario's result points (bungholio)", ResultPointLabels,
        g_layout->results != 0);
    Add(RowEvents1, GroupResults, "Completed events 1 of 2", "All F's: every event done (bungholio)", EventLabels,
        g_layout->results != 0);
    Add(RowEvents2, GroupResults, "Completed events 2 of 2", "All F's: every event done (bungholio)", EventLabels,
        g_layout->results != 0);
    Add(RowFiles, GroupResults, "Have all files", "All 16 files of the scenario; they count as events (bungholio)", OffOn,
        g_layout->files != 0);
    Add(RowPoints, GroupSave, "Collection points",
        "Code Master et al. (65,535 low, 99,999,999); MadCatz (99,999); Codejunkies (3,276,800)", PointLabels,
        g_layout->save != 0);
    Add(RowAvailable, GroupSave, "Whole collection for sale", "Every collection item can be bought (Code Master et al.)",
        OffOn,
        g_layout->available != 0);
    Add(RowOwned, GroupSave, "Whole collection owned", "Every collection item owned (Code Master et al.; MadCatz)", OffOn,
        g_layout->owned != 0);
    Add(RowOpened, GroupSave, "Collection opened", "Every item can be bought while on; the save is untouched (Codejunkies)",
        OffOn,
        g_layout->available != 0);
    Add(RowScenarios, GroupSave, "Unlock all scenarios", "All five scenarios (MadCatz; Code Master et al.)", OffOn,
        g_layout->scenarios != 0);
    Add(RowScenariiOpened, GroupSave, "Scenarios opened",
        "All five can be chosen while on; the save is untouched (Codejunkies)", OffOn,
        g_layout->scenarios != 0);
    Add(RowInfinity, GroupSave, "Unlock infinity mode", "Infinity and lone wolf modes (MadCatz; Code Master et al.)", OffOn,
        g_layout->save != 0);
    Add(RowCostumes, GroupSave, "Unlock costumes", "Type B for all, Type C for Alyssa, Yoko and Cindy (Code Master et al.)",
        CostumeUnlockLabels,
        g_layout->save != 0);
    Add(RowNpcs, GroupSave, "Unlock all NPCs", "The collection's 40 NPCs at the character select (Code Master et al.)",
        OffOn,
        g_layout->save != 0);
}

void Frame()
{
    if (g_layout == nullptr)
    {
        return;
    }
    if (!Single())
    {
        // Network play: nothing (the overrides run the game's own, Single() being false)
        ReleaseAll();
        return;
    }
    if (InScenario())
    {
        PlayerCheats();
        if (g_layout->results != 0)
        {
            Results();
        }
    }
    Partners();
    if (g_layout->save != 0 && SaveReady())
    {
        SaveData();
    }
}

void Ammo(Ps2Cpu* c, Ps2Function original)
{
    int mode = Single() ? Choice(RowAmmo) : 0;
    uint32_t count = ItemRecord(c) + ItemCount;
    int16_t before = static_cast<int16_t>(rd16(count));
    if (mode == 0 || before <= 0)
    {
        original(c);
        return;
    }
    // A round is used by "count - 1" and a store (the codes change those): one more round for the shot, so the game's own
    // checks after it (an empty weapon put away) see what the code made
    wr16(count, static_cast<uint16_t>(before + 1));
    original(c);
    int16_t after = static_cast<int16_t>(rd16(count));
    if (after == before + 1)
    {
        wr16(count, static_cast<uint16_t>(before)); // no round used: as it was
        return;
    }
    // Infinite: the count stays (Code Master's store removed); Extra: one more (count + 1); Max: 99 (the fixed Max Infinite Ammo)
    wr16(count, static_cast<uint16_t>(mode == 1 ? before : mode == 2 ? before + 1 : 99));
}

void ItemUse(Ps2Cpu* c, Ps2Function original)
{
    uint32_t count = ItemRecord(c) + ItemCount;
    int16_t before = static_cast<int16_t>(rd16(count));
    if (!On(RowItems) || before <= 0)
    {
        original(c);
        return;
    }
    // The count's store removed: one more before, so the item isn't used up (nor put away at 0), and as it was after
    wr16(count, static_cast<uint16_t>(before + 1));
    original(c);
    if (static_cast<int16_t>(rd16(count)) <= before + 1)
    {
        wr16(count, static_cast<uint16_t>(before));
    }
}

void GaugeOverTime(Ps2Cpu* c, Ps2Function original)
{
    const uint32_t gauge = c->r[4].uw[0] + F().gauge;
    const uint32_t before = rd32(gauge);
    original(c);
    if (!Single())
    {
        return;
    }
    if (g_infection)
    {
        // Code Master's replacement makes the update store a value past any maximum: the next update kills
        wr32(gauge, 0x7FFFFFFF);
    }
    else if (Choice(RowVirusNeverUp) != 0)
    {
        wr32(gauge, 0); // MadCatz' li s0,0 before the store
    }
    else if (Choice(RowNoZombieOverTime) != 0)
    {
        wr32(gauge, before); // the store removed
    }
}

void GaugeBite(Ps2Cpu* c, Ps2Function original)
{
    const uint32_t gauge = c->r[4].uw[0] + F().gauge;
    const uint32_t before = rd32(gauge);
    original(c);
    if (On(RowNoZombieBitten))
    {
        wr32(gauge, before); // the bite's store removed
    }
}

void EnemyDamage(Ps2Cpu* c, Ps2Function original)
{
    // Code Master's code makes the damage taken (+0x138) 0x7FFF; the game's own invulnerable enemies (HP 0x7FFF, a scene's)
    // stay so here
    constexpr uint32_t DamageTaken = 0x138;
    const uint32_t enemy = c->r[4].uw[0];
    if (On(RowOneHit) && rd16(enemy + F().hp) != 0x7FFF)
    {
        wr16(enemy + DamageTaken, 0x7FFF);
    }
    original(c);
}

void PlayerDamage(Ps2Cpu* c, Ps2Function original)
{
    // Code Master's code makes the function leave as it does in network play: the hit's flag (+0x13F) cleared, no damage
    constexpr uint32_t HitFlag = 0x13F;
    if (On(RowNoEnemyDamage))
    {
        wr8(c->r[4].uw[0] + HitFlag, 0);
        return;
    }
    original(c);
}

void DeathCheck(Ps2Cpu* c, Ps2Function original)
{
    if (On(RowEveryoneHealth))
    {
        wr16(c->r[4].uw[0] + F().hp, 9999); // Code Master's li v1,9999 and its store (16 bits: the maximum kept)
    }
    original(c);
}

void SinglePlay(Ps2Cpu* c, Ps2Function original)
{
    original(c);
    // Become A Zombie After Death: at death the game takes its network play's path (the character plays on as a zombie)
    if (c->r[31].uw[0] == g_layout->deathOfflineReturn && On(RowZombieAfterDeath))
    {
        c->r[2].sd[0] = 0;
    }
}

void Movement(Ps2Cpu* c, Ps2Function original)
{
    // Zombies Don't Move: the enemies' update doesn't call the movement routine
    if (c->r[31].uw[0] == g_layout->enemyMoveReturn && On(RowZombiesStill))
    {
        return;
    }
    original(c);
}

void Timers(Ps2Cpu* c, Ps2Function original)
{
    // The countdown's "- 1" made "- 0" (or its store removed): the timers stay, and none reaches 0
    int mode = Single() ? Choice(RowTime) : 0;
    if (mode == 1 || (mode == 2 && Exactly(GamePad::L2)))
    {
        return;
    }
    original(c);
}

void Collision(Ps2Cpu* c, Ps2Function original)
{
    // 47iscool's joker: the collision function returns at once (jr ra)
    if (g_walkJoker && Single())
    {
        return;
    }
    original(c);
}

void Available(Ps2Cpu* c, Ps2Function original)
{
    original(c);
    if (On(RowOpened))
    {
        c->r[2].sd[0] = 1; // Codejunkies' code removes the movz that ends the check with 0
    }
}

void ScenarioSelect(Ps2Cpu* c, Ps2Function original)
{
    // Codejunkies' code skips the select's lock check: here the five scenarios' bits are there while the select's functions
    // run, and the save's own word is as it was after
    if (!On(RowScenariiOpened) || !SaveReady())
    {
        original(c);
        return;
    }
    const uint32_t word = rd32(g_layout->scenarios);
    wr32(g_layout->scenarios, word | ScenarioBits);
    original(c);
    if (rd32(g_layout->scenarios) == (word | ScenarioBits))
    {
        wr32(g_layout->scenarios, word);
    }
}
}
