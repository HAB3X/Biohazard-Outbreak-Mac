#include "pointer.h"

#include "text_map.h"

#include "ps2/iop.h"
#include "ps2/recomp.h"
#include "ps2/runtime.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Pointer
{
uint8_t Byte(uint32_t address) { return *Ps2::EeMemory(address); }

uint16_t Half(uint32_t address)
{
    uint16_t value;
    std::memcpy(&value, Ps2::EeMemory(address), 2);
    return value;
}

uint32_t Word(uint32_t address)
{
    uint32_t value;
    std::memcpy(&value, Ps2::EeMemory(address), 4);
    return value;
}

int16_t Short(uint32_t address) { return static_cast<int16_t>(Half(address)); }

bool OverlayLoaded(const char* file)
{
    for (uint32_t n = 0; n < ps2_overlay_count; n++)
    {
        const Ps2Overlay& o = ps2_overlays[n];
        if (std::strcmp(o.file, file) != 0)
        {
            continue;
        }
        if (o.checkCount == 0)
        {
            return false; // nothing tells it from the others over its addresses: never taken for loaded
        }
        for (uint32_t k = 0; k < o.checkCount; k++)
        {
            if (Word(o.check[k].address) != o.check[k].word)
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

void Tile(Rect* rows, int count, float margin)
{
    if (count == 0)
    {
        return;
    }
    float x0 = rows[0].x0, x1 = rows[0].x1;
    for (int n = 1; n < count; n++)
    {
        x0 = std::min(x0, rows[n].x0);
        x1 = std::max(x1, rows[n].x1);
    }
    float top = rows[0].y0, bottom = rows[count - 1].y1;
    for (int n = 0; n + 1 < count; n++)
    {
        float middle = (rows[n].y1 + rows[n + 1].y0) / 2;
        rows[n].y1 = middle;
        rows[n + 1].y0 = middle;
    }
    rows[0].y0 = top - margin;
    rows[count - 1].y1 = bottom + margin;
    for (int n = 0; n < count; n++)
    {
        rows[n].x0 = x0;
        rows[n].x1 = x1;
    }
}

void Screen::Reset()
{
    name = nullptr;
    count = 0;
    wheelUp = wheelDown = clickElsewhere = 0;
    moveSound = Sound();
    follow = 0;
    readyAt = 0;
    readyValue = 0;
}

Target* Screen::Add()
{
    if (count == MaxTargets)
    {
        Ps2::Log("pointer: %s has more than %d targets: the rest are left out", name, MaxTargets);
        return nullptr;
    }
    targets[count] = Target();
    return &targets[count++];
}

Target& Target::With(uint32_t at, uint8_t bytes, int32_t to)
{
    if (alsoCount == MaxAlso)
    {
        Ps2::Log("pointer: a target writes more than %d values with its cursor: the rest are left out", MaxAlso);
        return *this;
    }
    also[alsoCount++] = {at, bytes, to};
    return *this;
}

Target& Screen::Item(const Rect& rect, uint32_t cursor, uint8_t size, int32_t value, uint32_t press)
{
    static Target spare; // what a full screen hands back (nothing is kept of it)
    Target* t = Add();
    if (t == nullptr)
    {
        spare = Target();
        return spare;
    }
    t->rect = rect;
    t->cursor = cursor;
    t->size = size;
    t->value = value;
    t->press = press;
    return *t;
}

void Screen::Press(const Rect& rect, uint32_t buttons)
{
    if (Target* t = Add())
    {
        t->rect = rect;
        t->press = buttons;
    }
}

void Screen::PressTimes(const Rect& rect, uint32_t buttons, int times)
{
    if (Target* t = Add())
    {
        t->rect = rect;
        t->press = buttons;
        t->times = times;
    }
}

void Screen::Setting(const Rect& rect, uint32_t cursor, uint8_t size, int32_t value, const Rect& less, const Rect& more)
{
    // The arrows first: they're inside the row
    for (const Rect* arrow : {&less, &more})
    {
        if (Target* t = Add())
        {
            t->rect = *arrow;
            t->cursor = cursor;
            t->size = size;
            t->value = value;
            t->press = arrow == &less ? Left : Right;
            t->wheelUp = Right;
            t->wheelDown = Left;
        }
    }
    if (Target* t = Add())
    {
        t->rect = rect;
        t->cursor = cursor;
        t->size = size;
        t->value = value;
        t->wheelUp = Right;
        t->wheelDown = Left;
    }
}

namespace
{
// The picture's fractions (the runtime's) in the game's 640 x 448 coordinates
constexpr float Width = 640.0f, Height = 448.0f;

int32_t Read(uint32_t address, uint8_t size)
{
    switch (size)
    {
    case 1: return Byte(address);
    case 2: return Half(address);
    default: return static_cast<int32_t>(Word(address));
    }
}

void Write(uint32_t address, uint8_t size, int32_t value)
{
    std::memcpy(Ps2::EeMemory(address), &value, size); // little endian: the low bytes
}

const Target* Under(const Screen& screen, float x, float y)
{
    for (int n = 0; n < screen.count; n++)
    {
        if (screen.targets[n].rect.Contains(x, y))
        {
            return &screen.targets[n];
        }
    }
    return nullptr;
}

// $BH_POINTER_LOG: the strings drawn in the last finished frame and their places, logged at each change of menu and every
// three seconds (docs/controls.md: how menus are found)
bool Diagnostics()
{
    static const bool on = getenv("BH_POINTER_LOG") != nullptr;
    return on;
}

void LogText()
{
    if (!Diagnostics())
    {
        return;
    }
    const TextMap::Drawn* drawn;
    int n = TextMap::Last(drawn);
    for (int k = 0; k < n; k++)
    {
        char text[TextMap::KeptBytes] = {};
        for (int c = 0; c < TextMap::KeptBytes - 1 && drawn[k].bytes[c] != 0; c++)
        {
            uint8_t b = static_cast<uint8_t>(drawn[k].bytes[c]);
            text[c] = b >= 0x20 && b < 0x7F ? static_cast<char>(b) : '?';
        }
        Ps2::Log("  text %08x %.0f,%.0f-%.0f,%.0f \"%s\"", drawn[k].text, drawn[k].place.x0, drawn[k].place.y0, drawn[k].place.x1,
                 drawn[k].place.y1, text);
    }
}

Screen g_screen;
// Presses a click still owes (a carousel's steps): one each time the last has been taken and spaced
struct Owed
{
    uint32_t buttons = 0;
    int left = 0;
    int wait = 0;
} g_presses;
// Vertical blanks between two owed presses (the menus' tap spacing is 7 game frames; a press shows for a frame or two)
constexpr int PressGap = 10;

void Press(uint32_t buttons, int times)
{
    ps2_press_buttons(buttons, 0.05);
    g_presses = {buttons, times - 1, PressGap};
}

// The move sound owed since the last frame (function 0: none)
Sound g_owed;

// The cursor put on a target (when it isn't there already, its other values with it), with the menu's move sound owed
void Point(const Target& t)
{
    if (t.cursor == 0)
    {
        return;
    }
    bool there = Read(t.cursor, t.size) == t.value;
    for (int n = 0; n < t.alsoCount && there; n++)
    {
        there = Read(t.also[n].at, t.also[n].size) == t.also[n].value;
    }
    if (there)
    {
        return;
    }
    for (int n = 0; n < t.alsoCount; n++)
    {
        Write(t.also[n].at, t.also[n].size, t.also[n].value);
    }
    Write(t.cursor, t.size, t.value);
    g_owed = g_screen.moveSound;
}
}

void Frame(Ps2Cpu* c)
{
    if (g_owed.function == 0)
    {
        return;
    }
    const Sound sound = g_owed;
    g_owed = Sound();
    // Called as the main loop would call it: its registers are the loop's again afterwards
    R128 saved[32];
    std::memcpy(saved, c->r, sizeof(saved));
    c->r[4].ud[0] = sound.a0;
    c->r[5].ud[0] = sound.a1;
    c->r[6].ud[0] = sound.a2;
    ps2_call(c, sound.function);
    std::memcpy(c->r, saved, sizeof(saved));
}

// The game's own pointer put where the mouse is (the browser's)
void Follow(float x, float y)
{
    if (g_screen.follow != 0)
    {
        int16_t at[2] = {static_cast<int16_t>(std::clamp(x, 0.0f, 1.0f) * Width), static_cast<int16_t>(std::clamp(y, 0.0f, 1.0f) * Height)};
        std::memcpy(Ps2::EeMemory(g_screen.follow), at, sizeof(at));
    }
}

bool Vblank(const Finder* finders, int count)
{
    static const char* last = nullptr;
    static int ticks = 0;
    if (Diagnostics() && ++ticks % 180 == 0)
    {
        Ps2::Log("pointer: the text drawn now");
        LogText();
    }
    bool found = false;
    for (int n = 0; n < count && !found; n++)
    {
        g_screen.Reset();
        found = finders[n](g_screen);
    }
    const char* name = found ? g_screen.name : nullptr;
    if (name != last)
    {
        Ps2::Log("pointer: %s (%d targets)", name != nullptr ? name : "no menu", found ? g_screen.count : 0);
        last = name;
        LogText();
    }
    float fx, fy;
    int kind;
    if (g_presses.left > 0 && found && --g_presses.wait <= 0 &&
        (g_screen.readyAt == 0 || Byte(g_screen.readyAt) == g_screen.readyValue))
    {
        ps2_press_buttons(g_presses.buttons, 0.05);
        g_presses.left--;
        g_presses.wait = PressGap;
    }
    if (!found)
    {
        g_presses = Owed(); // the menu's gone: its steps with it
        // Nothing to point at: the pointer's events are let go (a click here isn't kept for the next menu)
        while (ps2_take_click(&fx, &fy) || ps2_take_mouse_event(&kind, &fx, &fy))
        {
        }
        return false;
    }
    if (ps2_mouse_moved(&fx, &fy))
    {
        Follow(fx, fy);
        if (const Target* t = Under(g_screen, fx * Width, fy * Height))
        {
            Point(*t);
        }
    }
    while (ps2_take_click(&fx, &fy))
    {
        Follow(fx, fy);
        const Target* t = Under(g_screen, fx * Width, fy * Height);
        if (t != nullptr)
        {
            Point(*t);
            if (t->press != 0)
            {
                Press(t->press, t->times);
            }
        }
        else if (g_screen.clickElsewhere != 0)
        {
            Press(g_screen.clickElsewhere, 1);
        }
    }
    while (ps2_take_mouse_event(&kind, &fx, &fy))
    {
        if (kind != PS2_MOUSE_WHEEL_UP && kind != PS2_MOUSE_WHEEL_DOWN)
        {
            continue;
        }
        bool up = kind == PS2_MOUSE_WHEEL_UP;
        const Target* t = Under(g_screen, fx * Width, fy * Height);
        uint32_t buttons = t != nullptr && (t->wheelUp | t->wheelDown) != 0 ? (up ? t->wheelUp : t->wheelDown)
                                                                             : (up ? g_screen.wheelUp : g_screen.wheelDown);
        if (t != nullptr && (t->wheelUp | t->wheelDown) != 0)
        {
            Point(*t);
        }
        if (buttons != 0)
        {
            Press(buttons, 1);
        }
    }
    return true;
}
}
