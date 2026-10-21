// The cheats: the regular release's and the Greatest Hits' cheat lists for Resident Evil Outbreak, as RaccoonRecomp decoded
// and checked them (REO-Cheats, REO-GreatestHits-Cheats), merged into one list without duplicates and moved to this disc.
// Each is a row of the PC menus' Cheats tab, off until chosen, with its codes' authors credited. A cheat that writes data
// writes it to the game's memory each game frame (and puts back what it held when it's switched off); a cheat that patched
// the game's code is a native override of that function (config/fileN.toml), which runs the game's own and does what the
// patch did while the cheat is on. Cheats act only in single play: in network play every one is off and every override runs
// the game's function unchanged, so play with PS2 players is untouched. docs/mods.md has each cheat's evidence.
//
// Adapted from REO-Cheats and REO-GreatestHits-Cheats by RaccoonRecomp (MIT); the codes by Codejunkies, Code Master, Lajos
// Szalay, Jay007, VirusPunk, Jarnold83, MadCatz, bungholio and 47iscool.
#pragma once

#include "ps2/recomp.h"

#include <cstdint>

namespace Cheats
{
// A character record's fields the cheats write (0: not known in this game, its rows not offered)
struct CharacterFields
{
    uint32_t hp, hpMax;               // s16 each
    uint32_t speed0, scale;           // f32: Codejunkies' first speed factor; the model's three scale factors
    uint32_t speed1, speed2, damage;  // f32: bungholio's speed #1, the second speed factor (#2), the damage multiplier
    uint32_t characterType, animation; // u8
    uint32_t status, unnoticed, untouchable, rapidFire; // bytes of the flags word (their bits differ between the games)
    uint32_t gauge, gauge2;           // u32: the virus gauge, bungholio's virus value #2
    uint32_t model;                   // u16
    uint32_t altCostume;              // u8
    uint32_t wallsA, wallsB;          // u32 each: the collision's two words bungholio's code fills
};

// Where one game keeps what the cheats change (docs/mods.md); 0: not in this game (its cheats not offered)
struct Layout
{
    const char* name;
    uint32_t playMode;        // u8: 0 single play, 1 network play
    uint32_t characters;      // the characters' records (the player's first, then the CPU partners)
    uint32_t characterSize;
    uint32_t items;           // the item records, 60 bytes each (+0x24 the count, s16)
    uint32_t padFrames;       // the word pointing at port 0's DualShock frames
    uint32_t gaugeMaxima;     // u32 per character type: the virus gauge's maximum (100 %)
    uint32_t results;         // the scenario's results: the result points (u32), its flags after (docs/mods.md)
    uint32_t files;           // u16: the scenario's files found
    uint32_t save;            // the save block's start: the collection's points (u32)
    uint32_t scenarios;       // u32: the scenarios that can be chosen (bit 0 set once the save data is made or loaded)
    uint32_t available;       // the collection's "can be bought" bits (6 words), then its "owned" bits at owned
    uint32_t owned;
    // Two of the game's calls told apart by their return address: the death's single-play check (its YOU DIED path) and the
    // enemies' update's call of the movement routine
    uint32_t deathOfflineReturn;
    uint32_t enemyMoveReturn;
    CharacterFields field;
};

extern const Layout File1;
extern const Layout File2;

void Use(const Layout& layout);
// The Cheats tab's rows (src/mod_settings.h)
void AddSettings();
// Once a game frame, at its end (src/mods.h): the data cheats written
void Frame();

// The overrides (each `original` the game's function as recompiled)
void Ammo(Ps2Cpu* c, Ps2Function original);          // a round used (a0 the item)
void ItemUse(Ps2Cpu* c, Ps2Function original);       // an item used (a0 the item)
void GaugeOverTime(Ps2Cpu* c, Ps2Function original); // the virus gauge's rise with time (a0 a character)
void GaugeBite(Ps2Cpu* c, Ps2Function original);     // a bite's infection (a0 a character)
void EnemyDamage(Ps2Cpu* c, Ps2Function original);   // a hit's damage to an enemy (a0 the enemy)
void PlayerDamage(Ps2Cpu* c, Ps2Function original);  // an enemy attack's damage to a character (a0 the character)
void DeathCheck(Ps2Cpu* c, Ps2Function original);    // the death check (a0 a character)
void SinglePlay(Ps2Cpu* c, Ps2Function original);    // "is this single play" (the executable's)
void Movement(Ps2Cpu* c, Ps2Function original);      // the movement routine (a0 a character or an enemy)
void Timers(Ps2Cpu* c, Ps2Function original);        // the event timers' countdown
void Collision(Ps2Cpu* c, Ps2Function original);     // a character's collision with the room
void Available(Ps2Cpu* c, Ps2Function original);     // the collection's "can this be bought" check
void ScenarioSelect(Ps2Cpu* c, Ps2Function original); // the scenario select's functions reading the unlocked scenarios
}
