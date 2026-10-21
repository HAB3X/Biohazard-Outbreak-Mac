#include "partner_ai_world.h"

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
// The enemies' flag the game's AI lists leave out (File 1's 0x6A3B60, File 2's 0x6B3800, which make the per-character lists)
constexpr uint32_t SetAside = 0x100;

float F32(uint32_t address)
{
    uint32_t bits = rd32(address);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

Vec ReadVec(uint32_t address) { return {F32(address), F32(address + 4), F32(address + 8)}; }

int S16(uint32_t address) { return static_cast<int16_t>(rd16(address)); }

// An item's record (File 1's executable reads them so: 0x584800, 0x583EA0) and its class's flags (the classes' table, 0x18
// bytes each, its flags first). The flags, from the classes of File 1's bar (the table dumped, each class's item number at
// +0x12): 1 a weapon; with 0x2000, 0x4000 or 0x8000 one that fires (the handguns, Kevin's and Mark's 0x190 and 0x191,
// 0x102041; the shotguns and rifles 0x104001, 0x108001), without them a melee weapon (the knife and the pipes, 0x1001,
// 0x2481001...); ammunition 2 (0x100002, 0xFA-0x106: the AI's item use 0x833C90 takes 2 for rounds), 0x400 and 0x800 (the
// boxes of rounds 0xC8-0xD2, 0x100400: File 2's characters start with one, 7 and 15 rounds, beside their handguns); 4 without 1 a
// recovery item (the herbs and sprays, 0x12C-0x13D, 0x40004: 0x833C90 asks for 4 after 2)
constexpr uint32_t ItemSize = 0x3C;
constexpr uint32_t ItemThere = 0x0;
constexpr uint32_t ItemRounds = 0x24;
constexpr uint32_t ItemClass = 0x2B;
constexpr uint8_t NoClass = 0xFF;
constexpr uint32_t ItemClassSize = 0x18;
constexpr uint32_t ClassFirearm = 0xE000;

Item ReadItem(const Layout& layout, uint8_t id)
{
    Item item;
    const uint32_t at = layout.items + ItemSize * id;
    if (id == 0 || rd8(at + ItemThere) == 0)
    {
        return item;
    }
    item.id = id;
    item.rounds = S16(at + ItemRounds);
    const uint32_t flags = ItemClassFlags(layout, id);
    item.weapon = (flags & ClassWeapon) != 0;
    item.fires = item.weapon && (flags & ClassFirearm) != 0;
    item.heals = !item.weapon && (flags & ClassRecovery) != 0;
    item.classFlags = flags;
    return item;
}
}

uint32_t ItemClassFlags(const Layout& layout, uint8_t id)
{
    const uint32_t at = layout.items + ItemSize * id;
    const uint8_t itemClass = rd8(at + ItemClass);
    return id == 0 || rd8(at + ItemThere) == 0 || itemClass == NoClass ? 0 : rd32(layout.itemClasses + ItemClassSize * itemClass);
}

World ReadWorld(const Layout& layout)
{
    World world{};
    world.currentRoom = static_cast<int16_t>(rd16(layout.currentRoom));
    constexpr uint32_t RoomSize = 0xA4, NodeSize = 0x40, NodeInUse = 0, NodePosition = 4;
    const uint32_t room = layout.roomNodes + RoomSize * static_cast<uint16_t>(world.currentRoom);
    const uint32_t count = rd32(room), nodes = rd32(room + 4);
    constexpr uint32_t MainMemory = 32u << 20; // a room not set up: no table (its word 0) or not one in main memory
    const bool table = nodes != 0 && nodes < MainMemory && count <= MainMemory / NodeSize;
    for (uint32_t n = 0; table && n < count && world.nodeCount < MaxNodes; n++)
    {
        if (rd8(nodes + NodeSize * n + NodeInUse) != 0)
        {
            world.nodes[static_cast<size_t>(world.nodeCount++)] = ReadVec(nodes + NodeSize * n + NodePosition);
        }
    }
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
        c.hand = ReadItem(layout, rd8(at + layout.weapon));
        for (int slot = 0; slot < PackSize; slot++)
        {
            c.pack[slot] = ReadItem(layout, rd8(at + layout.pack + static_cast<uint32_t>(slot)));
        }
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
        e.setAside = (rd32(at + layout.enemyFlags) & SetAside) != 0;
    }
    return world;
}
}
