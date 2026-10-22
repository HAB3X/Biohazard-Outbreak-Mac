// The community mods' installation (mods.h)
#include "mods.h"

#include "mod_settings.h"
#include "modern_camera.h"
#include "modern_camera_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Mods
{
namespace
{
int (*g_gameMouseStick)(uint32_t) = nullptr;

int MouseStick(uint32_t pressed) { return ModernCamera::MouseStick(pressed, g_gameMouseStick); }

// The index of the game's controls' mode named `name` (fails loudly: the controls' modes are this program's own)
int ModeNamed(const Ps2GameInfo& game, const char* name)
{
    for (int i = 0; i < game.modeCount; i++)
    {
        if (std::strcmp(game.modes[i], name) == 0)
        {
            return i;
        }
    }
    std::fprintf(stderr, "mods: the controls have no mode \"%s\"\n", name);
    std::abort();
}
}

void Install(Ps2GameInfo& game, const ModernCamera::Layout& camera, const BiohazardControls::Layout& play,
             const Cheats::Layout& cheats)
{
    ModernCamera::Use(camera, play);
    ModernCamera::AddSettings();
    Cheats::Use(cheats);
    Cheats::AddSettings();

    static std::vector<Ps2GameSetting> settings;
    settings = ModSettings::Settings(game.settings, game.settingCount);
    game.settings = settings.data();
    game.settingCount = static_cast<int>(settings.size());

    // F8: the Camera tab over the running game, its framing seen as it's set (RaccoonRecomp's camera window)
    game.liveTab = ModernCamera::SettingsTab;

    g_gameMouseStick = game.mouseStick;
    game.mouseStick = MouseStick;

    // The camera's own controls in play: swap shoulder R3 (taken from her pad record while the camera drives:
    // src/modern_camera_game.h), recenter L3, the Alt view L3 and R3 at once
    static std::vector<Ps2GameControl> controls;
    controls.assign(game.controls, game.controls + game.controlCount);
    controls.push_back({"camera_shoulder", "Modern camera: swap shoulder", "r3", "Mouse Middle, X", ModeNamed(game, "Play")});
    controls.push_back({"camera_recenter", "Modern camera: recenter behind her", "l3", "C", ModeNamed(game, "Play")});
    controls.push_back({"camera_alt_view", "Modern camera: Alt view", "l3+r3", "V", ModeNamed(game, "Play")});
    game.controls = controls.data();
    game.controlCount = static_cast<int>(controls.size());
}

void FrameEnd(Ps2Cpu* c, Ps2Function original)
{
    original(c);
    ModernCamera::Frame();
    Cheats::Frame();
}
}
