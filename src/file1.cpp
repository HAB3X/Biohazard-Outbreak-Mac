// Biohazard Outbreak for Mac: the recompiled executable and its overlays on the PS2 runtime
#include "cheats.h"
#include "controls.h"
#include "modern_camera.h"
#include "mods.h"
#include "network.h"
#include "partner_ai.h"
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

// The partners' think and its perception, SINGLE.BIN's (src/partner_ai.cpp)
extern "C" void single_00822440(Ps2Cpu* c);
extern "C" void single_partner_think(Ps2Cpu* c) { PartnerAi::Think(c, PartnerAi::File1, single_00822440); }
extern "C" void single_00822520(Ps2Cpu* c);
extern "C" void single_partner_perceive(Ps2Cpu* c) { PartnerAi::Perceive(c, PartnerAi::File1, single_00822520); }

// The Modern camera (src/modern_camera.cpp): the game frame, the button mapper and the game's camera handlers, as recompiled
extern "C" void f_0019ecb0(Ps2Cpu* c);
extern "C" void mods_frame_end(Ps2Cpu* c) { Mods::FrameEnd(c, f_0019ecb0); }
extern "C" void f_001afc60(Ps2Cpu* c);
extern "C" void mods_pad_map(Ps2Cpu* c) { ModernCamera::PadMap(c, f_001afc60); }
extern "C" void game_0058ec70(Ps2Cpu* c);
extern "C" void game_camera_fixed_cut(Ps2Cpu* c) { ModernCamera::FixedCut(c, game_0058ec70); }
extern "C" void game_0058ed80(Ps2Cpu* c);
extern "C" void game_camera_rail_cut(Ps2Cpu* c) { ModernCamera::RailCut(c, game_0058ed80); }
extern "C" void game_0058f230(Ps2Cpu* c);
extern "C" void game_camera_spline_cut(Ps2Cpu* c) { ModernCamera::RailCut(c, game_0058f230); }
extern "C" void game_005a9080(Ps2Cpu* c);
extern "C" void game_camera_follow(Ps2Cpu* c) { ModernCamera::Follow(c, game_005a9080); }
extern "C" void game_00556800(Ps2Cpu* c);
extern "C" void game_gun_elevation(Ps2Cpu* c) { ModernCamera::GunElevation(c, game_00556800); }

// The cheats (src/cheats.cpp): the functions whose code the cheats' codes patched, as recompiled
#define CHEAT_OVERRIDE(name, function, original)                                                                               \
    extern "C" void original(Ps2Cpu* c);                                                                                      \
    extern "C" void name(Ps2Cpu* c) { Cheats::function(c, original); }
CHEAT_OVERRIDE(cheat_single_play, SinglePlay, f_001af4d0)
CHEAT_OVERRIDE(cheat_ammo, Ammo, game_00581a00)
CHEAT_OVERRIDE(cheat_item_use, ItemUse, game_00581b30)
CHEAT_OVERRIDE(cheat_gauge_over_time, GaugeOverTime, game_006359d0)
CHEAT_OVERRIDE(cheat_gauge_bite, GaugeBite, game_0062e6a0)
CHEAT_OVERRIDE(cheat_enemy_damage, EnemyDamage, game_0060f0c0)
CHEAT_OVERRIDE(cheat_player_damage, PlayerDamage, game_0062f130)
CHEAT_OVERRIDE(cheat_death_check, DeathCheck, game_0062ee10)
CHEAT_OVERRIDE(cheat_movement, Movement, game_0062ca30)
CHEAT_OVERRIDE(cheat_timers, Timers, game_0065a8c0)
CHEAT_OVERRIDE(cheat_collision, Collision, game_00631ca0)
CHEAT_OVERRIDE(cheat_available, Available, demo_00554ec0)
CHEAT_OVERRIDE(cheat_scenario_select_a, ScenarioSelect, demo_00542640)
CHEAT_OVERRIDE(cheat_scenario_select_b, ScenarioSelect, demo_0054dc10)
CHEAT_OVERRIDE(cheat_scenario_select_c, ScenarioSelect, demo_0054e2f0)
CHEAT_OVERRIDE(cheat_scenario_select_d, ScenarioSelect, demo_0054ee40)

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
    Mods::Install(game, ModernCamera::File1, BiohazardControls::File1, Cheats::File1);
    Tsnd::Register();
    Network::RegisterLoadHigh();
    Network::RegisterUsbSearcher();
    Network::RegisterAveWrapper();
    return ps2_main(argc, argv, &game);
}
