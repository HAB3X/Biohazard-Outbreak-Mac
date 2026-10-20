// Biohazard Outbreak File 2 for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "network.h"
#include "partner_ai.h"
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

// A key of the USB keyboard in the software keyboard: the Mac's keys type Latin characters, not romaji (src/typing.cpp)
extern "C" void netwk_0059fef0(Ps2Cpu* c);
extern "C" void netwk_00555080(Ps2Cpu* c);
extern "C" void netwk_00555660(Ps2Cpu* c);
extern "C" void netwk_00555350(Ps2Cpu* c);
extern "C" void netwk_00555480(Ps2Cpu* c);
extern "C" void netwk_00554ee0(Ps2Cpu* c);
extern "C" void netwk_005a25d0(Ps2Cpu* c);
extern "C" void netwk_005b6c80(Ps2Cpu* c);
extern "C" void netwk_00552e80(Ps2Cpu* c);
static const Typing::SoftKeyboard g_softKeyboard{-0x5DDC, 0x668, netwk_00555080, netwk_00555660,
                                                    {netwk_00555350, netwk_00555480, netwk_00554ee0, netwk_005a25d0},
                                                    netwk_005b6c80, netwk_00552e80};
extern "C" void netwk_usb_key(Ps2Cpu* c) { Typing::UsbKey(c, g_softKeyboard, netwk_0059fef0); }

// netwk's connection setup step: the configuration list skipped (src/network.cpp)
extern "C" void netwk_005bfd40(Ps2Cpu* c);
extern "C" void netwk_setup_step(Ps2Cpu* c) { Network::SetupStep(c, Network::File2Setup, netwk_005bfd40); }

// The partners' think and its perception, SINGLE.BIN's (src/partner_ai.cpp)
extern "C" void single_007e99d0(Ps2Cpu* c);
extern "C" void single_partner_think(Ps2Cpu* c) { PartnerAi::Think(c, PartnerAi::File2, single_007e99d0); }
extern "C" void single_007e9ad0(Ps2Cpu* c);
extern "C" void single_partner_perceive(Ps2Cpu* c) { PartnerAi::Perceive(c, PartnerAi::File2, single_007e9ad0); }
extern "C" void single_007e3aa0(Ps2Cpu* c);
extern "C" void single_partner_pick_item(Ps2Cpu* c) { PartnerAi::PickItem(c, PartnerAi::File2, single_007e3aa0); }

int main(int argc, char** argv)
{
    static Ps2GameInfo game = {"Biohazard Outbreak File 2", "SLPM_656.92", 1356096};
    // Its 3D masks red's lowest bit (FBMSK 1), which nothing reads back or shows: replays of File 1's bar, File 2's Training
    // Ground and its zoo drew every picture the same without it, 3 to 35 times faster (runtime.h's gsLooseFrameMasks)
    game.gsLooseFrameMasks = 1;
    // Its text is drawn a glyph at a time, each a region of its texture (REGION_CLAMP) blended over its shadow: each one a
    // shader blend on a copy of the frame, a command buffer of its own. GL's blending draws it with the text's edges a few
    // steps of 255 different (the replays of the character and partner selects: at most 4, on 3% of the pixels, all on the
    // text) and nothing else; the selects went from 7-22 frames a second to 30 (runtime.h's gsRoundedRegionBlends)
    game.gsRoundedRegionBlends = 1;
    // VIF1 and VU1 on a thread of their own (runtime.h's vu1Thread): its VIF1 codes have no interrupt bit, and VU1 was most of
    // the EE's time in its 3D
    game.vu1Thread = 1;
    // Drawn by Metal at 4x unless the player chooses otherwise (OpenGL where there's no Apple GPU): Metal makes the GS's rules
    // exactly, and its replay of the heaviest scene (the zoo with zombies) takes a quarter to two fifths of an M4's GPU at 4x, an
    // estimated half to two thirds of a base M1's (docs/PERFORMANCE.md); a 1280 x 960 window on a Retina screen shows 4x's
    // picture at nearly its own pixels
    game.gsRenderer = "metal";
    game.gsScale = 4;
    BiohazardControls::Apply(game, BiohazardControls::File2);
    game.networkDns = Network::ObsrvDns;
    game.settings = PartnerAi::Settings;
    game.settingCount = PartnerAi::SettingCount;
    game.typing = Typing::Typing;
    Tsnd::Register();
    Network::RegisterLoadHigh();
    Network::RegisterUsbSearcher();
    Network::RegisterAveWrapper();
    return ps2_main(argc, argv, &game);
}
