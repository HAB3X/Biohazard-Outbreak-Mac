// The mods' rows in the PC menus (F10): each a setting of the runtime's (ps2/runtime.h's Ps2GameSetting) whose choice is kept
// here, read by the mods on the game's side at any time. The runtime saves each choice in options.txt by its tab, name and
// label, and sets it again at the game's first vertical blank. Rows are added once, before the game starts.
#pragma once

#include "ps2/runtime.h"

#include <vector>

namespace ModSettings
{
struct Row
{
    const char* tab;
    const char* group;        // the heading it's under (NULL: the one before's)
    const char* name;
    const char* description;
    const char* const* choices; // ending with NULL
    int initial;              // the choice before the player makes one
};

// A row added: its index for Choice (fails loudly past the capacity)
int Add(const Row& row);
// The row's choice now. Any thread
int Choice(int index);
// The runtime's settings: `others` (another module's own) then every row added, in the order added
std::vector<Ps2GameSetting> Settings(const Ps2GameSetting* others, int otherCount);
}
