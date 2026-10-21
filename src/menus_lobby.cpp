// Network Play's screens after login (the lobby: SELECT CAPCOM ID, SELECT SERVER, the area's ROOM LIST / DATABASE / PERSONAL
// RANK, the room list, a room's RULE and its own screen, their dialogs). All of them are drawn from one pool of parts (File 2
// 64 of 0x54 bytes at 0x6358A0, File 1 of 0x4C at 0x639AF0, drawn by the task File 2 0x549370, File 1 0x548140): each part
// is in use (byte 0), shown (byte 1), has a step function (the word at 12), a graphic (16 bits at 0x22, an index into the
// graphics' table: File 2 0x5D8F00, File 1 0x5D9FC0), a place (16-bit x and y at 0x26 and 0x28) and a depth (the word at
// 0x34: the dialogs' -26 is in front). A graphic is a run of 20-byte pieces: more to come (byte 0), its texture (byte 1),
// flags (16 bits at 2), its offset from the part's place and its size (16 bits each at 4, 6, 8 and 10; the part's own size,
// at 0x3C and 0x3E, when the part's flags and the piece's differ in 0x200), and its texture place (12 to 18).
//
// The menus are built from tables by kind (File 2 0x54F460, File 1 0x550220): each item a part whose step function is one of
// a few (File 2 0x54F620, 0x54FB40, 0x54FD00, 0x54FE90, 0x54FFE0) with the item's number (byte 3) and the address of the
// menu's cursor (the word at 0x10); the item is highlighted while that cursor holds its number. The cursors move by a few
// engines (File 2 0x5477B0, 0x547AD0, 0x547D00 a byte, 0x547940 a 16-bit word, each with the last value and whether it moves
// across or down; 0x547F80 a list's row in a window of a longer list), whose overrides keep, each frame, which cursors the
// game moved and how far they go: a menu is pointed at only while the game moves its cursor, so a menu under a dialog isn't.
// The pointer over an item puts its number in the cursor, a click also presses the confirm. Arrows are parts with an arrow's
// graphic, shown while there's somewhere to go; the key hints along the bottom are pieces of one texture (rfff.afs's
// lobby_03, the game's texture 2 here), a row of it for each hint (translations/pictures-shared.toml has its boxes). The room
// list's rows and a room's own screen (its member slots and buttons) have step functions of their own (File 2 0x5A9DF0 and
// 0x5A6810: the list's row byte 0x62E239, the room's cursor 0x62E6EF, its member slots first).
#include "menus.h"

#include <algorithm>

