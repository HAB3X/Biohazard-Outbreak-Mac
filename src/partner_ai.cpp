#include "partner_ai.h"

#include "partner_ai_telemetry.h"
#include "partner_ai_world.h"

namespace PartnerAi
{
// File 1 (SLPM-65428): the characters' update is GAME.BIN's 0x6383E0 (the records at 0x476DD0, 0x10E0 bytes each; it calls
// SINGLE.BIN's think 0x822440 for those with the CPU flag at +0xFF0 set, only while the play mode 0x3065C2 is 0); the
// enemies' records at 0x3FFCA0, 0xD90 bytes each (SINGLE.BIN's 0x830270 picks one so). Hit points +0x544 (2300 Kevin, 3000
// Mark, 1500 Cindy in the bar; an enemy's too), the motion byte +0x54F, the virtual pad +0xF78 (0x1AFC60 copies the player's
// pad there), the AI's plan, move and action +0xFF4, +0xFF8, +0x103E (the think's tables' indices), its enemy +0x106E, the
// line being said +0x1075 (GAME.BIN's speech code 0x687420 sets it)
const Layout File1 = {.name = "file1",
                      .characters = 0x476DD0,
                      .characterSize = 0x10E0,
                      .enemies = 0x3FFCA0,
                      .enemySize = 0xD90,
                      .currentRoom = 0x3065AC,
                      .playMode = 0x3065C2,
                      .hp = 0x544,
                      .motion = 0x54F,
                      .cpu = 0xFF0,
                      .padPressed = 0xF78,
                      .aiState = 0xFF4,
                      .aiAction = 0x103E,
                      .aiTarget = 0x106E,
                      .speech = 0x1075};

// File 2 (SLPM-65692): the same code with the fields moved: the update 0x63EAF0 (records at 0x47BD30, 0x1100 bytes; the
// think, SINGLE's 0x7E99D0, for the CPU flag at +0xBB4; play mode 0x3137D2), enemies at 0x403DB0, 0xDB0 bytes (SINGLE's
// 0x7E04C0 as recompiled: 0x400000 + 15792 + index * 3504). Hit points +0x540 (2300 Kevin, 3000 and 1800 his partners in the
// Training Ground), motion +0x54B (the think's checks), the pad +0xF88 (0x1B08B0), the AI's fields 0x1C on from File 1's
// (its think 0x7E99D0 and plan step 0x7EA990 read them so), the line being said +0x1091
const Layout File2 = {.name = "file2",
                      .characters = 0x47BD30,
                      .characterSize = 0x1100,
                      .enemies = 0x403DB0,
                      .enemySize = 0xDB0,
                      .currentRoom = 0x3137BC,
                      .playMode = 0x3137D2,
                      .hp = 0x540,
                      .motion = 0x54B,
                      .cpu = 0xBB4,
                      .padPressed = 0xF88,
                      .aiState = 0x1010,
                      .aiAction = 0x105A,
                      .aiTarget = 0x108A,
                      .speech = 0x1091};

namespace
{
// The frames, counted by the thinks: the characters' update runs them in the records' order, so a record at or before the
// last one thought for starts a new frame
uint64_t g_frame = 0;
uint32_t g_lastRecord = 0;

int PartnerIndex(const Layout& layout, uint32_t record)
{
    return static_cast<int>((record - layout.characters) / layout.characterSize);
}
}

void Think(Ps2Cpu* c, const Layout& layout, Ps2Function original)
{
    const uint32_t record = c->r[4].uw[0];
    if (record <= g_lastRecord)
    {
        g_frame++;
    }
    g_lastRecord = record;
    original(c);
    if (Measuring())
    {
        Measure(ReadWorld(layout), PartnerIndex(layout, record), g_frame);
    }
}
}
