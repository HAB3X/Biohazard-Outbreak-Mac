// Network Play's screens before login (the network overlay's own state block, File 2 0x6AE220, File 1 0x68C5F0, whose
// address a word of the executable's data holds: File 2 0x24B4E4, File 1 0x23B618) and the KDDI browser (nethttp's pointer).
// The setup screens draw what's up from a word of flags at 0x2C of the state (File 2 0x58E0C0 draws each flag's element,
// 0x58E630). Their menus (the NET file's yes/no, the broadband menu, the manners screen's Next and Back...) are one engine in
// two layers: the flags 0x200 (the lower) and 0x200000 (the upper, the dialogs over a menu) each draw a menu's highlight bar
// at a place (an index into a table of x and width, the state's byte 0x34 or 0x36) and a row offset its layout gives (the
// byte 0x35 or 0x37: 13 layouts, each a column of rows 36 high and its cursor: the state's byte 0x29 or its 16-bit word at 8 or
// 10). The rows' places here are those offsets as that code has them; the input (File 2 0x58D470's up and down) moves the same
// cursors.
#include "menus.h"

#include "ps2/iop.h"

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
    const char* netwk;      // the network overlay (as the disc names it)
    uint32_t state;         // the word holding the setup screens' state's address
    uint32_t barTemplate;   // the highlight bar's template (16-bit x, y, width, height first; File 2 0x5E36C0)
    uint32_t barPlaces;     // the bar's places: 16-bit x and width each (File 2 0x5E3BB8)
    Pointer::Sound move;    // the menus' cursor sound (File 2 0x58D380)
    uint32_t hintGlyphs;    // the caution's two key glyphs' sprites (20 bytes each; File 2 0x5E3920)
    // The broadband menu (the setup's screen 4, its step 2: File 2 0x58AE90): "Same as last time" and "Erase login ID" can
    // be chosen only while these bytes are set (the input skips them otherwise)
    uint32_t broadbandRows[2];
    // The browser: its overlay, and the word holding the address of its pointer's state (the pointer's x and y, 16 bits each
    // in the 640 x 448 picture, at 16 and 18 there: nethttp's link focus, File 2 0x567370, sets them so)
    const char* browser;
    uint32_t browserPointer;
};

const Layout File2 = {"BIN/3.DAT", 0x24b4e4, 0x5e36c0, 0x5e3bb8, {0x58d380}, 0x5e3920, {0x360701, 0x3603e0}, "BIN/5.DAT", 0x24b360};
const Layout File1 = {"PROG/NETWK.BIN", 0x23b618, 0x5e4be0, 0x5e50d8, {0x1b7a50, 0xc}, 0, {0x35eeee, 0x35ebd0}, "PROG/NETHTTP.BIN", 0x23c094};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

// The menus' layouts (File 2 0x58E630's cases): the cursor, and each row's offset from the bar template's y (36 high)
enum class CursorAt : uint8_t
{
    Choice,  // the state's byte 0x29 (the dialogs' yes and no)
    Word8,   // its 16-bit word at 8
    Word10,  // its 16-bit word at 10
};
struct MenuLayout
{
    CursorAt cursor;
    int rows;
    int16_t offset[5];
};
const MenuLayout Layouts[13] = {
    {CursorAt::Choice, 2, {0x42, 0x6e}},
    {CursorAt::Word8, 4, {-0x58, -0x34, -0x10, 0x14}},
    {CursorAt::Word8, 3, {-0x58, -0x10, 0x38}},
    {CursorAt::Word8, 2, {0x6e, 0x9a}},
    {CursorAt::Word10, 2, {0x42, 0x6e}},
    {CursorAt::Word8, 5, {-0x58, -0x34, -0x10, 0x14, 0x38}},
    {CursorAt::Word8, 3, {0x42, 0x6e, 0x9a}},
    {CursorAt::Word10, 3, {0x42, 0x6e, 0x9a}},
    {CursorAt::Word10, 2, {0x6e, 0x9a}},
    {CursorAt::Word8, 2, {0x42, 0x6e}},
    {CursorAt::Word10, 2, {-0x42, -0x16}},
    {CursorAt::Word10, 2, {0x42, 0x6e}},
    {CursorAt::Word10, 3, {0x16, 0x42, 0x6e}},
};
constexpr uint32_t Flags = 0x2C;
constexpr uint32_t UpperMenu = 0x200000, LowerMenu = 0x200;
constexpr uint32_t UpperPlace = 0x36, UpperLayout = 0x37, LowerPlace = 0x34, LowerLayout = 0x35;
constexpr uint32_t Choice = 0x29;
constexpr int Places = 3;
constexpr uint8_t BroadbandScreen = 4, BroadbandChoosing = 2;

