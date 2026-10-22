// Skip intro movies (intro.h).
//
// The games' start-up sequences are tasks of the demo overlay that wait while their picture or movie runs, calling one START
// check each frame (File 2's 0x54F250, File 1's 0x540CB0, the same word for word): it reads the task's pad copy (+82) and,
// with START (0x8000) down, sets the task's word +92 to 1 and returns 1. A task that sees +92 set fades out over 20 frames
// and goes on to what follows, as a press of START does (seen: START at 22 s into File 2's boot reaches the title).
// Skipping is that check answering "pressed" for the start-up sequences' calls: File 2's opening movie (task 0x54FE60, the
// call returning to 0x54FFF0: PS2opening.sfd, CAPCOM Presents and the credits) and its title (0x54FB00, 0x54F914); File 1's
// CAPCOM logo (0x540E30, 0x540EA0) and its title (0x540F30, 0x5410BC). Those are the only calls a boot left in the main menu
// for 90 s made; the overlay's other sequences keep their own check. A boot reaches the main menu at 20 s instead of after
// File 2's three and a half minute opening (File 1's at 25 s, at its title). The warning and the Sofdec and ADX logos before
// them don't ask for START: they stay.
//
// Ending the opening's movie itself instead (0x34D560, the player's end, as the game does when the disc hasn't the movie's
// file) isn't enough: its task then waits out the movie's length (6210 frames) on a black screen.
#include "intro.h"

#include "mod_settings.h"

namespace Intro
{
const Layout File1 = {{0x540EA0, 0x5410BC}};
const Layout File2 = {{0x54FFF0, 0x54F914}};

namespace
{
// The sequence task's "START was pressed" word, both games'
constexpr uint32_t StartFlag = 92;
const char* const OffOn[] = {"Off", "On", nullptr};
int g_row = -1;

bool FromStartUp(const Ps2Cpu* c, const Layout& layout)
{
    const uint32_t from = c->r[31].uw[0];
    return from == layout.startUpCalls[0] || from == layout.startUpCalls[1];
}
}

void AddSettings()
{
    g_row = ModSettings::Add({"Gameplay", nullptr, "Skip intro movies",
                              "Start at the main menu: no opening movie or title screen", OffOn, 0});
}

void StartPressed(Ps2Cpu* c, void (*check)(Ps2Cpu*), const Layout& layout)
{
    if (g_row < 0 || ModSettings::Choice(g_row) == 0 || !FromStartUp(c, layout))
    {
        check(c);
        return;
    }
    wr32(c->r[4].uw[0] + StartFlag, 1);
    c->r[2].sd[0] = 1;
}
}
