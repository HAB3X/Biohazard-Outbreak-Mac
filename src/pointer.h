// The mouse over the games' menus. Each menu the game shows is described, each vertical blank, from the game's own memory: its
// items' places in the picture (read from the menu's drawing data where the game keeps it, or the positions its drawing code
// uses), and what pointing at one does: the menu's own cursor is set to that item at once (the game draws its highlight, help
// text and sounds from the cursor the next frame), and a click also presses the menu's confirm, which the game then takes for
// the item under its cursor. Arrows, key hints and value arrows press the buttons they stand for. Nothing is steered with
// presses, so the cursor follows the pointer within a frame or two, and nothing is remembered between vertical blanks: what's
// on screen is read again each time, so a menu left and come back to is found again as it is.
#pragma once

#include "ps2/recomp.h"

#include <cstdint>

namespace Pointer
{
// A place in the game's 640 x 448 picture (the coordinates its 2D drawing uses)
struct Rect
{
    float x0, y0, x1, y1;

    bool Contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

inline Rect Box(float x, float y, float width, float height) { return {x, y, x + width, y + height}; }

// A list's rows (the drawn text or frames, in order down the screen) made into targets that meet: each as wide as the widest,
// and reaching halfway to the next (the first and last `margin` past their ends), so the pointer anywhere along the list is
// over a row
void Tile(Rect* rows, int count, float margin = 8.0f);

// The PS2's buttons (the pad's bits) a target or the wheel presses
enum Button : uint32_t
{
    Select = 1u << 0,
    Start = 1u << 3,
    Up = 1u << 4,
    Right = 1u << 5,
    Down = 1u << 6,
    Left = 1u << 7,
    L2 = 1u << 8,
    R2 = 1u << 9,
    L1 = 1u << 10,
    R1 = 1u << 11,
    Triangle = 1u << 12,
    Circle = 1u << 13,
    Cross = 1u << 14,
    Square = 1u << 15,
};

// One thing on a menu the pointer can point at. With a cursor: the pointer over it sets the cursor (`size` bytes at
// `cursor`) to `value`, and a click sets it and presses `press` (the confirm, or nothing for a row whose value the arrows and
// the wheel change). Without one: a click presses `press` (an arrow, a key hint, a tab the menu has no cursor for)
struct Target
{
    Rect rect;
    uint32_t cursor = 0;
    uint8_t size = 1;
    int32_t value = 0;
    uint32_t press = 0;
    int times = 1; // presses a click makes (a carousel's items some steps away: one press a step)
    // The wheel over this target presses these (0: the screen's)
    uint32_t wheelUp = 0, wheelDown = 0;
    // A menu whose place is more than one value (a mode and a cursor in it): the others written with the cursor, as the
    // menu's own move writes them
    struct Also
    {
        uint32_t at = 0;
        uint8_t size = 1;
        int32_t value = 0;
    };
    static constexpr int MaxAlso = 6;
    Also also[MaxAlso];
    int alsoCount = 0;

    // Another value written with the cursor (this target, for chaining)
    Target& With(uint32_t at, uint8_t bytes, int32_t to);
};

// A sound of the game's: its function (a guest address) and arguments, as the menu itself plays it when its cursor moves
struct Sound
{
    uint32_t function = 0;
    uint32_t a0 = 0, a1 = 0, a2 = 0;
};

// The menu up now: its targets (the first that contains the pointer wins: put the topmost first), what the wheel presses
// over the rest of the screen, and what a click on nothing presses (screens with nothing to choose: their confirm)
struct Screen
{
    static constexpr int MaxTargets = 128;
    const char* name = nullptr;
    Target targets[MaxTargets];
    int count = 0;
    uint32_t wheelUp = 0, wheelDown = 0;
    uint32_t clickElsewhere = 0;
    // Played when the pointer moves the cursor (the menu's own move sound; none: silent)
    Sound moveSound;
    // A menu that takes its moves one at a time (an animation between them): a click's further presses wait until this byte
    // says it takes input again (0: they're spaced in time only)
    uint32_t readyAt = 0;
    uint8_t readyValue = 0;
    // A pointer of the game's own that follows the mouse (its x and y as 16-bit words there, in the picture's coordinates;
    // 0: none): the network browser's
    uint32_t follow = 0;

    // A row that sets a cursor (and confirms when clicked); the target, to add the values written with it
    Target& Item(const Rect& rect, uint32_t cursor, uint8_t size, int32_t value, uint32_t press = Circle);
    // A place that presses buttons when clicked (`times` presses, one after the other)
    void Press(const Rect& rect, uint32_t buttons);
    void PressTimes(const Rect& rect, uint32_t buttons, int times);
    // A row whose value the arrows change: pointing at it sets the cursor, the wheel over it and a click on its arrows step
    // the value (left and right), a click on the rest of it does nothing more
    void Setting(const Rect& rect, uint32_t cursor, uint8_t size, int32_t value, const Rect& less, const Rect& more);
    Target* Add();
    // Emptied for the next menu (cheaper than a new one: the targets are left as they were)
    void Reset();
};

// A menu's description: true when it's up, with its targets put in the screen
using Finder = bool (*)(Screen&);

// Each vertical blank, the game side: the first finder that says its menu is up takes the pointer's moves, clicks and wheel.
// Whether one was up
bool Vblank(const Finder* finders, int count);

// Each of the game's frames, after its main loop's work (the game side, between functions): the sound a cursor moved by the
// pointer owes the menu is played, as the menu would have
void Frame(Ps2Cpu* c);

// Whether a code overlay (by its file on the disc) is in memory: the words that tell it from the others over its addresses
// are there
bool OverlayLoaded(const char* file);

// The guest's memory, as the finders read and the cursors are written
uint8_t Byte(uint32_t address);
uint16_t Half(uint32_t address);
uint32_t Word(uint32_t address);
int16_t Short(uint32_t address);
}