// Whether a row of the menu up can be chosen (the broadband menu's second and third only when their settings are there)
bool Enabled(uint32_t state, int row)
{
    if (Byte(state) == BroadbandScreen && Byte(state + 1) == BroadbandChoosing && row >= 1 && row <= 2)
    {
        return Byte(L().broadbandRows[row - 1]) != 0;
    }
    return true;
}

uint32_t State()
{
    if (!Pointer::OverlayLoaded(L().netwk))
    {
        return 0;
    }
    uint32_t state = Word(L().state) & 0x1FFFFFF;
    return state != 0 && state < 0x1FFF000 ? state : 0;
}

bool SetupMenu(Pointer::Screen& screen)
{
    uint32_t state = State();
    if (state == 0)
    {
        return false;
    }
    uint32_t flags = Word(state + Flags);
    bool upper = (flags & UpperMenu) != 0;
    if (!upper && (flags & LowerMenu) == 0)
    {
        return false;
    }
    uint8_t place = Byte(state + (upper ? UpperPlace : LowerPlace));
    uint8_t which = Byte(state + (upper ? UpperLayout : LowerLayout));
    if (which >= sizeof(Layouts) / sizeof(Layouts[0]) || place >= Places)
    {
        static bool told = false;
        if (!told)
        {
            Ps2::Log("pointer: Network Play's menu has layout %u at place %u, not one the game's code has", which, place);
            told = true;
        }
        return false;
    }
    const MenuLayout& layout = Layouts[which];
    uint32_t cursor = state + (layout.cursor == CursorAt::Choice ? Choice : layout.cursor == CursorAt::Word8 ? 8 : 10);
    uint8_t size = layout.cursor == CursorAt::Choice ? 1 : 2;
    float x = Short(L().barPlaces + 4 * place), width = Short(L().barPlaces + 4 * place + 2);
    float top = Short(L().barTemplate + 2), height = Short(L().barTemplate + 6);
    Rect rows[5];
    for (int n = 0; n < layout.rows; n++)
    {
        rows[n] = Pointer::Box(x, top + layout.offset[n], width, height);
    }
    Pointer::Tile(rows, layout.rows, 4);
    screen.name = upper ? "Network Play's dialog" : "Network Play's menu";
    screen.moveSound = L().move;
    for (int n = 0; n < layout.rows; n++)
    {
        if (Enabled(state, n))
        {
            screen.Item(rows[n], cursor, size, n);
        }
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

// The caution screen's key hints, "Next" with the circle's glyph and "Back" with the cross's (the flag 0x10000: the glyphs'
// sprites at File 2 0x5E3920, x, y, width and height first; the words drawn left of them)
constexpr uint32_t CautionHints = 0x10000;

bool Hints(Pointer::Screen& screen)
{
    uint32_t state = State();
    if (state == 0 || L().hintGlyphs == 0 || (Word(state + Flags) & CautionHints) == 0)
    {
        return false;
    }
    screen.name = "Network Play's caution";
    const uint32_t buttons[2] = {Pointer::Circle, Pointer::Cross};
    for (int n = 0; n < 2; n++)
    {
        uint32_t glyph = L().hintGlyphs + 20 * static_cast<uint32_t>(n);
        Rect g = Pointer::Box(Short(glyph), Short(glyph + 2), Short(glyph + 4), Short(glyph + 6));
        screen.Press({g.x0 - 110, g.y0 - 6, g.x1 + 10, g.y1 + 6}, buttons[n]);
    }
    return true;
}

// A message with nothing to choose ("Press Confirm."; the flag 0x400, its box): a click anywhere is the confirm
constexpr uint32_t MessageBox = 0x400;

bool Message(Pointer::Screen& screen)
{
    uint32_t state = State();
    if (state == 0 || (Word(state + Flags) & MessageBox) == 0)
    {
        return false;
    }
    screen.name = "Network Play's message";
    screen.clickElsewhere = Pointer::Circle;
    return true;
}

// The browser's own pointer follows the Mac's (x and y in the 640 x 448 picture; the browser keeps it within its page); a
// click is its confirm there, the wheel scrolls its page (its L1 and R1)
bool Browser(Pointer::Screen& screen)
{
    if (!Pointer::OverlayLoaded(L().browser))
    {
        return false;
    }
    uint32_t state = Word(L().browserPointer) & 0x1FFFFFF;
    if (state == 0 || state >= 0x1FFFFE0)
    {
        return false;
    }
    screen.name = "network browser";
    screen.follow = state + 16;
    screen.clickElsewhere = Pointer::Circle;
    screen.wheelUp = Pointer::L1;
    screen.wheelDown = Pointer::R1;
    return true;
}
}

bool NetworkRunning() { return Pointer::OverlayLoaded(L().netwk); }

const Pointer::Finder NetFinders[] = {Browser, SetupMenu, Hints, Message};
const int NetFinderCount = sizeof(NetFinders) / sizeof(NetFinders[0]);
}