namespace Menus
{
namespace
{
using Pointer::Byte;
using Pointer::Half;
using Pointer::Rect;
using Pointer::Short;
using Pointer::Word;

constexpr int ItemSteps = 5;

struct Layout
{
    const char* netwk;
    uint32_t taskCount, taskList, drawTask;
    uint32_t parts, partBytes, graphics;
    uint32_t itemSteps[ItemSteps];
    uint16_t arrows[4];            // the graphics of the up, down, left and right arrows the menus' kinds use
    uint32_t roomRows, rowCursor;  // the room list's rows' step and its row byte (0: not found)
    uint32_t roomScreen, roomCursor, members;  // a room's screen's parts' step, its cursor, its member count (0: not found)
    uint32_t frameCount;
    Pointer::Sound move;
};

const Layout File2 = {"BIN/3.DAT", 0x24ad3c, 0x2aa890, 0x549370, 0x6358a0, 0x54, 0x5d8f00,
                      {0x54f620, 0x54fb40, 0x54fd00, 0x54fe90, 0x54ffe0}, {0xd7, 0xd8, 0xd9, 0xda},
                      0x5a9df0, 0x62e239, 0x5a6810, 0x62e6ef, 0x62d87e, 0x24adb4, {0x1b8010, 0}};
const Layout File1 = {"PROG/NETWK.BIN", 0x23bb30, 0x29b0d0, 0x548140, 0x639af0, 0x4c, 0x5d9fc0,
                      {0x5503e0, 0x550880, 0x550a40, 0x550bd0, 0x550d20}, {0xc2, 0xc3, 0xc4, 0xc5},
                      0x5af010, 0x62e675, 0, 0, 0, 0x23bba4, {0x1b6f00, 0}};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

constexpr int Parts = 64, MaxPieces = 16;
constexpr uint32_t Step = 0xc, CursorWord = 0x10, Graphic = 0x22, Flags = 0x24, X = 0x26, Y = 0x28, Depth = 0x34;
constexpr uint32_t OwnWidth = 0x3c, OwnHeight = 0x3e;
constexpr uint32_t InFront = 0xffffffe6;
constexpr uint16_t OwnSize = 0x200, Untextured = 0x800;
// Arrows both games' kinds 0x20 and 0x21 use (SELECT SERVER's card, a list's)
constexpr uint16_t CardLeft = 0x23, CardRight = 0x24, ListUp = 0x29, ListDown = 0x2a;
constexpr uint8_t HintTexture = 2;
constexpr float HintRow = 24;
constexpr int RoomSlots = 6, RoomButtons = 6;

// The cursors the game's engines moved in a frame (kept by the engines' overrides, by the game's frame count)
struct Live
{
    uint32_t cursor;
    uint8_t size;
    int last;      // the cursor's last value
    bool across;   // it moves left and right
};
constexpr int MaxLive = 16;
struct Frame
{
    uint32_t count = ~0u;
    int live = 0;
    Live cursors[MaxLive];
};
Frame g_frames[2];
int g_current = 0;

void Keep(const Live& live)
{
    uint32_t count = Word(L().frameCount);
    if (g_frames[g_current].count != count)
    {
        g_current ^= 1;
        g_frames[g_current].count = count;
        g_frames[g_current].live = 0;
    }
    Frame& frame = g_frames[g_current];
    for (int n = 0; n < frame.live; n++)
    {
        if (frame.cursors[n].cursor == live.cursor)
        {
            return;
        }
    }
    if (frame.live < MaxLive)
    {
        frame.cursors[frame.live++] = live;
    }
}

// The cursors moved in the last finished frame
const Frame& LastFrame()
{
    uint32_t count = Word(L().frameCount);
    const Frame& current = g_frames[g_current];
    return current.count == count ? g_frames[g_current ^ 1] : current;
}

const Live* FindLive(const Frame& frame, uint32_t cursor)
{
    for (int n = 0; n < frame.live; n++)
    {
        if (frame.cursors[n].cursor == cursor)
        {
            return &frame.cursors[n];
        }
    }
    return nullptr;
}

bool Drawing()
{
    if (!Pointer::OverlayLoaded(L().netwk))
    {
        return false;
    }
    uint32_t count = Word(L().taskCount);
    for (uint32_t n = 0; n < count && n < 32; n++)
    {
        if (Word(L().taskList + 4 * n) == L().drawTask)
        {
            return true;
        }
    }
    return false;
}

uint32_t Part(int n) { return L().parts + L().partBytes * static_cast<uint32_t>(n); }
bool Shown(uint32_t part) { return (Byte(part) & Byte(part + 1)) != 0; }
bool Front(uint32_t part) { return Word(part + Depth) == InFront; }

// Calls `piece` with each piece of a part's graphic: its place, flags, texture and texture place
template <typename Piece>
void Pieces(uint32_t part, Piece piece)
{
    uint32_t at = Word(L().graphics + 4 * static_cast<uint32_t>(Half(part + Graphic))) & 0x1FFFFFF;
    if (at == 0)
    {
        return;
    }
    float x = Short(part + X), y = Short(part + Y);
    uint16_t partFlags = Half(part + Flags);
    for (int n = 0; n < MaxPieces; n++, at += 20)
    {
        uint16_t flags = Half(at + 2);
        bool own = ((flags ^ partFlags) & OwnSize) != 0;
        float w = own ? Short(part + OwnWidth) : Short(at + 8);
        float h = own ? Short(part + OwnHeight) : Short(at + 10);
        if (w > 0 && h > 0)
        {
            piece(Pointer::Box(x + Short(at + 4), y + Short(at + 6), w, h), flags, Byte(at + 1), at + 12);
        }
        if (Byte(at) == 0)
        {
            return;
        }
    }
}

Rect Bounds(uint32_t part)
{
    Rect all = {0, 0, 0, 0};
    bool any = false;
    Pieces(part, [&](const Rect& r, uint16_t, uint8_t, uint32_t) {
        all = any ? Rect{std::min(all.x0, r.x0), std::min(all.y0, r.y0), std::max(all.x1, r.x1), std::max(all.y1, r.y1)} : r;
        any = true;
    });
    return all;
}

// The key a spot of the hints' texture shows (its rows 24 high, each one hint: lobby_03's boxes)
uint32_t HintKey(float u, float v)
{
    switch (static_cast<int>(v / HintRow))
    {
    case 0: return Pointer::Start;     // Sub-window
    case 1: return Pointer::Triangle;  // Soft keyboard
    case 2: return Pointer::Square;    // Chat log
    case 3: return Pointer::Triangle;  // Scenario info
    case 4:                            // Member list, Rules, Switch member: L1 back, R1 on
    case 5:
    case 6: return u < 24 ? Pointer::L1 : Pointer::R1;
    case 7: return u < 24 ? Pointer::L2 : u < 151 ? Pointer::R2 : Pointer::Start;  // Next page; Close
    case 8: return u < 150 ? Pointer::Right : Pointer::Cross;                    // Registration (left/right); Quit
    case 9: return u < 150 ? Pointer::Cross : Pointer::Circle;                   // Back; OK
    default: return 0;
    }
}

void Hints(Pointer::Screen& screen, uint32_t part)
{
    Pieces(part, [&](const Rect& r, uint16_t flags, uint8_t texture, uint32_t uv) {
        if (texture != HintTexture || (flags & Untextured) != 0)
        {
            return;
        }
        float u0 = Short(uv), v0 = Short(uv + 2), uw = Short(uv + 4);
        if (uw <= 0)
        {
            return;
        }
        // The piece cut where its key changes along the texture's row (an L1 and an R1 side by side, a hint and the next)
        float scale = (r.x1 - r.x0) / uw;
        float from = r.x0;
        uint32_t key = HintKey(u0, v0);
        for (float u = u0 + 1; u <= u0 + uw; u++)
        {
            uint32_t next = u < u0 + uw ? HintKey(u, v0) : 0;
            if (next != key)
            {
                float to = r.x0 + (u - u0) * scale;
                if (key != 0)
                {
                    screen.Press({from, r.y0, to, r.y1}, key);
                }
                from = to;
                key = next;
            }
        }
    });
}

uint32_t ArrowKey(uint16_t graphic)
{
    const uint16_t* a = L().arrows;
    if (graphic == a[0] || graphic == ListUp) return Pointer::Up;
    if (graphic == a[1] || graphic == ListDown) return Pointer::Down;
    if (graphic == a[2] || graphic == CardLeft) return Pointer::Left;
    if (graphic == a[3] || graphic == CardRight) return Pointer::Right;
    return 0;
}

// An item: the parts with one cursor and number, their places joined
struct Item
{
    uint32_t cursor;
    uint8_t size;
    int value;
    Rect rect;
    bool front;
};
constexpr int MaxItems = 64;

void Join(Item* items, int& count, uint32_t cursor, uint8_t size, int value, const Rect& rect, bool front)
{
    if (rect.x1 <= rect.x0)
    {
        return;
    }
    for (int n = 0; n < count; n++)
    {
        Item& item = items[n];
        if (item.cursor == cursor && item.value == value)
        {
            item.rect = {std::min(item.rect.x0, rect.x0), std::min(item.rect.y0, rect.y0), std::max(item.rect.x1, rect.x1),
                         std::max(item.rect.y1, rect.y1)};
            item.front = item.front || front;
            return;
        }
    }
    if (count < MaxItems)
    {
        items[count++] = {cursor, size, value, rect, front};
    }
}

bool IsItemStep(uint32_t step)
{
    for (uint32_t s : L().itemSteps)
    {
        if (s == step)
        {
            return true;
        }
    }
    return false;
}

bool Lobby(Pointer::Screen& screen)
{
    if (!Drawing())
    {
        return false;
    }
    const Frame& frame = LastFrame();
    Item items[MaxItems];
    int count = 0;
    bool anyFront = false;
    bool roomUp = false;
    for (int n = 0; n < Parts; n++)
    {
        uint32_t part = Part(n);
        if (Byte(part) == 0)
        {
            continue;
        }
        uint32_t step = Word(part + Step);
        int number = static_cast<int8_t>(Byte(part + 3));
        uint32_t cursor = 0;
        if (IsItemStep(step))
        {
            cursor = Word(part + CursorWord) & 0x1FFFFFF;
        }
        else if (L().roomRows != 0 && step == L().roomRows)
        {
            cursor = L().rowCursor;
        }
        else if (L().roomScreen != 0 && step == L().roomScreen)
        {
            // A room's screen: its member slots (as many as there are members) then its buttons, one cursor through them
            roomUp = true;
            int members = std::min<int>(Byte(L().members), RoomSlots);
            if (number < members || (number >= RoomSlots && number < RoomSlots + RoomButtons))
            {
                Join(items, count, L().roomCursor, 1, number < RoomSlots ? number : members + number - RoomSlots, Bounds(part),
                     Front(part));
            }
            continue;
        }
        const Live* live = cursor != 0 ? FindLive(frame, cursor) : nullptr;
        if (live == nullptr || number < 0 || number > live->last)
        {
            continue;
        }
        Join(items, count, cursor, live->size, number, Bounds(part), Front(part));
        anyFront = anyFront || Front(part);
    }
    if (count == 0 && !roomUp)
    {
        return false;
    }
    screen.name = "Network Play's lobby";
    screen.moveSound = L().move;
    // Arrows and key hints first (an arrow is over its row), the dialog's alone while one is up
    for (int n = 0; n < Parts; n++)
    {
        uint32_t part = Part(n);
        if (!Shown(part) || (anyFront && !Front(part)))
        {
            continue;
        }
        uint32_t key = ArrowKey(Half(part + Graphic));
        if (key != 0)
        {
            screen.Press(Bounds(part), key);
        }
        else
        {
            Hints(screen, part);
        }
    }
    for (int n = 0; n < count; n++)
    {
        if (!anyFront || items[n].front)
        {
            screen.Item(items[n].rect, items[n].cursor, items[n].size, items[n].value);
        }
    }
    // The wheel moves the cursors the game moves: down a list where one goes down, else across
    bool down = false;
    for (int n = 0; n < frame.live; n++)
    {
        down = down || !frame.cursors[n].across;
    }
    screen.wheelUp = down ? Pointer::Up : Pointer::Left;
    screen.wheelDown = down ? Pointer::Down : Pointer::Right;
    return true;
}
}

void LobbyCursor(Ps2Cpu* c, LobbyEngine engine, void (*original)(Ps2Cpu*))
{
    uint32_t a0 = c->r[4].uw[0] & 0x1FFFFFF, a1 = c->r[5].uw[0], a2 = c->r[6].uw[0] & 0xFF, a3 = c->r[7].uw[0] & 0xFF;
    switch (engine)
    {
    case LobbyEngine::Byte:
        Keep({a0, 1, static_cast<int>(a1 & 0xFF), a2 == 1 || a2 == 3});
        break;
    case LobbyEngine::Half:
        Keep({a0, 2, static_cast<int>(a1 & 0xFFFF), a2 == 1 || a2 == 3});
        break;
    case LobbyEngine::List:
        // The row in the window (a1), as far as the window or the list goes
        Keep({a1 & 0x1FFFFFF, 1, static_cast<int>(std::min(a2, a3)) - 1, false});
        break;
    }
    original(c);
}

const Pointer::Finder LobbyFinders[] = {Lobby};
const int LobbyFinderCount = sizeof(LobbyFinders) / sizeof(LobbyFinders[0]);
}
