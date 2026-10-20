// The network configuration on the Mac. The game looks for "Your Network Configuration" on a memory card (the BWNETCNF
// directory a network setup tool writes) before it asks netcnf for the configurations in it. On the Mac the network is
// the Mac's, which the runtime's netcnf presents as the one configuration, so no card holds one: the card's check is
// replaced by "there", and the list, its entry and its settings come from the runtime (iop/netcnf.cpp there).
#include "network.h"

#include "ps2/recomp.h"

// netwk's check of the chosen card for the BWNETCNF directory (File 2's 0x5C84F0, called in state 4 of the Network Play
// setup's step, 0x5C0410; File 1's 0x5D0C00, the same code): -2 while libmc2 is asked, -1 when the directory is there (the
// setup goes on to read the configurations), any other value an error of the card's
extern "C" void netwk_card_has_netcnf(Ps2Cpu* c) { c->r[2].sd[0] = -1; }

namespace Network
{
namespace
{
// The setup screen's step (0x5BFD40 in File 2): its state, as the step numbers them
enum Step : u32
{
    StepOpen = 0,         // the screen made, then 1 (the list's way) or 2 ("same connection as last time")
    StepList = 5,         // the configuration list (0x5C0970) with its own state, below
    StepQuitDialog = 7,   // "stop connecting?": yes leaves Network Play, no goes back to the list
};
// The work area's bytes the list uses (0x5C0970)
constexpr u32 ListCursor = 0x10;     // the row: 0 edit the network settings, 1 the proxy, 2 on the configurations
constexpr u32 ListChoice = 0x13;     // the configuration (the row - 2), and the first one shown
constexpr u32 ListScroll = 0x14;
constexpr u32 DialogChoice = 0x1F;   // a yes/no dialog's choice
constexpr u32 ListState = 0x20;      // 0 made, 1 choosing, 0x12 connecting with the chosen configuration
constexpr u32 ListCount = 0x24;      // the configurations read (0x5C0410)
constexpr u32 Message = 0x4E;        // the message shown
constexpr u8 ListMade = 0, ListChoosing = 1, ListConnect = 0x12;
constexpr u8 QuitMessage = 0x1A;     // the list's own "stop connecting?" (its Back, the step's state 7)

// A connection tried from this screen since it opened
bool g_tried = false;
}

// The connection list skipped. On the console the list offers the network settings' editor (Sony's setup program, which the
// game starts with LoadExecPS2 after shutting its own pads and sound down: a program the Mac doesn't have, where the game
// stops), the proxy's, and the card's configurations; on the Mac there's one, the Mac's network (the runtime's netcnf). So
// when the list would come up the game is put where choosing that configuration puts it (the list's state 0x12, which
// connects, saves it as the last connection and leaves the screen), as if it had been chosen. If that connection fails the
// game's error is shown and the list would come back: the list's own "stop connecting?" dialog is shown instead (yes leaves
// Network Play, no tries again), so the editor is never reached.
void SetupStep(Ps2Cpu* c, const SetupScreen& screen, void (*step)(Ps2Cpu*))
{
    const u32 gp = c->r[28].uw[0];
    const u32 stepAt = gp + static_cast<u32>(screen.step);
    const u32 before = rd32(stepAt);
    if (before == StepOpen)
    {
        g_tried = false;
    }
    step(c);
    const u32 work = rd32(gp + static_cast<u32>(screen.work));
    const u8 list = rd8(work + ListState);
    if (rd32(stepAt) != StepList || (list != ListMade && list != ListChoosing) || static_cast<s8>(rd8(work + ListCount)) < 1)
    {
        return;
    }
    if (!g_tried || before == StepQuitDialog)
    {
        g_tried = true;
        wr8(work + ListCursor, 2);
        wr8(work + ListChoice, 0);
        wr8(work + ListScroll, 0);
        wr8(work + ListState, ListConnect);
        return;
    }
    // As the list's Back does (0x5C0970 returning -1 to the step)
    wr32(stepAt, StepQuitDialog);
    wr8(work + ListState, ListMade);
    wr8(work + Message, QuitMessage);
    wr8(work + ListCursor, 0);
    wr8(work + DialogChoice, 0);
}
}
