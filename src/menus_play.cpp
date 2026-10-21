// The game's own screens over play (the game overlay's status task: File 2 GAME at 0x6C8FA0): the status screen with its ITEM,
// SPECIAL and FILE tabs, an item's actions, the file reader. The status task's state is a few bytes of the overlay's data
// (File 2 0x767250-0x767255: the tabs' level, the ITEM tab's own level and step, and what the task shows: 1 the status screen,
// 2 the map, 4 and 5 the pause) and a work area whose address the word at 0x24B22C holds. The status screen's handler (File 2
// 0x6BBDC0) runs the tab up: ITEM 0x6C17D0, SPECIAL 0x6C30C0, FILE 0x6C2D90.
#include "menus.h"
#include "text_map.h"

namespace Menus
{
namespace
{
using Pointer::Box;
using Pointer::Byte;
using Pointer::Half;
using Pointer::Rect;
using Pointer::Short;
using Pointer::Word;

struct Layout
{
    const char* game;        // the game overlay (as the disc names it)
    uint32_t statusTask;     // the status task's function (a task of the executable's list while it's up)
    uint32_t taskCount, taskList;
    uint32_t state;          // the status task's state bytes (File 2 0x767250: the tabs' level, ITEM's level and step, ...)
    uint32_t shows;          // the byte saying what the task shows (1 the status screen)
    uint32_t tab;            // the tab (0 ITEM, 1 SPECIAL, 2 FILE)
    uint32_t tabX;           // the tabs' x (three 16-bit words; each drawn 7 to the right, at y 28, 69 x 16: 0x6B9A00)
    uint32_t work;           // the word holding the work area's address
    uint32_t actions;        // the player's item action state (its byte 0 the grid's item, 1 the action, 4 the step)
    uint32_t itemCursor;     // the item the ITEM tab's grid is on (a byte)
    uint32_t weaponCursor;   // the equipped weapon's panel's cursor (a byte)
    uint32_t player;         // the byte saying which character the player is
    uint32_t slotBase;       // the inventories' places (16-bit x and y each, the player's first: 0x6BE460)
    uint32_t actionRows;     // the actions' rows' places (16-bit x and y each; 112 x 24: 0x6C52C0)
    uint32_t actionLists;    // the actions' lists (a word each: their actions' bytes, 0xFF ended)
    uint32_t actionList;     // the byte saying which list is up
    uint32_t giveCursor;     // the two-row giving lists' cursor (a byte)
    Pointer::Sound move;     // the status screen's cursor sound (File 2 0x1B7FD0)
    // SELECT's pause menu (what the task shows: 4): its rows' places, for each of the cursor's values the address of the
    // 16-bit x and y the game draws that row's setting or name at (File 2 0x6C8510, from its tables at 0x7402C0 and
    // 0x7402E0), 0 for a value the cursor skips
    uint32_t pauseRows[11];
    int pauseRowCount;
    int pauseSettings;       // the rows before this are settings (left and right change them), the rest are chosen
};

const Layout File2 = {"BIN/2.DAT", 0x6c8fa0, 0x24ad3c, 0x2aa890, 0x767250, 0x767254, 0x76a60e, 0x24a210, 0x24b22c,
                      0x38ae30, 0x38ae30, 0x76a60c, 0x4912bd, 0x740a90, 0x73fb60, 0x73f330, 0x767211, 0x7671f2, {0x1b7fd0},
                      {0x7402e0, 0x7402f0, 0, 0x740300, 0x740308, 0x740310, 0x740314, 0x740318, 0x7402d0, 0x7402d4, 0x7402d8},
                      11, 8};
// File 1's: the same code at its own addresses (its slots' places 0x6AEA40, its actions' list 0x6B5740, its tabs 0x6AA100;
// its pause has STEREO as its row 2, the rows' places at 0x6FF4B0 and 0x6FF4F0)
const Layout File1 = {"PROG/GAME.BIN", 0x6b9450, 0x23bb30, 0x29b0d0, 0x720be0, 0x720be4, 0x723f9e, 0x23b208, 0x23bfbc,
                      0x389740,    0x389740, 0x723f9c, 0x48bf7d, 0x6ffc80, 0x6fed50, 0x6fe520, 0x720ba1, 0x720b82, {0x1b6ec0},
                      {0x6ff4f0, 0x6ff500, 0x6ff510, 0x6ff514, 0x6ff51c, 0x6ff524, 0x6ff528, 0x6ff52c, 0x6ff4c0, 0x6ff4c4, 0x6ff4c8},
                      11, 8};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

bool StatusTaskUp()
{
    if (L().game == nullptr || !Pointer::OverlayLoaded(L().game))
    {
        return false;
    }
    uint32_t count = Word(L().taskCount);
    for (uint32_t n = 0; n < count && n < 8; n++)
    {
        if (Word(L().taskList + 4 * n) == L().statusTask)
        {
            return true;
        }
    }
    return false;
}

// The status task's state bytes (0x767250 in File 2): the tabs' level, ITEM's own (1 its normal step), ITEM's level (0 the
// grid, 1 an item's actions, 2 a pack's grid, 3 a pack's actions)
enum StateByte : uint32_t
{
    TabLevel = 0,
    ItemStep = 1,
    ItemLevel = 2,
};
constexpr uint8_t ShowsStatus = 1;
constexpr uint8_t GridStep = 1, WeaponStep = 2;
constexpr uint8_t ItemTab = 0, SpecialTab = 1, FileTab = 2;
constexpr int Tabs = 3;

// The tabs: a click on one goes there with R1 (the next) or L1 (the one before), the tab's own change (0x6BBB80)
void TabTargets(Pointer::Screen& screen, int tab)
{
    for (int n = 0; n < Tabs; n++)
    {
        int ahead = (n - tab + Tabs) % Tabs;
        if (ahead != 0)
        {
            screen.Press(Box(Short(L().tabX + 2 * static_cast<uint32_t>(n)) + 7, 24, 69, 24), ahead == 1 ? Pointer::R1 : Pointer::L1);
        }
    }
}

// The ITEM tab's grid: the character's items where the game puts its cursor for each (0x6BE460: walking the items before it,
// a cell 0x34 right, or down to the row's start after the second; an item of two cells, its byte's bit 0x80, a row down); the
// highlight the game draws for a cell is 6 right of and 10 above that place (measured in a picture of the screen), 56 x 52
void ItemTargets(Pointer::Screen& screen, uint32_t work)
{
    uint8_t character = Byte(work + 5);
    if (character != Byte(L().player))
    {
        return; // a partner's inventory: its place depends on the partners shown (not done)
    }
    uint8_t count = Byte(work + 0x3109 + character);
    uint32_t items = work + (static_cast<uint32_t>(character) * 0x38 + character) * 0x20 + 0x20;
    const float baseX = Short(L().slotBase), baseY = Short(L().slotBase + 2);
    const float x0 = baseX - 2;
    float x = x0, y = baseY + 0x30;
    unsigned cells = 0;
    for (int n = 0; n < count && n < 4; n++)
    {
        bool wide = (Byte(items + 0x98 * static_cast<uint32_t>(n)) & 0x80) != 0;
        screen.Item(Box(x + 6, y - 10, wide ? 0x34 + 56 : 56, 52), L().itemCursor, 1, n)
            .With(L().state + ItemStep, 1, GridStep)
            .With(L().state + ItemLevel, 1, 0);
        if (wide)
        {
            y += 0x34;
            x = x0;
            cells += 2;
        }
        else
        {
            if (cells % 2 == 0)
            {
                x += 0x34;
            }
            else
            {
                y += 0x34;
                x = x0;
            }
            cells += 1;
        }
    }
    // The equipped weapon's slot: ITEM's step 2, its own cursor 0 (as the grid's down from its last row goes there, and up
    // back), right of the grid's column a row below the grid
    screen.Item(Box(baseX + 0x32 + 6, baseY + 0x34 * static_cast<float>(count >> 1) + 0x34 - 10, 56, 52), L().weaponCursor, 1, 0)
        .With(L().state + ItemStep, 1, WeaponStep)
        .With(L().state + ItemLevel, 1, 0);
}

// An item's actions (Use, Equip, Combine, Give...): the rows of the list up (its bytes to 0xFF; 0x6C52C0 draws them), the
// cursor the action state's byte 1 for an item of the grid (0x6BF640, its step at byte 4 being 1), the work area's byte
// 0x303C for the weapon's panel (0x6C0520), the giving lists' own (0x6BF640 with the list 0x0F or 0x11)
void ActionTargets(Pointer::Screen& screen, uint32_t work, bool weapon)
{
    uint8_t which = Byte(L().actionList);
    uint32_t list = Word(L().actionLists + 4 * which) & 0x1FFFFFF;
    if (list == 0)
    {
        return;
    }
    uint32_t cursor = weapon ? work + 0x303c : L().actions + 1;
    if (!weapon && (which == 0x0f || which == 0x11))
    {
        cursor = L().giveCursor;
    }
    else if (!weapon && Byte(L().actions + 4) != 1)
    {
        return; // choosing a target for the action (an item to combine with, a partner): not rows of this list
    }
    for (int n = 0; n < 6 && Byte(list + static_cast<uint32_t>(n)) != 0xFF; n++)
    {
        uint32_t at = L().actionRows + 4 * static_cast<uint32_t>(n);
        screen.Item(Box(Short(at), Short(at + 2), 112, 24), cursor, 1, n);
    }
}

// SPECIAL's and FILE's lists: six rows from y 124, 24 apart (0x6C6240's highlight, 132 to 508), the row at the work area's
// byte 0x3076 and the first shown its 16-bit word at 0x307C; the arrows above and below scroll it
void ListTargets(Pointer::Screen& screen, uint32_t work)
{
    int count = Short(work + 0x307a);
    int first = Short(work + 0x307c);
    for (int n = 0; n < 6 && first + n < count; n++)
    {
        screen.Item(Box(132, 124 + 24 * static_cast<float>(n), 376, 24), work + 0x3076, 1, n);
    }
    screen.Press(Box(140, 92, 24, 24), Pointer::Up);
    screen.Press(Box(140, 284, 24, 24), Pointer::Down);
}

bool StatusScreen(Pointer::Screen& screen)
{
    if (!StatusTaskUp() || Byte(L().shows) != ShowsStatus)
    {
        return false;
    }
    uint32_t work = Word(L().work) & 0x1FFFFFF;
    if (work == 0)
    {
        return false;
    }
    int tab = Byte(L().tab);
    screen.name = "status screen";
    screen.moveSound = L().move;
    uint8_t level = Byte(L().state + TabLevel);
    if (tab == ItemTab)
    {
        uint8_t itemLevel = Byte(L().state + ItemLevel);
        uint8_t itemStep = Byte(L().state + ItemStep);
        if ((itemStep == GridStep || itemStep == WeaponStep) && (itemLevel == 1 || itemLevel == 3))
        {
            screen.name = "item's actions";
            ActionTargets(screen, work, itemStep == WeaponStep);
            screen.wheelUp = Pointer::Up;
            screen.wheelDown = Pointer::Down;
            return true;
        }
        uint8_t step = Byte(L().state + ItemStep);
        if ((step == GridStep || step == WeaponStep) && itemLevel == 0)
        {
            ItemTargets(screen, work);
        }
    }
    else if ((tab == SpecialTab || tab == FileTab) && level == 2)
    {
        ListTargets(screen, work);
        screen.wheelUp = Pointer::Up;
        screen.wheelDown = Pointer::Down;
    }
    else if (tab == FileTab && level == 3)
    {
        // The file reader: its pages turn with left and right (the work area's byte 0x307F), the wheel too
        screen.name = "file reader";
        screen.wheelUp = Pointer::Left;
        screen.wheelDown = Pointer::Right;
        screen.Press(Box(0, 60, 160, 300), Pointer::Left);
        screen.Press(Box(480, 60, 160, 300), Pointer::Right);
        return true;
    }
    TabTargets(screen, tab);
    if (screen.wheelUp == 0)
    {
        screen.wheelUp = Pointer::L1;
        screen.wheelDown = Pointer::R1;
    }
    return true;
}
}

// SELECT's pause menu (0x6C9870: the task showing 4, its tabs' level 1): the cursor is the status task's byte 1 (0-10, the
// settings first), its level byte 2 (0 the rows; with the cursor 9, quitting, 1 its YES/NO, the choice the byte 3: 0 YES)
constexpr uint8_t ShowsPause = 4;
constexpr int QuitRow = 9;

bool PauseMenu(Pointer::Screen& screen)
{
    if (!StatusTaskUp() || Byte(L().shows) != ShowsPause || Byte(L().state + TabLevel) != 1 || L().pauseRowCount == 0)
    {
        return false;
    }
    screen.moveSound = L().move;
    uint32_t cursor = L().state + ItemStep;
    uint8_t level = Byte(L().state + ItemLevel);
    if (Byte(cursor) == QuitRow && level == 1)
    {
        Rect yes, no;
        if (!TextMap::FindYesNo(yes, no))
        {
            return false;
        }
        screen.name = "pause menu's quit question";
        float middle = (yes.x1 + no.x0) / 2;
        screen.Item({yes.x0 - 8, yes.y0 - 6, middle, yes.y1 + 6}, L().state + 3, 1, 0);
        screen.Item({middle, no.y0 - 6, no.x1 + 8, no.y1 + 6}, L().state + 3, 1, 1);
        screen.wheelUp = Pointer::Left;
        screen.wheelDown = Pointer::Right;
        return true;
    }
    if (level != 0)
    {
        return false;
    }
    screen.name = "pause menu";
    for (int n = 0; n < L().pauseRowCount; n++)
    {
        uint32_t at = L().pauseRows[n];
        if (at == 0)
        {
            continue;
        }
        float y = Short(at + 2);
        Rect row = {130, y - 4, 520, y + 16};
        if (n < L().pauseSettings)
        {
            screen.Setting(row, cursor, 1, n, {row.x1 - 120, row.y0, row.x1 - 60, row.y1}, {row.x1 - 60, row.y0, row.x1, row.y1});
        }
        else
        {
            screen.Item(row, cursor, 1, n);
        }
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

const Pointer::Finder PlayFinders[] = {PauseMenu, StatusScreen};
const int PlayFinderCount = sizeof(PlayFinders) / sizeof(PlayFinders[0]);
}
