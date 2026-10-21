// Biohazard Outbreak for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "controls.h"
#include "menus.h"
#include "network.h"
#include "partner_ai.h"
#include "pointer.h"
#include "text_map.h"
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

// A key of the USB keyboard in the software keyboard: the Mac's keys type Latin characters, not romaji (src/typing.cpp)
extern "C" void netwk_005a51c0(Ps2Cpu* c);
extern "C" void netwk_00555860(Ps2Cpu* c);
extern "C" void netwk_00555e50(Ps2Cpu* c);
extern "C" void netwk_00555b40(Ps2Cpu* c);
extern "C" void netwk_00555c70(Ps2Cpu* c);
extern "C" void netwk_005556c0(Ps2Cpu* c);
extern "C" void netwk_005a75b0(Ps2Cpu* c);
extern "C" void netwk_005be590(Ps2Cpu* c);
extern "C" void netwk_00553790(Ps2Cpu* c);
static const Typing::SoftKeyboard g_softKeyboard{-0x5C20, 0x664, netwk_00555860, netwk_00555e50,
                                                    {netwk_00555b40, netwk_00555c70, netwk_005556c0, netwk_005a75b0},
                                                    netwk_005be590, netwk_00553790};
extern "C" void netwk_usb_key(Ps2Cpu* c) { Typing::UsbKey(c, g_softKeyboard, netwk_005a51c0); }

// netwk's connection setup step: the configuration list skipped (src/network.cpp)
extern "C" void netwk_005c88c0(Ps2Cpu* c);
extern "C" void netwk_setup_step(Ps2Cpu* c) { Network::SetupStep(c, Network::File1Setup, netwk_005c88c0); }

// The lobby's cursor engines: the cursors the game moves each frame kept for the pointer (src/menus_lobby.cpp)
extern "C" void netwk_005432f0(Ps2Cpu* c);
extern "C" void netwk_menu_cursor(Ps2Cpu* c) { Menus::LobbyCursor(c, Menus::LobbyEngine::Byte, netwk_005432f0); }
extern "C" void netwk_00543610(Ps2Cpu* c);
extern "C" void netwk_menu_cursor_held(Ps2Cpu* c) { Menus::LobbyCursor(c, Menus::LobbyEngine::Byte, netwk_00543610); }
extern "C" void netwk_00543840(Ps2Cpu* c);
extern "C" void netwk_menu_cursor_held_fast(Ps2Cpu* c) { Menus::LobbyCursor(c, Menus::LobbyEngine::Byte, netwk_00543840); }
extern "C" void netwk_00543480(Ps2Cpu* c);
extern "C" void netwk_menu_cursor_word(Ps2Cpu* c) { Menus::LobbyCursor(c, Menus::LobbyEngine::Half, netwk_00543480); }
extern "C" void netwk_00543ac0(Ps2Cpu* c);
extern "C" void netwk_list_cursor(Ps2Cpu* c) { Menus::LobbyCursor(c, Menus::LobbyEngine::List, netwk_00543ac0); }

// The partners' think and its perception, SINGLE.BIN's (src/partner_ai.cpp)
extern "C" void single_00822440(Ps2Cpu* c);
extern "C" void single_partner_think(Ps2Cpu* c) { PartnerAi::Think(c, PartnerAi::File1, single_00822440); }
extern "C" void single_00822520(Ps2Cpu* c);
extern "C" void single_partner_perceive(Ps2Cpu* c) { PartnerAi::Perceive(c, PartnerAi::File1, single_00822520); }
extern "C" void single_00833250(Ps2Cpu* c);
extern "C" void single_partner_pick_item(Ps2Cpu* c) { PartnerAi::PickItem(c, PartnerAi::File1, single_00833250); }

// The executable's text functions: where each string is drawn is kept for the pointer (src/text_map.h)
extern "C" void f_001b43c0(Ps2Cpu* c);
extern "C" void f_001b5fb0(Ps2Cpu* c);
extern "C" void text_draw(Ps2Cpu* c) { TextMap::Draw(c, f_001b43c0); }
extern "C" void text_draw_net(Ps2Cpu* c) { TextMap::Draw(c, f_001b5fb0); }

// The main loop's frame, then the menus' sounds the pointer owes them (src/pointer.h)
extern "C" void f_001b9070(Ps2Cpu* c);
extern "C" void game_frame(Ps2Cpu* c)
{
    f_001b9070(c);
    Pointer::Frame(c);
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
    // Drawn by Metal at 4x unless the player chooses otherwise (OpenGL where there's no Apple GPU): Metal makes the GS's rules
    // exactly, and its replay of the heaviest scene (the zoo with zombies) takes a quarter to two fifths of an M4's GPU at 4x, an
    // estimated half to two thirds of a base M1's (docs/PERFORMANCE.md); a 1280 x 960 window on a Retina screen shows 4x's
    // picture at nearly its own pixels
    game.gsRenderer = "metal";
    game.gsScale = 4;
    BiohazardControls::Apply(game, BiohazardControls::File1);
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
