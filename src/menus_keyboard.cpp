// Network Play's software keyboard (netwk's: its step File 2 0x552F10, File 1 0x553820, runs while it's up; src/typing.cpp
// lets the Mac's keyboard type into it). Its work area (the word at File 2 0x24B314, File 1 0x23C050) keeps its layout (the
// word at 0: a table of the keys' characters, of their places, and of the cells' keys), the cursor's cell (bytes 0x28, the
// column 0-20, and 0x29, the row 0-3) with the key there (byte 0x32 and the words 4, 8 and 12 pointing at its entries: File 2
// 0x552A10 sets them from the cell) and the keyboard's place (a float x at 0x44, a 16-bit y at 0x3C). Each key's place is an
// 8-byte entry: its x (a float) and y (16 bits) from the keyboard's place. The pointer over a key puts the cursor on its cell
// as the game's own move does (0x552A10's lookups), a click also presses the confirm, which types the key.
#include "menus.h"
#include "typing.h"

#include <cstring>

namespace Menus
{
namespace
{
using Pointer::Byte;
using Pointer::Rect;
using Pointer::Short;
using Pointer::Word;

struct Layout
{
    uint32_t work;
    Pointer::Sound move; // the keyboard's cursor sound (File 2 0x1B8010 with 0, as 0x552F10 plays it)
};

const Layout File2 = {0x24b314, {0x1b8010, 0}};
const Layout File1 = {0x23c050, {0x1b6f00, 0}};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

constexpr int Columns = 21, Rows = 4;
constexpr uint32_t Column = 0x28, Row = 0x29, Key = 0x32, KeyEntries = 4, X = 0x44, Y = 0x3c;
constexpr float KeyHeight = 17, KeyWidth = 21;

float Float(uint32_t address)
{
    uint32_t bits = Word(address);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool SoftKeyboard(Pointer::Screen& screen)
{
    if (!Typing::Typing())
    {
        return false;
    }
    uint32_t work = Word(L().work) & 0x1FFFFFF;
    uint32_t layout = work != 0 ? Word(work) & 0x1FFFFFF : 0;
    if (work == 0 || layout == 0 || Byte(work + 0x33) != 0 || Byte(work + 0x2a) != 0)
    {
        return false; // the sub-window or the text line has the cursor: the keys aren't where it moves
    }
    uint32_t characters = Word(layout) & 0x1FFFFFF, places = Word(layout + 4) & 0x1FFFFFF;
    uint32_t cells = Word(layout + 8) & 0x1FFFFFF, extras = Word(layout + 12) & 0x1FFFFFF;
    float x0 = Float(work + X), y0 = Short(work + Y);
    screen.name = "software keyboard";
    screen.moveSound = L().move;
    for (int c = 0; c < Columns; c++)
    {
        for (int r = 0; r < Rows; r++)
        {
            uint8_t key = Byte(cells + 4 * static_cast<uint32_t>(c) + static_cast<uint32_t>(r));
            // A key wider than a cell is the same key in the cells after it: its place once, from its first cell
            if (c > 0 && Byte(cells + 4 * static_cast<uint32_t>(c - 1) + static_cast<uint32_t>(r)) == key)
            {
                continue;
            }
            int span = 1;
            while (c + span < Columns && Byte(cells + 4 * static_cast<uint32_t>(c + span) + static_cast<uint32_t>(r)) == key)
            {
                span++;
            }
            uint32_t place = places + 8 * key;
            float x = x0 + Float(place), y = y0 + Short(place + 4);
            // The next key's x along the row ends this one (the last its own width)
            float end = x + KeyWidth * static_cast<float>(span);
            if (c + span < Columns)
            {
                uint8_t next = Byte(cells + 4 * static_cast<uint32_t>(c + span) + static_cast<uint32_t>(r));
                float nextX = x0 + Float(places + 8 * next);
                if (nextX > x)
                {
                    end = nextX;
                }
            }
            // The cell and the key there with its entries, as 0x552A10 sets them: its place, its character, its extra entry
            // (from key 12 on, where the layout has those)
            screen.Item({x, y, end, y + KeyHeight}, work + Column, 1, c)
                .With(work + Row, 1, r)
                .With(work + Key, 1, key)
                .With(work + KeyEntries, 4, static_cast<int32_t>(places + 8 * key))
                .With(work + KeyEntries + 4, 4, static_cast<int32_t>(characters + 4 * key))
                .With(work + KeyEntries + 8, 4, extras != 0 ? static_cast<int32_t>(extras + 16 * (key - 12)) : 0);
        }
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}
}

const Pointer::Finder KeyboardFinders[] = {SoftKeyboard};
const int KeyboardFinderCount = sizeof(KeyboardFinders) / sizeof(KeyboardFinders[0]);
}
