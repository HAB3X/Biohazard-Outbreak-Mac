// Biohazard Outbreak for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "network.h"
#include "ps2/runtime.h"
#include "tsnd.h"
#include "typing.h"

#include "ps2/recomp.h"

// netwk's software keyboard's step (src/typing.cpp), its body as recompiled
extern "C" void netwk_00553820(Ps2Cpu* c);
extern "C" void netwk_soft_keyboard_step(Ps2Cpu* c)
{
    Typing::SoftKeyboardStep();
    netwk_00553820(c);
}

int main(int argc, char** argv)
{
    static Ps2GameInfo game = {"Biohazard Outbreak", "SLPM_654.28", 1294104};
    // Its 3D masks red's lowest bit (FBMSK 1), which nothing reads back or shows: replays of File 1's bar, File 2's Training
    // Ground and its zoo drew every picture the same without it, 3 to 35 times faster (runtime.h's gsLooseFrameMasks)
    game.gsLooseFrameMasks = 1;
    // Its text is drawn a glyph at a time, each a region of its texture (REGION_CLAMP) blended over its shadow: each one a
    // shader blend on a copy of the frame, a command buffer of its own. GL's blending draws it with the text's edges a few
    // steps of 255 different (the replay of the character select: at most 4, all on the text) and nothing else; the select
    // went from 21-23 frames a second to 30 (runtime.h's gsRoundedRegionBlends)
    game.gsRoundedRegionBlends = 1;
    // VIF1 and VU1 on a thread of their own (runtime.h's vu1Thread): its VIF1 codes have no interrupt bit, and VU1 was most of
    // the EE's time in its 3D
    game.vu1Thread = 1;
    BiohazardControls::Apply(game, BiohazardControls::File1);
    game.networkDns = Network::ObsrvDns;
    game.typing = Typing::Typing;
    Tsnd::Register();
    Network::RegisterLoadHigh();
    Network::RegisterUsbSearcher();
    Network::RegisterAveWrapper();
    return ps2_main(argc, argv, &game);
}
