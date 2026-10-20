#include "controls.h"

#include "ps2/iop.h"

#include <algorithm>
#include <cstring>

namespace BiohazardControls
{
namespace
{
enum Mode : int
{
    Play,
    Menus,
    Title,
};

const char* const Modes[] = {"Play", "Menus", "Title, movies and cutscenes"};

// Escape is START in play and in the title, CROSS (back) in the menus, where START is the status screen's own key (Tab): the
// status screen's cross closes it from its top level. Arrows and WASD are the D-pad in the menus; in play WASD is the left
// stick. Right click is the confirm in the menus (circle), as Return and E are; the left button clicks (see Steer)
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
    {"options", "Pause menu: settings, quit (SELECT)", "select", "O", Play},
    {"inventory", "Status screen, pause (START)", "start", "Tab, Escape", Play},
    {"dpad_up", "D-pad up", "up", "Up", Play},
    {"dpad_down", "D-pad down", "down", "Down", Play},
    {"dpad_left", "D-pad left", "left", "Left", Play},
    {"dpad_right", "D-pad right", "right", "Right", Play},
    {"menu_up", "Up", "up", "Up, W", Menus},
    {"menu_down", "Down", "down", "Down, S", Menus},
    {"menu_left", "Left", "left", "Left, A", Menus},
    {"menu_right", "Right", "right", "Right, D", Menus},
    {"menu_confirm", "Confirm (circle)", "circle", "E, Return, Mouse Right", Menus},
    {"menu_back", "Back, close (cross)", "cross", "Escape, Left Shift, Backspace", Menus},
    {"menu_triangle", "Triangle (info window; closes the map)", "triangle", "Space, M", Menus},
    {"menu_square", "Square", "square", "F", Menus},
    {"menu_tab_previous", "Previous tab (L1)", "l1", "Q, PageUp", Menus},
    {"menu_tab_next", "Next tab (R1)", "r1", "R, PageDown", Menus},
    {"menu_l2", "L2 (the collection's pages)", "l2", "Z", Menus},
    {"menu_r2", "R2 (the collection's pages)", "r2", "C", Menus},
    {"menu_start", "Close the inventory (START)", "start", "Tab", Menus},
    {"menu_select", "Select (the options' default settings)", "select", "O", Menus},
    {"title_start", "Start, skip", "start", "Escape, Return, Space, E, Tab", Title},
};

constexpr uint32_t Up = 1u << 4, Right = 1u << 5, Down = 1u << 6, Left = 1u << 7, L1 = 1u << 10, R1 = 1u << 11,
                   Circle = 1u << 13, Start = 1u << 3;

Layout g_layout = {};

uint32_t Word(uint32_t address)
{
    uint32_t value;
    std::memcpy(&value, Ps2::EeMemory(address), 4);
    return value;
}

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

// The title's menu: the executable's loop alone, with the title's state at its menu
bool TitleMenu() { return Word(g_layout.taskCount) == 1 && HasTask(g_layout.baseTask) && Word(g_layout.titleState) == 3; }

// The game's loop with nothing on top of it is play; the executable's own loop alone (a movie, the title's "PRESS START", a load)
// is the title and movies; anything else (the title menu, a select, the status screen over the game) is a menu
int CurrentMode()
{
    uint32_t count = Word(g_layout.taskCount);
    if (HasTask(g_layout.gameTask))
    {
        return count > 2 ? Menus : HasTask(g_layout.cutsceneTask) ? Title : Play;
    }
    if (HasTask(g_layout.cutsceneTask))
    {
        return Title;
    }
    return count == 1 && HasTask(g_layout.baseTask) && !TitleMenu() ? Title : Menus;
}

int MenuUp() { return CurrentMode() != Play; }

// While the weapon is readied in play (R1, the mouse's right button) the mouse aims, as in the PC releases of Resident Evil 4
// and 5: across it turns toward the targets, up and down it aims high and low (the left stick's, as the game aims)
int MouseStick(uint32_t pressed) { return CurrentMode() == Play && (pressed & R1) != 0 ? 1 : 0; }

// --- the mouse in the menus: the pointer over an item moves the game's cursor there, a click confirms the item, the wheel turns
// pages. The game's cursor is moved the way the player moves it, with D-pad presses, one a step and each checked in the game's
// memory (a step that moved nothing is tried again a few times, then given up)

struct Box
{
    float x, y, halfWidth, halfHeight; // the picture's fractions
};

bool Inside(const Box& box, float x, float y)
{
    return x >= box.x - box.halfWidth && x < box.x + box.halfWidth && y >= box.y - box.halfHeight && y < box.y + box.halfHeight;
}

// Where the game draws these, as fractions of its 640 x 560 picture
const Box Tabs[3] = {{0.377f, 0.080f, 0.057f, 0.025f}, {0.502f, 0.080f, 0.057f, 0.025f}, {0.627f, 0.080f, 0.057f, 0.025f}};
// The item slots: the grid's four (row by row) and the equipped weapon's
const Box Slots[5] = {{0.837f, 0.265f, 0.038f, 0.056f}, {0.915f, 0.265f, 0.038f, 0.056f}, {0.837f, 0.379f, 0.038f, 0.056f},
                      {0.915f, 0.379f, 0.038f, 0.056f}, {0.911f, 0.518f, 0.040f, 0.056f}};

enum class Goal
{
    None,
    TitleItem,
    Tab,
    Slot,
};

struct Steer
{
    Goal goal = Goal::None;
    int target = 0;
    bool confirm = false; // press the confirm button on arrival (a click)
    bool pressed = false; // a step is out: the game takes a few frames to show it
    int waited = 0;       // vertical blanks since the step
    int tries = 0;        // steps that moved nothing, one after the other
    int last = -1;        // the cursor before the last step
} g_steer;

// The game shows a step in its memory some frames after the press (up to half a second, its menus' own pace); one that shows
// nothing in this long is lost
constexpr int StepTimeout = 90;

// The slots' place moves between runs, and the cursor doesn't open on the first: its address is found when the pointer first
// goes over them, by moving the cursor up and left until it stops (the slots are one after the other from it)
uint32_t g_slotBase = 0;
int g_homeSteps = 0; // the moves toward it made
constexpr uint32_t Homing[] = {Left, Up, Up};
constexpr int HomeSteps = sizeof(Homing) / sizeof(Homing[0]);
constexpr int HomeTimeout = 40; // vertical blanks: a move that shows nothing in this long is at the corner

bool SlotsUsable()
{
    return HasTask(g_layout.statusTask) && (Word(g_layout.statusTab) & 0xFF) == 0 && (Word(g_layout.statusPopup) & 0xFF) == 0;
}

// 0-3 the grid's slots (row by row), 4 the equipped weapon's, -1 before the first slot is known
int SlotOf(uint32_t cursor)
{
    uint32_t n = (cursor - g_slotBase) / g_layout.slotStride;
    return g_slotBase != 0 && cursor >= g_slotBase && n < 4 && (cursor - g_slotBase) % g_layout.slotStride == 0 ? static_cast<int>(n)
           : g_slotBase != 0 ? 4
                             : -1;
}

// Where the cursor is now in the goal's terms, -1 when it isn't where it can be steered (the item's actions are up...)
int CursorNow()
{
    switch (g_steer.goal)
    {
    case Goal::TitleItem: return TitleMenu() ? static_cast<int>(Word(g_layout.titleCursor)) : -1;
    case Goal::Tab: return HasTask(g_layout.statusTask) ? static_cast<int>(Word(g_layout.statusTab) & 0xFF) : -1;
    case Goal::Slot: return SlotsUsable() ? SlotOf(Word(g_layout.statusCursor)) : -1;
    default: return -1;
    }
}

// The button that moves the cursor from `now` toward the target (0: nothing to press). The slots are a grid of 2 x 2 with the
// weapon's below it: the weapon is reached from the bottom row by down, left by up
uint32_t NextButton(int now)
{
    int to = g_steer.target;
    switch (g_steer.goal)
    {
    case Goal::TitleItem: return to > now ? Down : Up;
    case Goal::Tab: return to > now ? R1 : L1;
    case Goal::Slot:
    {
        if (to == 4)
        {
            return Down;
        }
        if (now == 4)
        {
            return Up;
        }
        if (to / 2 != now / 2)
        {
            return to / 2 > now / 2 ? Down : Up;
        }
        return to % 2 > now % 2 ? Right : Left;
    }
    default: return 0;
    }
}

void Aim(Goal goal, int target, bool confirm)
{
    g_steer = Steer();
    g_homeSteps = 0;
    g_steer.goal = goal;
    g_steer.target = target;
    g_steer.confirm = confirm;
}

// The selects run sideways: the scenario select's current scenario is drawn left of the middle with the others after it (and
// before it, once it has moved on), the character and partner selects have an arrow at each edge. A click beside the current
// one (or on an arrow) steps that way; a click on it is the confirm
const Box ScenarioCurrent = {0.375f, 0.39f, 0.195f, 0.20f};
const Box SelectArrows[2] = {{0.049f, 0.376f, 0.045f, 0.06f}, {0.951f, 0.376f, 0.045f, 0.06f}};

// File 1's selects run with the task list empty, the title overlay's screen byte saying which
constexpr uint8_t ScreenScenarioSelect = 8, ScreenCharacterSelect = 9;

int TitleScreen()
{
    if (g_layout.screenState == 0 || Word(g_layout.taskCount) != 0)
    {
        return -1;
    }
    // The address as the game keeps it (it may be stale between screens: kept within main memory)
    uint32_t state = Word(g_layout.screenState);
    return state != 0 ? *Ps2::EeMemory((state + 4) & 0x1FFFFFF) : -1;
}

bool InScenarioSelect()
{
    return (g_layout.scenarioTask != 0 && HasTask(g_layout.scenarioTask)) || TitleScreen() == ScreenScenarioSelect;
}
bool InCharacterSelect()
{
    return (g_layout.selectTask != 0 && HasTask(g_layout.selectTask)) || TitleScreen() == ScreenCharacterSelect;
}
bool InSelect() { return InCharacterSelect() || InScenarioSelect(); }

// A click on a select: the step it asks for (Left, Right) or 0 for none
uint32_t SelectStep(float x, float y)
{
    if (InScenarioSelect() && y > 0.15f && y < 0.62f && !Inside(ScenarioCurrent, x, y))
    {
        return x < ScenarioCurrent.x ? Left : Right;
    }
    if (InCharacterSelect())
    {
        for (int n = 0; n < 2; n++)
        {
            if (Inside(SelectArrows[n], x, y))
            {
                return n == 0 ? Left : Right;
            }
        }
    }
    return 0;
}

// The goal under the pointer, if there is one (hovering moves the cursor, which a tab doesn't: those are clicked)
bool Under(float x, float y, bool click)
{
    if (TitleMenu() && g_layout.titleCursor != 0)
    {
        // The title menu's items: 6 with a hard disk (HDD INSTALL last), 5 without, the runtime's case (the title's word 0x20C,
        // 512 bytes after its cursor, as the game counts them)
        int items = static_cast<int>(std::min<uint32_t>(Word(g_layout.titleCursor + 512), 6));
        for (int n = 0; n < items; n++)
        {
            if (Inside({0.5f, g_layout.titleTop + g_layout.titleStep * static_cast<float>(n), 0.14f, g_layout.titleStep / 2}, x, y))
            {
                Aim(Goal::TitleItem, n, click);
                return true;
            }
        }
    }
    if (HasTask(g_layout.statusTask) && g_layout.statusTab != 0)
    {
        for (int n = 0; n < 3 && click; n++)
        {
            if (Inside(Tabs[n], x, y))
            {
                Aim(Goal::Tab, n, false);
                return true;
            }
        }
        for (int n = 0; n < 5 && g_layout.statusCursor != 0; n++)
        {
            if (Inside(Slots[n], x, y))
            {
                Aim(Goal::Slot, n, click);
                return true;
            }
        }
    }
    return false;
}

// Finds the first slot: moves toward it, each move waited for, and takes the cursor's place at the last
void Home()
{
    if (!SlotsUsable())
    {
        g_steer = Steer();
        g_homeSteps = 0;
        return;
    }
    uint32_t cursor = Word(g_layout.statusCursor);
    if (g_steer.pressed && cursor == static_cast<uint32_t>(g_steer.last) && ++g_steer.waited < HomeTimeout)
    {
        return;
    }
    if (g_homeSteps == HomeSteps)
    {
        g_slotBase = cursor;
        g_homeSteps = 0;
        g_steer.pressed = false;
        return;
    }
    g_steer.last = static_cast<int>(cursor);
    g_steer.pressed = true;
    g_steer.waited = 0;
    ps2_press_buttons(Homing[g_homeSteps++], 0.05);
}

// One step of the steering: a press toward the target, then wait for the cursor to show it
void Step()
{
    if (g_steer.goal == Goal::None)
    {
        return;
    }
    if (g_steer.goal == Goal::Slot && g_slotBase == 0)
    {
        Home();
        return;
    }
    int now = CursorNow();
    if (now < 0)
    {
        g_steer = Steer();
        return;
    }
    if (g_steer.pressed)
    {
        if (now == g_steer.last && ++g_steer.waited < StepTimeout)
        {
            return;
        }
        g_steer.tries = now == g_steer.last ? g_steer.tries + 1 : 0;
        g_steer.pressed = false;
    }
    if (now == g_steer.target)
    {
        if (g_steer.confirm)
        {
            ps2_press_buttons(Circle, 0.05);
        }
        g_steer = Steer();
        return;
    }
    if (g_steer.tries >= 3)
    {
        Ps2::Log("controls: the cursor stays at %d going for %d: given up", now, g_steer.target);
        g_steer = Steer();
        return;
    }
    g_steer.last = now;
    g_steer.pressed = true;
    g_steer.waited = 0;
    ps2_press_buttons(NextButton(now), 0.05);
}

// The wheel turns the status screen's tabs, the selects' pages (they run sideways) and the other menus' lists
void Wheel(int kind)
{
    bool up = kind == PS2_MOUSE_WHEEL_UP;
    uint32_t button;
    if (HasTask(g_layout.statusTask))
    {
        button = up ? L1 : R1;
    }
    else if (InSelect())
    {
        button = up ? Left : Right;
    }
    else
    {
        button = up ? Up : Down;
    }
    ps2_press_buttons(button, 0.05);
}

// Game side, each vertical blank: the tap spacing of the menus, and the mouse
void Vblank()
{
    static bool spaced = false;
    bool menu = MenuUp();
    if (menu != spaced)
    {
        // The game's menus ignore presses that come within 6 game frames of a move: taps closer than 7 wait their turn
        ps2_set_tap_spacing(menu ? 7 : 0);
        spaced = menu;
    }
    if (!menu)
    {
        g_steer = Steer();
        g_slotBase = 0;
        return;
    }
    if (!HasTask(g_layout.statusTask))
    {
        g_slotBase = 0;
    }
    float x, y;
    int kind;
    if (ps2_mouse_moved(&x, &y))
    {
        Under(x, y, false);
    }
    if (ps2_take_click(&x, &y))
    {
        uint32_t step = SelectStep(x, y);
        if (CurrentMode() == Title)
        {
            ps2_press_buttons(Start, 0.05);
        }
        else if (step != 0)
        {
            ps2_press_buttons(step, 0.05);
        }
        else if (!Under(x, y, true))
        {
            ps2_press_buttons(Circle, 0.05); // a click away from the items: the confirm, as Return
        }
    }
    while (ps2_take_mouse_event(&kind, &x, &y))
    {
        if (kind == PS2_MOUSE_WHEEL_UP || kind == PS2_MOUSE_WHEEL_DOWN)
        {
            Wheel(kind);
        }
    }
    Step();
}
}

