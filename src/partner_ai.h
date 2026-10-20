// The partners' AI in single play (docs/research/partner-ai.md, docs/adr/0002-partner-ai.md). The game runs each CPU partner's
// "think" (SINGLE.BIN's, called once a frame for each character whose CPU flag is set) before the characters' update; the
// think writes the partner's own virtual pad (the buttons and sticks the character's update reads, as it reads the player's
// pad for the player) and its own work area. This module stands in for that think: it runs the game's own, and measures
// what the partners do (telemetry, below). Online play has no CPU partners and doesn't load SINGLE.BIN (NETAQ.BIN is there),
// so nothing here runs online.
#pragma once

#include "ps2/recomp.h"

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
    uint32_t hp;              // s16 field: hit points, then the most there can be (hp + 2)
    uint32_t motion;          // u8 field: what the character's doing (its action's number)
    uint32_t cpu;             // u8 field: nonzero for a partner the game's AI plays
    uint32_t padPressed;      // u32 field: the virtual pad's buttons pressed this frame, then released (+4) and held (+8)
    uint32_t aiState;         // u16 field: the AI's plan (its index into SINGLE.BIN's plan table), then its move (+4)
    uint32_t aiAction;        // u8 field: the AI's action routine (its index into the action table)
    uint32_t aiTarget;        // u8 field: the enemy the AI has picked (its index, 0xFF none)
    uint32_t speech;          // u8 field: the code of the line the character is saying (0 none): the player's commands
};

extern const Layout File1;
extern const Layout File2;

// The think override: `c`'s a0 is the partner's record; `original` is the game's think as recompiled
void Think(Ps2Cpu* c, const Layout& layout, Ps2Function original);
}
