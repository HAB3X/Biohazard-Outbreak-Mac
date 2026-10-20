#include "partner_ai.h"

#include "partner_ai_brain.h"
#include "partner_ai_telemetry.h"
#include "partner_ai_world.h"

#include <array>
#include <atomic>
#include <cstdio>

namespace PartnerAi
{
// File 1 (SLPM-65428): the characters' update is GAME.BIN's 0x6383E0 (the records at 0x476DD0, 0x10E0 bytes each; it calls
// SINGLE.BIN's think 0x822440 for those with the CPU flag at +0xFF0 set, only while the play mode 0x3065C2 is 0); the
// enemies' records at 0x3FFCA0, 0xD90 bytes each (SINGLE.BIN's 0x830270 picks one so), their flags +0xC94 (0x6A3B60 lists
// those without 0x100 for the AI). Hit points +0x544 (2300 Kevin, 3000
// Mark, 1500 Cindy in the bar; an enemy's too), the motion byte +0x54F, the virtual pad +0xF78 (0x1AFC60 copies the player's
// pad there), the AI's plan, move and action +0xFF4, +0xFF8, +0x103E (the think's tables' indices), its enemy +0x106E and
// the fight's +0x1068 (the fight plan 0x82C8C0 sets one from the other), its wants +0x10C8 (the perception 0x822520 sums
// them, the plan step's 0x823390 picks the plan by the highest), the weapon in hand +0xC7C (0x831D80), the line being said
// +0x1075 (GAME.BIN's speech code 0x687420 sets it); 0x823340 starts a plan, 0x82A690 a move. The fight plan 0x82C8C0: its
// step +0xFFC, flags +0x1048, threat level kept +0x1050 from the perception's +0x106C. The items at 0x3967D0, 0x3C bytes each
// (rounds +0x24, class +0x2B; the executable's 0x583EA0 reads a class's flags from the classes at 0x716980); the pack +0xC84
// (four, then the own item: Mark's handgun, item 82, 15 rounds at the start of the bar; then the four in the own item: Cindy's
// herb case holds her herbs there; 0x836BD0 and 0x835300 list them); the item check
// 0x823780, which swaps to a better weapon (0x835300) for bit 4 of +0x1096
const Layout File1 = {.name = "file1",
                      .characters = 0x476DD0,
                      .characterSize = 0x10E0,
                      .enemies = 0x3FFCA0,
                      .enemySize = 0xD90,
                      .currentRoom = 0x3065AC,
                      .playMode = 0x3065C2,
                      .enemyFlags = 0xC94,
                      .hp = 0x544,
                      .motion = 0x54F,
                      .cpu = 0xFF0,
                      .padPressed = 0xF78,
                      .aiState = 0xFF4,
                      .planStep = 0xFFC,
                      .fightFlags = 0x1048,
                      .threatLevel = 0x106C,
                      .fightThreat = 0x1050,
                      .aiAction = 0x103E,
                      .aiTarget = 0x106E,
                      .aiTargetRecord = 0x1068,
                      .desires = 0x10C8,
                      .weapon = 0xC7C,
                      .pack = 0xC84,
                      .items = 0x3967D0,
                      .itemClasses = 0x716980,
                      .itemChecks = 0x1096,
                      .checkItems = 0x823780,
                      .speech = 0x1075,
                      .setPlan = 0x823340,
                      .setMove = 0x82A690};

// File 2 (SLPM-65692): the same code with the fields moved: the update 0x63EAF0 (records at 0x47BD30, 0x1100 bytes; the
// think, SINGLE's 0x7E99D0, for the CPU flag at +0xBB4; play mode 0x3137D2), enemies at 0x403DB0, 0xDB0 bytes (SINGLE's
// 0x7E04C0 as recompiled: 0x400000 + 15792 + index * 3504), their flags +0xCB0 (0x6B3800). Hit points +0x540 (2300 Kevin, 3000 and 1800 his partners in the
// Training Ground), motion +0x54B (the think's checks), the pad +0xF88 (0x1B08B0), plan, move and action +0x1010, +0x1014,
// +0x105A (the think 0x7E99D0 and plan step 0x7EA990 read them so), the enemy +0x108A and the fight's +0x1084 (0x7E04C0), the
// wants +0x10F4 (the plan step's 0x7EAA30), the weapon +0xC8C, the line being said +0x1091; 0x7EA9E0 starts a plan, 0x7DA650
// a move. The fight plan 0x7DCA20: step +0x1018, flags +0x1064, threat level +0x106C from +0x1088; the items at 0x397B40
// (their classes at 0x75D100: the executable's class lookups read them so), the pack +0xC94; the item check 0x7EB0C0 (bit 4
// of +0x10B0)
const Layout File2 = {.name = "file2",
                      .characters = 0x47BD30,
                      .characterSize = 0x1100,
                      .enemies = 0x403DB0,
                      .enemySize = 0xDB0,
                      .currentRoom = 0x3137BC,
                      .playMode = 0x3137D2,
                      .enemyFlags = 0xCB0,
                      .hp = 0x540,
                      .motion = 0x54B,
                      .cpu = 0xBB4,
                      .padPressed = 0xF88,
                      .aiState = 0x1010,
                      .planStep = 0x1018,
                      .fightFlags = 0x1064,
                      .threatLevel = 0x1088,
                      .fightThreat = 0x106C,
                      .aiAction = 0x105A,
                      .aiTarget = 0x108A,
                      .aiTargetRecord = 0x1084,
                      .desires = 0x10F4,
                      .weapon = 0xC8C,
                      .pack = 0xC94,
                      .items = 0x397B40,
                      .itemClasses = 0x75D100,
                      .itemChecks = 0x10B0,
                      .checkItems = 0x7EB0C0,
                      .speech = 0x1091,
                      .setPlan = 0x7EA9E0,
                      .setMove = 0x7DA650};

namespace
{
enum Mode : int
{
    ModeNew,
    ModeOriginal,
};
std::atomic<int> g_mode{ModeNew};

const char* const ModeNames[] = {"New", "Original", nullptr};
int GetMode() { return g_mode.load(); }
void SetMode(int mode) { g_mode.store(mode == ModeOriginal ? ModeOriginal : ModeNew); }

// The game AI's plans (its plan table's order, both games: docs/research/partner-ai.md) and the wants that pick them
constexpr uint16_t PlanPick = 0;    // nothing: the next plan is picked by the wants
constexpr uint16_t PlanWait = 1;    // stand facing the player (its move 1) for 15 s, then off to a node (want 4)
constexpr uint16_t PlanFollow = 3;  // to the player (want 0)
constexpr uint16_t PlanFight = 5;   // the action routines against the picked enemy (want 3)
constexpr int WantFollow = 0;
constexpr int WantFight = 3;
constexpr int WantWait = 4;
constexpr int WantCount = 8;
// Plans a decision may cut short: picking, waiting, looking about, following, staying in the player's room, fighting. Not
// the item and door plans (7-9), helping the player up (10) or the special ones (11-15)
constexpr bool Interruptible(uint16_t plan) { return plan <= PlanFight; }
// The least time between two plans started for a partner: the game's own plan step may refuse a plan for a frame (busy)
constexpr uint64_t ReplanFrames = 20;
// A guarding partner's wait cut short this often, so the game's own picking runs (helping the player up, Cindy healing them:
// its special plans come before the wants, and only when a plan is picked)
constexpr uint64_t RepickFrames = 90;
// The virtual pad's held buttons, after the pressed and released ones
constexpr uint32_t PadHeld = 8;

const Tunables g_tunables{};

// The frames, counted by the thinks: the characters' update runs them in the records' order, so a record at or before the
// last one thought for starts a new frame
uint64_t g_frame = 0;
uint32_t g_lastRecord = 0;

struct PartnerState
{
    Memory memory;
    uint64_t lastReplan = 0;
    uint32_t pad = 0;        // this frame's buttons instead of the game AI's (the perception decides, the think's end applies)
    uint32_t pressed = 0;
    uint64_t padFrame = 0;
};
std::array<PartnerState, CharacterCount> g_partners;

int PartnerIndex(const Layout& layout, uint32_t record)
{
    return static_cast<int>((record - layout.characters) / layout.characterSize);
}

// The game's own function at `address` called with the record and one argument (the recompiled code's calling convention)
void CallGame(Ps2Cpu* c, uint32_t address, uint32_t record, uint32_t argument)
{
    c->r[4].sd[0] = static_cast<int32_t>(record);
    c->r[5].sd[0] = static_cast<int32_t>(argument);
    ps2_call(c, address);
}

// The fight plan's steps (File 1 0x82C8C0, File 2 0x7DCA20): 0 sets out for a node of the room first (a random one, by
// the room's node weights), 1 looks for items, 2 picks the attack for the enemy's kind and its own state, 3 attacks
constexpr uint8_t FightPickAttack = 2;
constexpr uint8_t FightAttacking = 3;

uint32_t EnemyRecord(const Layout& layout, int index) { return layout.enemies + layout.enemySize * static_cast<uint32_t>(index); }

// The fight plan started at its attack: the plan's own start (`setPlan`, its fields cleared) without the walk to a random
// node its first step makes, which held the game's partners back for seconds while the player was being bitten
void Engage(Ps2Cpu* c, const Layout& layout, uint32_t record, int target)
{
    CallGame(c, layout.setMove, record, 0);
    CallGame(c, layout.setPlan, record, PlanFight);
    wr32(record + layout.fightFlags, 0);
    wr32(record + layout.fightFlags + 4, 0);
    wr32(record + layout.aiTargetRecord, EnemyRecord(layout, target));
    wr8(record + layout.fightThreat, rd8(record + layout.threatLevel));
    wr8(record + layout.planStep, FightPickAttack);
}

// The game AI's own item check (File 1 0x823780, File 2 0x7EB0C0) asked to look: the bits say at what (1 an item to use or
// pick up, 2 to give, 4 a better weapon to hold, 8 a reload), as the plans ask on arriving somewhere. It decides by its own
// tables (the hit points at which each character uses each recovery item...) and starts the plan for it. True if it did
bool CheckItems(Ps2Cpu* c, const Layout& layout, uint32_t record, uint16_t what)
{
    wr16(record + layout.itemChecks, rd16(record + layout.itemChecks) | what);
    c->r[4].sd[0] = static_cast<int32_t>(record);
    ps2_call(c, layout.checkItems);
    return c->r[2].uw[0] != 0;
}

constexpr uint16_t LookForBetterWeapon = 4;
constexpr uint16_t LookAtEverything = 0xF;

// A decision put into the game's AI: the wants that pick its next plan, the enemy to fight, and the plan cut short when it
// isn't the one wanted (as the game cuts its own: the move stopped, then the plan started)
void Apply(Ps2Cpu* c, const Layout& layout, uint32_t record, const Decision& decision, PartnerState& state)
{
    const uint16_t plan = rd16(record + layout.aiState);
    const bool mayCut = Interruptible(plan) && g_frame - state.lastReplan >= ReplanFrames;
    // An item check runs between plans, or cuts a fight short when urgent (hurt, with a recovery item)
    const bool betweenPlans = plan != PlanFight || decision.urgent;
    if (decision.items != ItemCheck::None && mayCut && betweenPlans)
    {
        const bool gun = decision.items == ItemCheck::TakeOutGun;
        const bool started = CheckItems(c, layout, record, gun ? LookForBetterWeapon : LookAtEverything);
        if (started)
        {
            state.lastReplan = g_frame;
        }
        if (Measuring() && (started || gun))
        {
            NoteEvent(PartnerIndex(layout, record), gun ? (started ? "take out gun" : "take out gun: refused")
                                                        : "item plan", g_frame);
        }
        if (started)
        {
            return;
        }
    }
    if (decision.intent == Intent::Free)
    {
        return;
    }
    const int want = decision.intent == Intent::Follow ? WantFollow : decision.intent == Intent::Guard ? WantWait : WantFight;
    for (int n = 0; n < WantCount; n++)
    {
        wr8(record + layout.desires + static_cast<uint32_t>(n), n == want ? 0xFF : 0);
    }
    if (decision.intent != Intent::Fight)
    {
        const uint16_t wanted = decision.intent == Intent::Follow ? PlanFollow : PlanWait;
        const bool repick = plan == PlanWait && g_frame - state.lastReplan >= RepickFrames;
        if ((plan != wanted || repick) && plan != PlanPick && mayCut)
        {
            state.lastReplan = g_frame;
            CallGame(c, layout.setMove, record, 0);
            CallGame(c, layout.setPlan, record, PlanPick);
        }
        return;
    }
    wr8(record + layout.aiTarget, static_cast<uint8_t>(decision.target));
    const uint32_t enemy = EnemyRecord(layout, decision.target);
    if (plan == PlanFight)
    {
        const uint8_t step = rd8(record + layout.planStep);
        if (step < FightPickAttack)
        {
            // the game's own fight setting out: straight to the attack
            wr32(record + layout.aiTargetRecord, enemy);
            wr8(record + layout.fightThreat, rd8(record + layout.threatLevel));
            wr8(record + layout.planStep, FightPickAttack);
        }
        else if (step == FightAttacking && rd32(record + layout.aiTargetRecord) != enemy &&
                 g_frame - state.lastReplan >= ReplanFrames)
        {
            // another enemy: its attack picked again for its kind
            state.lastReplan = g_frame;
            wr32(record + layout.aiTargetRecord, enemy);
            wr8(record + layout.planStep, FightPickAttack);
        }
        return;
    }
    // plan 0 is a plan about to be picked: the game's own picking runs first (its special plans, helping the player up and
    // healing among them, before the wants), so a fight is started only by cutting another plan short
    if (plan != PlanFight && plan != PlanPick && mayCut)
    {
        state.lastReplan = g_frame;
        Engage(c, layout, record, decision.target);
    }
}
}

void PickItem(Ps2Cpu* c, const Layout& layout, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    const uint32_t objectOut = c->r[7].uw[0];
    original(c);
    if (c->r[2].uw[0] == 0)
    {
        return;
    }
    // The object an item lies on in the room: its item at +0x40 (the chooser reads it so)
    constexpr uint32_t ObjectItem = 0x40;
    const uint8_t item = rd8(rd32(objectOut) + ObjectItem);
    const uint32_t flags = ItemClassFlags(layout, item);
    const bool refuse = g_mode.load() == ModeNew && rd8(layout.playMode) == 0 && !Consumable(flags);
    if (refuse)
    {
        c->r[2].sd[0] = 0; // nothing picked: the player's to find
    }
    if (Measuring())
    {
        char what[64];
        std::snprintf(what, sizeof(what), "pick up item=%d class=%x %s", item, flags, refuse ? "refused" : "allowed");
        NoteEvent(PartnerIndex(layout, record), what, g_frame);
    }
}

const Ps2GameSetting Settings[] = {{"Gameplay", "Partner AI", ModeNames, GetMode, SetMode}};
const int SettingCount = 1;

void Think(Ps2Cpu* c, const Layout& layout, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    if (record <= g_lastRecord)
    {
        g_frame++;
    }
    g_lastRecord = record;
    original(c);
    const int partner = PartnerIndex(layout, record);
    const PartnerState& state = g_partners[static_cast<size_t>(partner)];
    if (state.pad != 0 && state.padFrame == g_frame)
    {
        // the director's buttons this frame in place of the game AI's
        wr32(record + layout.padPressed, state.pressed);
        wr32(record + layout.padPressed + PadHeld, state.pad);
    }
    if (Measuring())
    {
        Measure(ReadWorld(layout), partner, g_frame);
    }
}

void Perceive(Ps2Cpu* c, const Layout& layout, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    original(c);
    // Single play only: the game calls the think only then, and this says so again (network play's other players are people)
    constexpr uint8_t SinglePlay = 0;
    if (g_mode.load() != ModeNew || rd8(layout.playMode) != SinglePlay)
    {
        return;
    }
    const World world = ReadWorld(layout);
    const int partner = PartnerIndex(layout, record);
    PartnerState& state = g_partners[static_cast<size_t>(partner)];
    const Abilities abilities = AbilitiesOf(world.characters[static_cast<size_t>(partner)]);
    const Decision decision = Decide(world, partner, abilities, state.memory, g_frame, g_tunables);
    Apply(c, layout, record, decision, state);
    state.pad = decision.pad;
    state.pressed = decision.pressed;
    state.padFrame = g_frame;
    if (Measuring())
    {
        Note(partner, decision, abilities, g_frame);
    }
}
}