const Layout File2 = {.taskCount = 0x24ad3c,
                      .taskList = 0x2aa890,
                      .gameTask = 0x344cb0,
                      .cutsceneTask = 0,
                      .baseTask = 0x1b3fa0,
                      .selectTask = 0x554870,
                      .scenarioTask = 0x553420,
                      .screenState = 0,
                      .titleState = 0x24ac70,
                      .statusTask = 0x6c8fa0,
                      .titleCursor = 0x8bfe4c,
                      .statusTab = 0x76a60e,
                      .statusCursor = 0x7671f4,
                      .statusPopup = 0x767210,
                      .slotStride = 0x98,
                      .titleTop = 0.645f,
                      .titleStep = 0.0450f};

// File 1's (found by memory diffs: the title menu's cursor stepping 0-1-2-1, the status screen's tab 0-1-2-1-0, its item cursor
// moving by the slots' stride with Right and Down, the item popup's byte 0x1C after it as in File 2: 1 while the actions are up).
// Its selects aren't a task of the list (the list is empty there): the title overlay's screen byte says which (found by memory
// diffs of the title menu, the scenario and character selects and the difficulty, then DEMO.BIN's code read)
const Layout File1 = {.taskCount = 0x23bb30,
                      .taskList = 0x29b0d0,
                      .gameTask = 0x341b80,
                      .cutsceneTask = 0x341af0,
                      .baseTask = 0x1b3400,
                      .selectTask = 0,
                      .scenarioTask = 0,
                      .screenState = 0x3065fc,
                      .titleState = 0x23ba68,
                      .statusTask = 0x6b9450,
                      .titleCursor = 0x90d1cc,
                      .statusTab = 0x723f9e,
                      .statusCursor = 0x720b84,
                      .statusPopup = 0x720ba0,
                      .slotStride = 0x98,
                      .titleTop = 0.591f,
                      .titleStep = 0.0536f};

void Apply(Ps2GameInfo& game, const Layout& layout)
{
    g_layout = layout;
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
