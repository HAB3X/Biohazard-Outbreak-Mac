// The selects before a scenario (the demo overlay's: the scenario select, the character select, the partners' select): they
// run in the select's own state (its address in the word at File 2 0x24B080, File 1 0x3065FC; its byte 8, File 1's 4, the
// screen: 8 the scenario select, 9 the character select, 10 the partners'), which keeps everything it draws as sprite records
// of 0x48 bytes one after the other (File 2 0x5518F0 makes them from the overlay's tables: shown while the 16-bit word at 2 is
// set; the picture's texture at 0x30 and its place in it at 0x28, 16-bit x, y, width, height; the place in the picture at
// 0x3C, the same; File 2 0x559A80 draws them). What each record is comes from its picture: the button hints along the
// bottom are rows of one texture (each 24 high: its key's glyph and words, as translations/file*/pictures.toml has them),
// the arrows one picture, the cards and portraits theirs. The selects move by their left and right (each with its own
// animation, so the pointer presses them rather than writing the selection), and choose by the confirm.
#include "menus.h"
#include "text_map.h"

#include <cmath>

namespace Menus
{
namespace
{
using Pointer::Box;
using Pointer::Byte;
using Pointer::Rect;
using Pointer::Short;
using Pointer::Word;

struct Range
{
    uint32_t first, end; // records' offsets in the state
};

struct Layout
{
    const char* overlay;
    uint32_t state;        // the word holding the select's state's address
    uint32_t screen;       // the state's byte saying which select
    uint32_t records;      // where the records start (the offset of one of them; the others 0x48 apart)
    Range scenario;        // the scenario select's records (its cards, then its frame, arrows, hints)
    Range characters;      // the character and partner selects' (their portraits, arrows, hints, panels)
    uint32_t hintTexture;  // the hints' texture (File 2 select.tex's first picture, File 1's sixth)
    // The difficulty's choice (after the character, the select's screen 11): a byte of the title's state (its address in
    // this word), 0 EASY
    uint32_t titleState, difficulty;
};

const Layout File2 = {"BIN/1.DAT", 0x24b080, 8, 0x998, {0x908, 0x25b8}, {0x2d98, 0x3da0}, 0, 0x24b070, 0xe};
const Layout File1 = {"PROG/DEMO.BIN", 0x3065fc, 4, 0x974, {0x8e4, 0x254c}, {0x2d74, 0x3d7c}, 5, 0x23be34, 0xd};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

constexpr uint32_t RecordBytes = 0x48;
constexpr uint8_t ScenarioSelect = 8, CharacterSelect = 9, PartnerSelect = 10, DifficultySelect = 11;

struct Record
{
    Rect place;
    int texture;
    int u, v, w, h;
};

bool Read(uint32_t at, Record& r)
{
    if (Short(at + 2) == 0)
    {
        return false;
    }
    r.place = Box(Short(at + 0x3c), Short(at + 0x3e), Short(at + 0x40), Short(at + 0x42));
    r.texture = static_cast<int>(Word(at + 0x30));
    r.u = Short(at + 0x28);
    r.v = Short(at + 0x2a);
    r.w = Short(at + 0x2c);
    r.h = Short(at + 0x2e);
    return r.place.x1 > 0 && r.place.x0 < 640 && r.place.y1 > 0 && r.place.y0 < 448;
}

// A hint's button by its row of the hints' picture (its v; the arrows' and the scroll's rows have none: the wheel and the
// arrows are those)
uint32_t HintButton(const Record& r)
{
    if (r.texture != static_cast<int>(L().hintTexture) || r.u != 0 || r.h != 24)
    {
        return 0;
    }
    switch (r.v)
    {
    case 0: return Pointer::Triangle;   // Switch item (File 2)
    case 296: return Pointer::Triangle; // Close window
    case 368: return Pointer::Triangle; // Info window
    case 392: return Pointer::Cross;    // Back to title
    case 464: return Pointer::Cross;    // Back
    case 488: return Pointer::Circle;   // OK
    default: return 0;                  // Scroll, Character select, Scenario select, Type select: the arrows' and wheel's
    }
}

bool IsArrow(const Record& r) { return r.u == 80 && r.v == 464 && r.w == 40 && r.h == 40; }
bool IsCard(const Record& r) { return r.u == 0 && r.v == 0 && r.w == 256 && r.h == 192; }
// (drawn 60 wide in File 2, 56 in File 1: other pictures of the size are drawn otherwise)
bool IsPortrait(const Record& r)
{
    float width = r.place.x1 - r.place.x0;
    return r.w == 56 && r.h == 128 && width >= 56 && width <= 60;
}
bool IsBigPortrait(const Record& r) { return r.u == 0 && r.v == 0 && r.w == 256 && r.h == 256; }
constexpr uint32_t Characters = 8;

uint32_t SelectState()
{
    if (!Pointer::OverlayLoaded(L().overlay))
    {
        return 0;
    }
    uint32_t state = Word(L().state) & 0x1FFFFFF;
    return state != 0 && state < 0x1FFF000 ? state : 0;
}

bool Select(Pointer::Screen& screen)
{
    uint32_t state = SelectState();
    if (state == 0)
    {
        return false;
    }
    uint8_t which = Byte(state + L().screen);
    const Range* range = which == ScenarioSelect ? &L().scenario
                         : which == CharacterSelect || which == PartnerSelect ? &L().characters
                                                                               : nullptr;
    if (range == nullptr)
    {
        return false;
    }
    // The records of this select, from the first at or after its range's start that lines up with the others
    uint32_t first = range->first + (L().records % RecordBytes + RecordBytes - range->first % RecordBytes) % RecordBytes;
    Record records[96];
    int count = 0;
    for (uint32_t at = first; at < range->end && count < 96; at += RecordBytes)
    {
        if (Read(state + at, records[count]))
        {
            count++;
        }
    }
    // The current card (the biggest) and the selected portrait's highlight (the first record of the portraits' array, its
    // place the selected slot's): the others are steps away from them
    const Record* current = nullptr;
    for (int n = 0; n < count; n++)
    {
        if (IsCard(records[n]) && (current == nullptr || records[n].place.x1 - records[n].place.x0 > current->place.x1 - current->place.x0))
        {
            current = &records[n];
        }
    }
    // The selected portrait's highlight: one record for each character at the array's start, the selected one's shown, at
    // the selected slot (the portraits scroll under it)
    Record highlight;
    bool portraits = false;
    for (uint32_t n = 0; n < Characters && which != ScenarioSelect && !portraits; n++)
    {
        portraits = Read(state + range->first + RecordBytes * n, highlight) && IsPortrait(highlight) && highlight.v == 128;
    }
    int hints = 0;
    for (int n = 0; n < count; n++)
    {
        const Record& r = records[n];
        if (uint32_t button = HintButton(r))
        {
            screen.Press({r.place.x0, r.place.y0 - 4, r.place.x1, r.place.y1 + 4}, button);
            hints++;
        }
        else if (IsArrow(r))
        {
            screen.Press({r.place.x0 - 6, r.place.y0 - 6, r.place.x1 + 6, r.place.y1 + 6}, r.place.x0 < 320 ? Pointer::Left : Pointer::Right);
        }
        else if (IsCard(r) && current != nullptr)
        {
            float middle = (current->place.x0 + current->place.x1) / 2;
            screen.Press(r.place, &r == current ? Pointer::Circle : r.place.x0 < middle ? Pointer::Left : Pointer::Right);
        }
        else if (portraits && IsBigPortrait(r))
        {
            screen.Press(r.place, Pointer::Circle);
        }
        else if (portraits && IsPortrait(r) && r.v == 0)
        {
            // Steps from the selected slot (the portraits are 72 apart): one press a step
            int steps = static_cast<int>(std::lround((r.place.x0 - highlight.place.x0) / 72.0f));
            if (steps == 0)
            {
                screen.Press(r.place, Pointer::Circle);
            }
            else
            {
                screen.PressTimes(r.place, steps > 0 ? Pointer::Right : Pointer::Left, std::abs(steps));
            }
        }
    }
    if (hints == 0)
    {
        return false; // not drawn yet (or a select this doesn't know): the records are another screen's
    }
    screen.name = which == ScenarioSelect ? "scenario select" : which == CharacterSelect ? "character select" : "partner select";
    screen.wheelUp = Pointer::Left;
    screen.wheelDown = Pointer::Right;
    // Each step animates (the select's step byte, after its screen byte, is 1 again when it takes the next)
    screen.readyAt = state + L().screen + 1;
    screen.readyValue = 1;
    return true;
}

// The difficulty: its words drawn as text, each in a frame (measured in a picture: x 60 to 308, from 6 above the word to 2
// below it); the pointer over one writes the choice, as the game's up and down do
bool Difficulty(Pointer::Screen& screen)
{
    uint32_t state = SelectState();
    uint32_t title = Word(L().titleState) & 0x1FFFFFF;
    if (state == 0 || title == 0 || Byte(state + L().screen) != DifficultySelect)
    {
        return false;
    }
    static const char* const Levels[] = {"EASY", "NORMAL", "HARD", "VERY HARD"};
    Rect rows[4];
    int found = 0;
    for (const char* level : Levels)
    {
        Rect word;
        if (!TextMap::FindBytes(level, word))
        {
            break;
        }
        rows[found++] = {60, word.y0 - 6, 308, word.y1 + 2};
    }
    if (found == 0)
    {
        return false;
    }
    Pointer::Tile(rows, found);
    screen.name = "difficulty";
    for (int n = 0; n < found; n++)
    {
        screen.Item(rows[n], title + L().difficulty, 1, n);
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}
}

const Pointer::Finder SelectFinders[] = {Difficulty, Select};
const int SelectFinderCount = sizeof(SelectFinders) / sizeof(SelectFinders[0]);
}
