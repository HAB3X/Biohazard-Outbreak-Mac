// The collection (the demo overlay's, from the title menu's COLLECTION): its grid of items under five tabs (GALLERY,
// COSTUME, MOVIE, SOUND, EXTRA) and its questions (buying an item, returning to the title menu). Its state: File 2's at the
// address the word at 0x24B074 holds while its grid's task (0x549DA0) runs, File 1's at 0x5A4420 while its byte 1 is 2 (the
// grid) or a question's; the cursor (the cell's index on its tab), the tab and the first cell shown are bytes of it (memory
// diffs of the grid's moves, tabs and scrolling). Where the grid and tabs are drawn was measured in pictures of the screen
// (both games draw them alike: 8 cells a row 72 apart, two rows shown). The questions' Yes and No are found where the game
// drew them (src/text_map.h), the choice the state's byte 5 in File 1 (0 Yes; 0x553740 toggles it).
#include "menus.h"
#include "text_map.h"

namespace Menus
{
namespace
{
using Pointer::Box;
using Pointer::Byte;
using Pointer::Rect;
using Pointer::Word;

struct Layout
{
    const char* overlay;
    uint32_t stateWord;      // the word holding the state's address (0: the state is at `state`)
    uint32_t state;
    uint32_t gridTask;       // the grid's task (0: told by the state's byte 1)
    uint32_t taskCount, taskList;
    uint32_t cursor, tab, first;
    uint32_t choice;         // the questions' choice (0: not found)
    Pointer::Sound move;
};

const Layout File2 = {"BIN/1.DAT", 0x24b074, 0, 0x549da0, 0x24ad3c, 0x2aa890, 0x18, 0x10, 0x28, 0, {0x1b8000, 2}};
const Layout File1 = {"PROG/DEMO.BIN", 0, 0x5a4420, 0, 0x23bb30, 0x29b0d0, 6, 4, 0x10, 5, {0x1b6ef0, 2}};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

constexpr int Columns = 8, ShownRows = 2, Tabs = 5;
constexpr uint8_t File1Grid = 2;
const Rect TabPlaces[Tabs] = {Box(25, 96, 70, 18), Box(105, 96, 60, 18), Box(178, 96, 44, 18), Box(259, 96, 48, 18),
                              Box(337, 96, 40, 18)};

bool TaskUp(uint32_t task)
{
    uint32_t count = Word(L().taskCount);
    for (uint32_t n = 0; n < count && n < 8; n++)
    {
        if (Word(L().taskList + 4 * n) == task)
        {
            return true;
        }
    }
    return false;
}

uint32_t State()
{
    if (!Pointer::OverlayLoaded(L().overlay) || !TitleRunning())
    {
        return 0;
    }
    if (L().stateWord == 0)
    {
        return L().state;
    }
    uint32_t state = Word(L().stateWord) & 0x1FFFFFF;
    return state != 0 && state < 0x1FFF000 ? state : 0;
}

bool Question(Pointer::Screen& screen)
{
    uint32_t state = State();
    Rect yes, no;
    if (state == 0 || L().choice == 0 || !TextMap::FindYesNo(yes, no))
    {
        return false;
    }
    screen.name = "collection's question";
    screen.moveSound = L().move;
    float middle = (yes.x1 + no.x0) / 2;
    screen.Item({yes.x0 - 12, yes.y0 - 6, middle, yes.y1 + 6}, state + L().choice, 1, 0);
    screen.Item({middle, no.y0 - 6, no.x1 + 12, no.y1 + 6}, state + L().choice, 1, 1);
    screen.wheelUp = Pointer::Left;
    screen.wheelDown = Pointer::Right;
    return true;
}

bool Grid(Pointer::Screen& screen)
{
    uint32_t state = State();
    if (state == 0 || (L().gridTask != 0 ? !TaskUp(L().gridTask) : Byte(state + 1) != File1Grid))
    {
        return false;
    }
    screen.name = "collection";
    screen.moveSound = L().move;
    int tab = Byte(state + L().tab);
    for (int n = 0; n < Tabs; n++)
    {
        int ahead = (n - tab + Tabs) % Tabs;
        if (ahead != 0)
        {
            screen.PressTimes(TabPlaces[n], ahead <= Tabs / 2 ? Pointer::R1 : Pointer::L1, ahead <= Tabs / 2 ? ahead : Tabs - ahead);
        }
    }
    int firstRow = Byte(state + L().first) / Columns;
    for (int r = 0; r < ShownRows; r++)
    {
        for (int c = 0; c < Columns; c++)
        {
            screen.Item(Box(52 + 72 * c - 36, 156 + 72 * r - 36, 72, 72), state + L().cursor, 1, (firstRow + r) * Columns + c);
        }
    }
    // The scroll bar's arrows, and the hints along the bottom: Q R switch item (the tabs), E OK, Esc Back
    screen.Press(Box(596, 118, 24, 22), Pointer::Up);
    screen.Press(Box(596, 240, 24, 22), Pointer::Down);
    screen.Press(Box(156, 420, 150, 24), Pointer::R1);
    screen.Press(Box(326, 420, 50, 24), Pointer::Circle);
    screen.Press(Box(412, 420, 72, 24), Pointer::Cross);
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}
}

const Pointer::Finder CollectionFinders[] = {Question, Grid};
const int CollectionFinderCount = sizeof(CollectionFinders) / sizeof(CollectionFinders[0]);
}
