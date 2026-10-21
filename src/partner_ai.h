// The partners' AI in single play (docs/research/partner-ai.md, docs/adr/0002-partner-ai-director.md). The game runs each CPU
// partner's "think" (SINGLE.BIN's, called once a frame for each character whose CPU flag is set) before the characters'
// update; the think writes the partner's own virtual pad (the buttons and sticks the character's update reads, as it reads
// the player's pad for the player) from its perception, its planner (plans picked by wants) and its routines. This module
// stands in for the think and its perception: each runs the game's own; after the perception, a native director decides
// what the partner should be doing (src/partner_ai_brain.h) and puts it into the game AI's wants, plan, target and item
// check; after the think, the partners are measured (src/partner_ai_telemetry.h). Online play has no CPU partners and
// doesn't load SINGLE.BIN (NETAQ.BIN is there), so nothing here runs online.
#pragma once

#include "ps2/recomp.h"
#include "ps2/runtime.h"

#include <cstdint>

namespace PartnerAi
{
// Where one game keeps what the partners' AI reads and writes. The characters' records (the player first, then the
// partners) and the enemies' share their first fields' layout (active +0, id +3, position +0x38, facing +0x92, room
// +0x1E2: the same in both games); the fields after differ between the games
struct Layout
{
    const char* name;
    uint32_t characters;      // the characters' records: 8, the player's first
    uint32_t characterSize;
    uint32_t enemies;         // the enemies' records: 50
    uint32_t enemySize;
    uint32_t currentRoom;     // s16: the room the game shows (the player's)
    uint32_t playMode;        // u8: 0 single play (SINGLE.BIN loaded), 1 network play (NETAQ.BIN)
    uint32_t enemyFlags;      // u32 field of an enemy's: 0x100 set aside (not in the AI's lists: the bar's dead customers)
    uint32_t hp;              // s16 field: hit points, then the most there can be (hp + 2)
    uint32_t motion;          // u8 field: what the character's doing (its action's number)
    uint32_t cpu;             // u8 field: nonzero for a partner the game's AI plays
    uint32_t padPressed;      // u32 field: the virtual pad's buttons pressed this frame, then released (+4) and held (+8)
    uint32_t aiState;         // u16 field: the AI's plan (its index into SINGLE.BIN's plan table), then its move (+4)
    uint32_t planStep;        // u8 field: the plan's own step (the fight plan's: 0 setting out, 2 picking its attack, 3 attacking)
    uint32_t fightFlags;      // two u32 fields: the fight plan's flags, cleared as it starts an attack
    uint32_t threatLevel;     // u8 field: the perception's threat level of its enemy, which the fight plan keeps (fightThreat)
    uint32_t fightThreat;
    uint32_t aiAction;        // u8 field: the AI's action routine (its index into the action table)
    uint32_t aiTarget;        // u8 field: the enemy the AI has picked (its index, 0xFF none)
    uint32_t aiTargetRecord;  // u32 field: the enemy the AI's fight is against (its record)
    uint32_t desires;         // 8 u8 fields: the AI's wants, the highest picks its next plan
    uint32_t weapon;          // u8 field: the item in hand (an index into the items, 0 none)
    uint32_t pack;            // 9 u8 fields: the items carried (four slots, the character's own item, four in it)
    uint32_t items;           // the items, 0x3C bytes each: +0 there, +0x24 rounds (s16), +0x2B its class (0xFF none)
    uint32_t itemClasses;     // the item classes, 0x18 bytes each: flags first (src/partner_ai_world.cpp)
    uint32_t itemChecks;      // u16 field: what the AI's item check (checkItems) looks at (4: a better weapon to hold)
    uint32_t checkItems;      // the AI's item check (a0 the record): starts the plan that picks up, swaps or uses an item
    uint32_t speech;          // u8 field: the code of the line the character is saying (0 none): the player's commands
    uint32_t setPlan;         // the AI's function starting a plan (a0 the record, a1 the plan; 0 has the next picked)
    uint32_t setMove;         // its function starting a move (a0 the record, a1 the move; 0 stops)
    uint32_t roomNodes;       // the rooms' route nodes, 0xA4 bytes a room: the count, then the nodes (0x40 bytes each: +0 in use,
                              // +4 the position)
    uint32_t route;           // the AI's route code (a0 the record, a1 the point, a2 its room; -1: no way there)
    uint32_t routeGoal;       // three float fields: where the route goes (the route code copies its point there)
    uint32_t planLock;        // u16 field: frames before the plan may be cut short by the game's own events
};

extern const Layout File1;
extern const Layout File2;

// The think override: `c`'s a0 is the partner's record; `original` is the game's think as recompiled
void Think(Ps2Cpu* c, const Layout& layout, Ps2Function original);
// The think's perception override (File 1 0x822520, File 2 0x7E9AD0): the game's, then this module's decision put into the
// game AI's wants, plan and target (the setting below choosing it)
void Perceive(Ps2Cpu* c, const Layout& layout, Ps2Function original);

// The AI's choice of an item lying in the room to pick up (File 1 0x833250, File 2 0x7E3AA0; a0 the record, a3 where the
// item's object goes, the result nonzero when one was picked: the item check then starts the pick-up plan): the game's, and with
// this module's AI an item that isn't a consumable (a key, an event item, a file) is left for the player
void PickItem(Ps2Cpu* c, const Layout& layout, Ps2Function original);

// The PC menus' Gameplay setting: the partners' AI, this module's (the default) or the game's own
extern const Ps2GameSetting Settings[];
extern const int SettingCount;
}
