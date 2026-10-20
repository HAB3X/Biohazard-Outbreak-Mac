// Biohazard Outbreak File 2 for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "network.h"
#include "ps2/runtime.h"
#include "tsnd.h"
#include "typing.h"

#include "ps2/recomp.h"

// netwk's software keyboard's step (src/typing.cpp), its body as recompiled
extern "C" void netwk_00552f10(Ps2Cpu* c);
extern "C" void netwk_soft_keyboard_step(Ps2Cpu* c)
{
    Typing::SoftKeyboardStep();
    netwk_00552f10(c);
}

int main(int argc, char** argv)
{
    static Ps2GameInfo game = {"Biohazard Outbreak File 2", "SLPM_656.92", 1356096};
    // Its 3D masks red's lowest bit (FBMSK 1), which nothing reads back or shows: replays of File 1's bar, File 2's Training
    // Ground and its zoo drew every picture the same without it, 3 to 35 times faster (runtime.h's gsLooseFrameMasks)
    game.gsLooseFrameMasks = 1;
    BiohazardControls::Apply(game, BiohazardControls::File2);
    game.networkDns = Network::ObsrvDns;
    game.typing = Typing::Typing;
    Tsnd::Register();
    Network::RegisterLoadHigh();
    Network::RegisterUsbSearcher();
    Network::RegisterAveWrapper();
    return ps2_main(argc, argv, &game);
}
