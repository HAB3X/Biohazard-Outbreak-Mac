#include "controls.h"

#include "menus.h"
#include "text_map.h"

#include "ps2/iop.h"

#include <algorithm>
#include <atomic>

namespace BiohazardControls
{
namespace
{
enum Mode : int
{
    Play,
    MenuMode,
    Title,
    Pause,
};

const char* const Modes[] = {"Play", "Menus", "Title, movies and cutscenes", "Pause menu"};

// Escape is the game's own pause in play (SELECT's pause menu: the game stops, which it doesn't behind the status screen,
// START's, on Tab), SELECT again in that menu (closing it), START in the title and CROSS (back) in the other menus, where
// START is the status screen's own key (Tab): the status screen's cross closes it from its top level. Arrows and WASD are the D-pad in the menus; in play WASD is the left
// stick. Right click is the confirm in the menus (circle), as Return and E are; the left button points and clicks
// (src/pointer.h)
const Ps2GameControl Controls[] = {
    // Play: the game's own roles (its controller settings, type A): circle is the action and, with the weapon ready, the
    // attack; cross runs (and cancels); square is the ad-lib; triangle the map screen; L1 reloads; R1 readies, R2 readies
    // forward (no aiming at a target); L2 is the appeal shift (calling a partner by name); the right stick's four ways are
    // the appeals (seen in play: up "Go!", down "This way!", left "Help me!", right thanks or sorry, each character's own
    // line); SELECT the pause menu (settings, quit)
    {"move_forward", "Move forward", "left_up", "W", Play},
    {"move_back", "Move back", "left_down", "S", Play},
    {"move_left", "Move left", "left_left", "A", Play},
    {"move_right", "Move right", "left_right", "D", Play},
    {"walk", "Walk", "walk", "Left Ctrl", Play},
    {"run", "Run (hold)", "cross", "Left Shift", Play},
    {"action", "Action, check, pick up", "circle", "E, Return", Play},
    {"attack", "Attack, fire (weapon ready)", "circle", "Mouse Left", Play},
    {"ready", "Ready or aim the weapon (hold)", "r1", "Mouse Right", Play},
    {"ready_forward", "Ready forward, no target (hold)", "r2", "Q", Play},
    {"reload", "Reload", "l1", "R", Play},
    {"adlib", "Ad-lib", "square", "Space", Play},
    {"map", "Map", "triangle", "M", Play},
    {"appeal_shift", "Appeal shift: call a partner (hold)", "l2", "F", Play},
    {"appeal_go", "Appeal: Go!", "right_up", "1", Play},
    {"appeal_come", "Appeal: This way!", "right_down", "2", Play},
    {"appeal_help", "Appeal: Help me!", "right_left", "3", Play},
    {"appeal_thanks", "Appeal: Thanks, sorry", "right_right", "4", Play},
    {"pause", "Pause: settings, quit (SELECT)", "select", "Escape", Play},
    {"inventory", "Status screen: items, SPECIAL, FILE (START)", "start", "Tab", Play},
    {"dpad_up", "D-pad up", "up", "Up", Play},
    {"dpad_down", "D-pad down", "down", "Down", Play},
    {"dpad_left", "D-pad left", "left", "Left", Play},
    {"dpad_right", "D-pad right", "right", "Right", Play},
    {"menu_up", "Up", "up", "Up, W", MenuMode},
    {"menu_down", "Down", "down", "Down, S", MenuMode},
    {"menu_left", "Left", "left", "Left, A", MenuMode},
    {"menu_right", "Right", "right", "Right, D", MenuMode},
    {"menu_confirm", "Confirm (circle)", "circle", "E, Return, Mouse Right", MenuMode},
    {"menu_back", "Back, close (cross)", "cross", "Escape, Left Shift, Backspace", MenuMode},
    {"menu_triangle", "Triangle (info window; closes the map)", "triangle", "Space, M", MenuMode},
    {"menu_square", "Square", "square", "F", MenuMode},
    {"menu_tab_previous", "Previous tab (L1)", "l1", "Q, PageUp", MenuMode},
    {"menu_tab_next", "Next tab (R1)", "r1", "R, PageDown", MenuMode},
    {"menu_l2", "L2 (the collection's pages)", "l2", "Z", MenuMode},
    {"menu_r2", "R2 (the collection's pages)", "r2", "C", MenuMode},
    {"menu_start", "Close the inventory (START)", "start", "Tab", MenuMode},
    {"menu_select", "Select (the options' default settings)", "select", "O", MenuMode},
    {"title_start", "Start, skip", "start", "Escape, Return, Space, E, Tab", Title},
    {"pause_up", "Up", "up", "Up, W", Pause},
    {"pause_down", "Down", "down", "Down, S", Pause},
    {"pause_left", "Left, less", "left", "Left, A", Pause},
    {"pause_right", "Right, more", "right", "Right, D", Pause},
    {"pause_confirm", "Choose", "circle", "E, Return, Mouse Right", Pause},
    {"pause_back", "Back", "cross", "Backspace, Left Shift", Pause},
    {"pause_close", "Close the pause (SELECT)", "select", "Escape", Pause},
};

constexpr uint32_t R1 = 1u << 11, Cross = 1u << 14;

Layout g_layout = {};

uint32_t Word(uint32_t address) { return Pointer::Word(address); }

// Whether a task's function is among the game's active tasks
bool HasTask(uint32_t task)
{
    uint32_t count = std::min<uint32_t>(Word(g_layout.taskCount), 8);
    for (uint32_t n = 0; n < count; n++)
    {
        if (Word(g_layout.taskList + 4 * n) == task)
        {
            return true;
        }
    }
    return false;
}

// A menu the pointer can point at was up at the last vertical blank (the game's side finds them; the modes are asked on the
// presenter's thread)
std::atomic<bool> g_pointerMenu{false};

// The status task's screen (statusKind): 1 the status screen, 2 the map, 4 SELECT's pause menu
constexpr uint32_t StatusKindPause = 4;

// What the game is doing, from its memory alone: the game's loop is play, unless the status task (the status screen, the map,
// the pause) or a menu of its own is over it, or it plays a scene; without it, a cutscene or the title's PRESS START and its
// attract movies (or the executable's loop alone: the boot's movies, loading) are the title's, and the rest are menus
int CurrentMode()
{
    if (HasTask(g_layout.gameTask))
    {
        if (HasTask(g_layout.statusTask))
        {
            return (Word(g_layout.statusKind) & 0xFF) == StatusKindPause ? Pause : MenuMode;
        }
        // a scene the game plays itself in its play loop (File 1's opening credits) is the title's: Escape skips it
        bool scene = HasTask(g_layout.cutsceneTask) ||
                     (g_layout.playState != 0 && *Ps2::EeMemory(g_layout.playState) == g_layout.openingState);
        if (scene)
        {
            return Title;
        }
        return g_pointerMenu ? MenuMode : Play;
    }
    if (HasTask(g_layout.cutsceneTask))
    {
        return Title;
    }
    if (g_pointerMenu)
    {
        return MenuMode;
    }
    if (Menus::AtPressStart())
    {
        return Title;
    }
    bool executableAlone = Word(g_layout.taskCount) == 1 && HasTask(g_layout.baseTask);
    return executableAlone && !Menus::TitleRunning() && !Menus::NetworkRunning() ? Title : MenuMode;
}

int MenuUp() { return CurrentMode() != Play; }

// While the weapon is readied in play (R1, the mouse's right button) the mouse aims, as in the PC releases of Resident Evil 4
// and 5: across it turns toward the targets, up and down it aims high and low (the left stick's, as the game aims)
int MouseStick(uint32_t pressed) { return CurrentMode() == Play && (pressed & R1) != 0 ? 1 : 0; }

// The title's OPTIONS' state, 0 when it isn't up
uint32_t OptionsState()
{
    if (g_layout.optionsState == 0 || HasTask(g_layout.gameTask) || !Menus::TitleRunning())
    {
        return 0;
    }
    uint32_t state = Word(g_layout.optionsState) & 0x1FFFFFF;
    return state != 0 && state < 0x1FFFFF0 ? state : 0;
}

// The options' CONTROLLER SETTINGS (the list's first row) shows the PS2 pad; while the keyboard and mouse were used last it's
// the PC menus' Controls instead: the game's screen is left at once (CROSS) and the PC options open over the list
constexpr uint8_t OptionsScreen = 5;

void ControllerSettings()
{
    static int leaving = 0; // vertical blanks since CROSS was pressed to leave the game's screen (0: not leaving)
    uint32_t state = OptionsState();
    bool inScreen = state != 0 && *Ps2::EeMemory(state + 1) == OptionsScreen && Pointer::Half(state + 4) == 0;
    if (leaving == 0)
    {
        if (inScreen && ps2_prompt_style() == 0)
        {
            ps2_press_buttons(Cross, 0.05);
            leaving = 1;
        }
        return;
    }
    if (!inScreen)
    {
        leaving = 0;
        ps2_open_settings();
        return;
    }
    if (++leaving % 45 == 0) // the press came during the screen's own transition: again
    {
        ps2_press_buttons(Cross, 0.05);
    }
}

// Game side, each vertical blank: the menus' tap spacing, the options' controller screen, and the pointer
void Vblank()
{
    // Each change of mode logged with what decided it (the task list), to find screens taken for the wrong mode
    static int lastMode = -1;
    int mode = CurrentMode();
    if (mode != lastMode)
    {
        uint32_t count = std::min<uint32_t>(Word(g_layout.taskCount), 8);
        Ps2::Log("controls: %s (tasks %u: %08x %08x %08x)", Modes[mode], count, count > 0 ? Word(g_layout.taskList) : 0,
                 count > 1 ? Word(g_layout.taskList + 4) : 0, count > 2 ? Word(g_layout.taskList + 8) : 0);
        lastMode = mode;
    }
    ControllerSettings();
    static bool spaced = false;
    bool menu = mode != Play;
    if (menu != spaced)
    {
        // The game's menus ignore presses that come within 6 game frames of a move: taps closer than 7 wait their turn
        ps2_set_tap_spacing(menu ? 7 : 0);
        spaced = menu;
    }
    int count = 0;
    const Pointer::Finder* finders = Menus::Finders(count);
    g_pointerMenu = Pointer::Vblank(finders, count);
}
}

const Layout File2 = {.taskCount = 0x24ad3c,
                      .taskList = 0x2aa890,
                      .gameTask = 0x344cb0,
                      .cutsceneTask = 0x344c10,
                      .baseTask = 0x1b3fa0,
                      .statusTask = 0x6c8fa0,
                      .statusKind = 0x767254,
                      .optionsState = 0x24b08c};

// File 1's (found by memory diffs: the game's and the status screen's tasks, the status task's screen byte)
const Layout File1 = {.taskCount = 0x23bb30,
                      .taskList = 0x29b0d0,
                      .gameTask = 0x341b80,
                      .cutsceneTask = 0x341af0,
                      .playState = 0x48bf60,
                      .openingState = 14,
                      .baseTask = 0x1b3400,
                      .statusTask = 0x6b9450,
                      .statusKind = 0x720be4,
                      .optionsState = 0x23be40};

void Apply(Ps2GameInfo& game, const Layout& layout)
{
    g_layout = layout;
    const bool file2 = &layout == &File2;
    Menus::Use(file2 ? Menus::Disc::File2 : Menus::Disc::File1);
    TextMap::Use(file2 ? TextMap::File2 : TextMap::File1);
    game.ownPause = 1;
    game.controls = Controls;
    game.controlCount = static_cast<int>(sizeof(Controls) / sizeof(Controls[0]));
    game.modes = Modes;
    game.modeCount = static_cast<int>(sizeof(Modes) / sizeof(Modes[0]));
    game.mode = CurrentMode;
    game.menu = MenuUp;
    game.mouseStick = MouseStick;
    game.vblank = Vblank;
}
}
