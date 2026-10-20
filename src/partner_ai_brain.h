// The partners' decisions: what each partner should be doing now (stay with the player, fight an enemy, or what the game's own
// AI picks), from the world and the player's commands. Pure: no game memory, so it's tested on made-up worlds
// (tests/partner_ai_brain_test.cpp). src/partner_ai.cpp turns a decision into the game AI's own plan and target.
#pragma once

#include "partner_ai_world.h"

#include <cstdint>

namespace PartnerAi
{
// The numbers the decisions turn on, in the game's units (a character is about 100 across; File 1's bar is about 2000 by
// 1500) and frames (30 a second)
struct Tunables
{
    float threatToPlayer = 900;  // an enemy after the player this close to the player is a threat
    float threatToPartner = 450; // an enemy after the partner this close to it is a threat
    float watchToPlayer = 900;   // any enemy this close to the player is worth shooting (a gun with rounds)
    float watchToPartner = 600;  // or this close to the partner
    float closeQuarters = 180;   // an enemy this close to the player is on them (biting, grabbing): the first to fight
    float followFar = 700;       // farther than this from the player, a partner comes back
    float followNear = 300;      // and stops coming back this close
    float comeFar = 260;         // the same while told "This way!" or called
    float comeNear = 150;
    float goThreat = 2000;       // while told "Go!": an enemy this close to the partner is fair game
    uint32_t orderFrames = 30 * 25; // how long a command holds
    float hurt = 0.5f;           // below this share of its hit points, a partner carrying a recovery item stops to use it
    float personalSpace = 200;   // an enemy this close to a partner that can't fight it (no loaded gun, or badly hurt) is
    float badlyHurt = 0.3f;      // stepped away from, unless it's on the player
    float evadeSlack = 1.6f;     // and on until it's this many times as far
    float shootRange = 1300;     // a partner with a loaded gun shoots its enemy itself within this (the game's attack
                                 // routines wander and wait first)
    uint16_t aimTolerance = 0x0B00; // facing within this of the enemy (about 15 degrees) is on target
    uint32_t fireFrames = 10;    // a shot every this many frames at most (the gun's own pace holds it back further)
    uint32_t missedShots = 6;    // presses of the attack without the target losing hit points before the partner stops
    uint32_t holdFireFrames = 30 * 8; // shooting itself, for this long (the game's own attack routine instead)
    uint32_t lookFrames = 90;    // how often a guarding partner's items are looked at (the game's plans do it on arriving)
    uint32_t retryFrames = 15;   // how soon the gun carried is asked for again (the game refuses while the partner's busy)
    float keepTarget = 0.7f;     // the enemy being fought is kept while it's worth this much of the worst one's score (a
                                 // switch restarts the attack), unless the worst is on the player and it isn't
};

// The player's commands (the appeal lines' codes in the speaker's record, docs/research/partner-ai.md)
enum class Command : uint8_t
{
    Go = 2,
    Thanks = 3,
    ThisWay = 4,
    HelpMe = 5,
    CallFirst = 9,  // the partner with id 1 by name
    CallSecond = 10, // id 2
};

enum class Order : uint8_t
{
    None,
    Come,  // "This way!", or called by name: stay close
    Go,    // "Go!": take on the enemies about
    Help,  // "Help me!": the player's enemies first, close by
};

enum class Intent : uint8_t
{
    Free,   // the game's own AI decides (items, doors, looking about)
    Follow, // go to the player and stay close
    Fight,  // attack `target`
    Guard,  // stay by the player, facing them, while enemies are about (no wandering off)
};

// The game AI's own item check asked for (it decides, by its own tables, and starts its plan for it)
enum class ItemCheck : uint8_t
{
    None,
    TakeOutGun, // a better weapon to hold: the loaded gun carried (enemies about, none in hand)
    Look,       // everything: use a recovery item when hurt enough, pick up, swap, reload
};

struct Decision
{
    Intent intent = Intent::Free;
    int target = -1;          // the enemy's index, for Fight
    const char* why = "";
    ItemCheck items = ItemCheck::None;
    bool urgent = false;      // the item check may cut a fight short (hurt, with a recovery item)
    uint32_t pad = 0;         // buttons held this frame instead of the game AI's (src/partner_ai_pad.h; 0: the game's)
    uint32_t pressed = 0;     // and pressed this frame (an attack)
};

// Shooting an enemy with the gun in hand: the partner turned to face it with the weapon down (right lowers the facing), then
// the weapon readied, and in the aiming stance the attack pressed every fireFrames frames
struct Buttons
{
    uint32_t held = 0, pressed = 0;
};


Buttons Aim(const Character& me, const Vec& target, uint64_t frame, const Tunables& tunables);

// The buttons that take a character away from a point: run off when facing away from it, step back when facing it, turn
// away in between (the game's own turning rule: right lowers the facing, File 1's 0x82BB30)
uint32_t StepAway(const Vec& position, uint16_t facing, const Vec& from);

// What a partner remembers between frames
struct Memory
{
    Order order = Order::None;
    uint64_t orderUntil = 0;
    uint8_t lastSpeech = 0;   // the player's line seen last frame (a new command is a change)
    bool following = false;   // coming back to the player (until followNear)
    int target = -1;          // the enemy being fought
    uint64_t lastLook = 0;    // the last item check asked for
    bool evading = false;     // stepping away from an enemy
    int shotTarget = -1;      // the enemy being shot at, its hit points last frame, the presses since it last lost some
    int shotTargetHp = 0;
    uint32_t shotsMissed = 0;
    uint64_t holdFireUntil = 0;
};

// What the partner has: a gun with rounds in it fights what threatens the player; anything else (a knife, a pipe, an empty
// gun, bare hands) only what's on the player. A loaded gun carried but not in hand is worth taking out
struct Abilities
{
    bool gun = false;
    bool spareGun = false;
    bool melee = false;
    bool recovery = false;    // a recovery item carried
};
Abilities AbilitiesOf(const Character& character);

Decision Decide(const World& world, int partner, const Abilities& abilities, Memory& memory, uint64_t frame,
                const Tunables& tunables);
}
