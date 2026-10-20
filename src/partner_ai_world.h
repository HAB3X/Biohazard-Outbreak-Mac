// What the partners' AI sees of the game: the characters and enemies as plain values, read from the game's records once a
// frame (partner_ai.h's Layout says where they are). The AI decides on these, not on the game's memory, so its logic can be
// tested on made-up worlds
#pragma once

#include "partner_ai.h"

#include <array>
#include <cstdint>

namespace PartnerAi
{
struct Vec
{
    float x, y, z;
};

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
    bool alive() const { return active && hp > 0; }
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
float Distance(const Vec& a, const Vec& b);
}
