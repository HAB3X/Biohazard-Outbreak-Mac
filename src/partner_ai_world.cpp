#include "partner_ai_world.h"

#include <cmath>
#include <cstring>

namespace PartnerAi
{
namespace
{
// The fields every character's and enemy's record has at the same place, in both games (memory dumps of File 1's bar and
// File 2's Training Ground; the characters' update reads them so: File 1's 0x6383E0, File 2's 0x63EAF0)
constexpr uint32_t Active = 0x0;
constexpr uint32_t Id = 0x3;
constexpr uint32_t Target = 0x24;   // an enemy's: the record of the character it's after (File 1's bar: the partners' and the player's)
constexpr uint32_t Position = 0x38; // three floats
constexpr uint32_t Facing = 0x92;
constexpr uint32_t Room = 0x1E2;

float F32(uint32_t address)
{
    uint32_t bits = rd32(address);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

Vec ReadVec(uint32_t address) { return {F32(address), F32(address + 4), F32(address + 8)}; }

int S16(uint32_t address) { return static_cast<int16_t>(rd16(address)); }
}

World ReadWorld(const Layout& layout)
{
    World world{};
    world.currentRoom = static_cast<int16_t>(rd16(layout.currentRoom));
    for (int n = 0; n < CharacterCount; n++)
    {
        uint32_t at = layout.characters + layout.characterSize * static_cast<uint32_t>(n);
        Character& c = world.characters[n];
        c.address = at;
        c.active = rd8(at + Active) != 0;
        c.cpu = rd8(at + layout.cpu) != 0;
        c.id = rd8(at + Id);
        c.position = ReadVec(at + Position);
        c.facing = rd16(at + Facing);
        c.room = static_cast<int16_t>(rd16(at + Room));
        c.hp = S16(at + layout.hp);
        c.hpMax = S16(at + layout.hp + 2);
        c.motion = rd8(at + layout.motion);
        c.padPressed = rd32(at + layout.padPressed);
        c.padHeld = rd32(at + layout.padPressed + 8);
        c.plan = rd16(at + layout.aiState);
        c.move = rd16(at + layout.aiState + 4);
        c.action = rd8(at + layout.aiAction);
        c.target = rd8(at + layout.aiTarget);
        c.speech = rd8(at + layout.speech);
    }
    for (int n = 0; n < EnemyCount; n++)
    {
        uint32_t at = layout.enemies + layout.enemySize * static_cast<uint32_t>(n);
        Enemy& e = world.enemies[n];
        e.address = at;
        e.active = rd8(at + Active) != 0;
        e.position = ReadVec(at + Position);
        e.facing = rd16(at + Facing);
        e.room = static_cast<int16_t>(rd16(at + Room));
        e.hp = S16(at + layout.hp);
        e.hpMax = S16(at + layout.hp + 2);
        e.target = rd32(at + Target);
    }
    return world;
}

float Distance(const Vec& a, const Vec& b) { return std::hypot(a.x - b.x, a.z - b.z); }
}
