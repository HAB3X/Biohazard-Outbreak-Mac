// What the partners' AI sees of the game: the characters and enemies as plain values, read from the game's records once a
// frame (partner_ai.h's Layout says where they are). The AI decides on these, not on the game's memory, so its logic can be
// tested on made-up worlds
#pragma once

#include "partner_ai.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace PartnerAi
{
struct Vec
{
    float x, y, z;
};

// An item a character holds or carries
struct Item
{
    uint8_t id = 0;      // its index in the game's items (0: none)
    bool weapon = false; // a weapon (a gun, a knife, a pipe...)
    bool fires = false;  // a gun
    bool heals = false;  // a recovery item (a herb, a spray)
    int rounds = 0;      // a gun's loaded rounds
    bool loadedGun() const { return id != 0 && fires && rounds > 0; }
};

constexpr int PackSize = 9; // four slots, the character's own item, then four in it (Cindy's herb case, a bag)

struct Character
{
    uint32_t address; // its record
    bool active;
    bool cpu;         // a partner the game's AI plays
    uint8_t id;       // 0 the player
    Vec position;
    uint16_t facing;  // 0x10000 a turn
    int16_t room;
    int hp, hpMax;
    uint8_t motion;
    uint32_t padPressed, padHeld;
    uint16_t plan, move;
    uint8_t action;
    uint8_t target;   // the enemy the AI has picked (0xFF none)
    uint8_t speech;   // the code of the line being said (0 none)
    Item hand;
    std::array<Item, PackSize> pack;
};

struct Enemy
{
    uint32_t address;
    bool active;
    Vec position;
    uint16_t facing;
    int16_t room;
    int hp, hpMax;
    uint32_t target;  // the character record it's after (0: none)
    bool setAside;    // not counted by the game's AI (the bar's dead customers before they rise)
    bool alive() const { return active && hp > 0 && !setAside; }
};

constexpr int CharacterCount = 8;
constexpr int EnemyCount = 50;

struct World
{
    int16_t currentRoom;
    std::array<Character, CharacterCount> characters;
    std::array<Enemy, EnemyCount> enemies;
    const Character& player() const { return characters[0]; }
};

World ReadWorld(const Layout& layout);
// The ground distance (x and z: y is up)
inline float Distance(const Vec& a, const Vec& b) { return std::hypot(a.x - b.x, a.z - b.z); }
// The facing from a toward b, as the game measures facings (0x10000 a turn, 0 toward +z, 0x4000 toward +x: the executable's
// 0x340140, atan2 of the x and z differences, and 0x1918C0's conversion)
inline uint16_t Heading(const Vec& from, const Vec& to)
{
    constexpr double Turn = 65536.0 / (2.0 * 3.14159265358979323846);
    const double a = std::atan2(static_cast<double>(to.x - from.x), static_cast<double>(to.z - from.z)) * Turn;
    return static_cast<uint16_t>(static_cast<int32_t>(std::lround(a)) & 0xFFFF);
}
}
