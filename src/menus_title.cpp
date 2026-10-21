// The title menu and its submenus (the demo overlay's: File 2 BIN/1.DAT, File 1 PROG/DEMO.BIN). They share one state block,
// whose address a word of the executable's data holds while the title runs (File 2 0x24B070, File 1 0x23BE34; set by the
// title's start, File 2 0x5430D0): its byte 0 says which menu (0 PRESS START and the attract movies, 1 the title menu, 2 a
// submenu), byte 1 that menu's step, byte 0xC the cursor, the word at 0x64 the title row chosen (which submenu: 0 single play, 1
// network play, 2 the collection, 3 the character log, 4 the options, 5 the hard disk's install; the dispatch at File 2
// 0x543A50) and the word at 0x20C the title menu's row count. The title menu and the two-row submenus draw their rows as
// sprites the block keeps: 13 records of 0x14 bytes from 0xC4 (x, y, width and height as 16-bit words in the 640 x 448
// picture; the title rows from record 4, File 2 0x5507B0 makes them), each shown while its byte from 0x74 is set.
#include "menus.h"

#include <cstring>

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
    const char* overlay;
    uint32_t state;      // the word holding the title menus' state block's address
    uint32_t subState;   // the word holding the character log's and options' own state (inside the block)
    // The options' list: its row count and which layout it uses (the word at these offsets of the options' state: 1 with the
    // network adaptor's row), and the two layouts' row frames (x, y, width, height, 16-bit words; File 2 0x55CB80 draws them)
    uint32_t optionsCount, optionsMode;
    uint32_t optionsFrames[2];
    // The character log's two rows' frames (six 16-bit words a row, the frame's x, y, width and height from the third; File 2
    // 0x563000 draws them)
    uint32_t charLogFrames;
    // The character log's pages (File 2 0x5638A0 draws them; 12-byte entries, the frame's x, y, width and height from the
    // third 16-bit word): the tabs, the scroll bar (its up arrow, track and down arrow) and the key hints' glyphs (L1
    // R1, cross)
    uint32_t charLogTabs;
    int charLogPages;  // File 2's three tabs, File 1's five
    uint32_t charLogScroll, charLogHintGlyphs;
    // The options' screens' frame tables (File 2 drawn by 0x55DF80, 0x55D0C0, 0x55E5B0, 0x55EB50): sound (its rows' frames
    // from entry 8, its value arrows 11 and 12, the volume bars' first segment 13), brightness (its slider 14-16, its
    // marker 17, the marker's step a float of the executable's data), the screen's position (its arrows 16-19), save/load
    // (its two rows from entry 6)
    uint32_t soundFrames, brightnessFrames, brightnessStep, screenFrames, saveLoadFrames;
    // The settings' values in the options' state, from this offset: the output, the two volumes (0-7), two more, then the
    // brightness (0x80 the middle) and the screen's position
    uint32_t settings;
    // The menus' cursor sound (File 2 0x1B8000 with 2, as the title menu's moves play it)
    uint32_t moveSound;
    // File 2's collection starts with two rows (COLLECTION, DATA CONVERT, 0x551370); File 1's is its grid at once (0x542490)
    bool collectionFirstMenu;
    // The network configuration's question's place is File 2's (File 1's options never showed its row here)
    bool networkQuestion;
};

const Layout File2 = {"BIN/1.DAT", 0x24b070, 0x24b08c, 0x170, 0x174, {0x56a520, 0x56a570}, 0x56b1c4, 0x56b2dc, 3, 0x56b360,
                     0x56b348, 0x56ab10, 0x56a8e0, 0x249220, 0x56a9f0, 0x56ac50, 0x14, 0x1b8000, true, true};
const Layout File1 = {"PROG/DEMO.BIN", 0x23be34, 0x23be40, 0x168, 0x16c, {0x5599b0, 0x559a00}, 0x55a644, 0x55a750, 5, 0x55a7e0,
                     0x55a7c8, 0x559fa0, 0x559d70, 0x239cc4, 0x559e80, 0x55a0e0, 0xc, 0x1b6ef0, false, false};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

// The title's menus' own cursor sound
Pointer::Sound MoveSound() { return {L().moveSound, 2}; }

enum TitleMenu : uint8_t
{
    PressStart = 0,
    Main = 1,
    Submenu = 2,
};
enum Row : uint32_t
{
    SinglePlay = 0,
    Collection = 2,
    CharacterLog = 3,
    Options = 4,
};
// Each menu's step taking the pad (File 2: the title menu's 5 at 0x543420, the two-row submenus' 2 at 0x550D50 and 0x551370,
// the character log's list 1 at 0x5627B0, the options' list 3 at 0x55C7D0)
constexpr uint8_t MainChoosing = 5, TwoRowChoosing = 2, CharLogChoosing = 1, OptionsChoosing = 3;
constexpr uint32_t Cursor = 0xC, ChosenRow = 0x64, RowCount = 0x20C, Sprites = 0xC4, SpriteShown = 0x74;
constexpr uint32_t SpriteBytes = 0x14;
constexpr int FirstRowSprite = 4;
// The submenus' cursor (a 16-bit word of their own state)
constexpr uint32_t SubCursor = 4;

// The title menus' state block, 0 when the title isn't running
uint32_t State()
{
    if (!Pointer::OverlayLoaded(L().overlay))
    {
        return 0;
    }
    uint32_t state = Word(L().state) & 0x1FFFFFF;
    return state != 0 && state < 0x1FFF000 ? state : 0;
}

Rect Sprite(uint32_t state, int n)
{
    uint32_t at = state + Sprites + SpriteBytes * static_cast<uint32_t>(n);
    return Box(Short(at), Short(at + 2), Short(at + 4), Short(at + 6));
}

// `count` rows drawn as the sprites from FirstRowSprite, the cursor a byte of the block
bool SpriteRows(Pointer::Screen& screen, uint32_t state, int count)
{
    Rect rows[8];
    if (count < 1 || count > 8)
    {
        return false;
    }
    for (int n = 0; n < count; n++)
    {
        if (Byte(state + SpriteShown + FirstRowSprite + n) == 0)
        {
            return false;
        }
        rows[n] = Sprite(state, FirstRowSprite + n);
    }
    Pointer::Tile(rows, count);
    for (int n = 0; n < count; n++)
    {
        screen.Item(rows[n], state + Cursor, 1, n);
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

bool MainMenu(Pointer::Screen& screen)
{
    uint32_t state = State();
    if (state == 0 || Byte(state) != Main || Byte(state + 1) != MainChoosing)
    {
        return false;
    }
    screen.name = "title menu";
    screen.moveSound = MoveSound();
    return SpriteRows(screen, state, static_cast<int>(Word(state + RowCount)));
}

// Single play's NEW GAME / CONTINUE (File 1 SCENARIO / ???) and File 2's COLLECTION / DATA CONVERT
bool TwoRowSubmenu(Pointer::Screen& screen)
{
    uint32_t state = State();
    uint32_t row = state != 0 ? Word(state + ChosenRow) : ~0u;
    bool twoRows = row == SinglePlay || (row == Collection && L().collectionFirstMenu);
    if (state == 0 || Byte(state) != Submenu || !twoRows || Byte(state + 1) != TwoRowChoosing)
    {
        return false;
    }
    screen.name = row == SinglePlay ? "single play" : "collection's first menu";
    return SpriteRows(screen, state, 2);
}

uint32_t SubState()
{
    uint32_t sub = Word(L().subState) & 0x1FFFFFF;
    return sub != 0 && sub < 0x1FFF000 ? sub : 0;
}

// Rows framed by a table of the overlay's data (16-bit x, y, width, height at `frame` in each `stride` bytes)
void FramedRows(Pointer::Screen& screen, uint32_t table, uint32_t stride, uint32_t frame, int count, uint32_t cursor)
{
    Rect rows[10];
    count = count < 10 ? count : 10;
    for (int n = 0; n < count; n++)
    {
        uint32_t at = table + stride * static_cast<uint32_t>(n) + frame;
        rows[n] = Box(Short(at), Short(at + 2), Short(at + 4), Short(at + 6));
    }
    Pointer::Tile(rows, count);
    for (int n = 0; n < count; n++)
    {
        screen.Item(rows[n], cursor, 2, n);
    }
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
}

bool CharacterLogList(Pointer::Screen& screen)
{
    uint32_t state = State();
    uint32_t sub = SubState();
    if (state == 0 || sub == 0 || Byte(state) != Submenu || Word(state + ChosenRow) != CharacterLog ||
        Byte(sub + 1) != CharLogChoosing)
    {
        return false;
    }
    screen.name = "character log";
    screen.moveSound = MoveSound();
    FramedRows(screen, L().charLogFrames, 12, 4, 2, sub + SubCursor);
    return true;
}

// A 12-byte entry of the overlay's frame tables (as its frame drawing, File 2 0x55F750, reads them)
Rect Frame(uint32_t table, int n)
{
    uint32_t at = table + 12 * static_cast<uint32_t>(n) + 4;
    return Box(Short(at), Short(at + 2), Short(at + 4), Short(at + 6));
}

// The character log's pages: File 2's Basic, Combat & Co-op and Scenario, File 1's Basic, Combat & Team, Scenario, Special and
// SP Item (the step 2 or 3 of its state, its 16-bit page at 6; File 2 0x562A20). A tab is reached with R1 (the next page) or L1 (the one before), as the page's text is made as the page
// changes; the scroll bar's arrows and the wheel scroll it, the hints press their keys
constexpr uint8_t CharLogData = 2, CharLogNetData = 3;
constexpr uint32_t CharLogPage = 6;

bool CharacterLogPages(Pointer::Screen& screen)
{
    uint32_t state = State();
    uint32_t sub = SubState();
    if (L().charLogTabs == 0 || state == 0 || sub == 0 || Byte(state) != Submenu || Word(state + ChosenRow) != CharacterLog ||
        (Byte(sub + 1) != CharLogData && Byte(sub + 1) != CharLogNetData) || Half(sub + CharLogPage) >= L().charLogPages)
    {
        return false;
    }
    screen.name = "character log's pages";
    screen.moveSound = MoveSound();
    int page = Half(sub + CharLogPage);
    const int pages = L().charLogPages;
    for (int n = 0; n < pages; n++)
    {
        int ahead = (n - page + pages) % pages;
        if (ahead != 0)
        {
            bool on = ahead <= pages / 2;
            screen.PressTimes(Frame(L().charLogTabs, n), on ? Pointer::R1 : Pointer::L1, on ? ahead : pages - ahead);
        }
    }
    screen.Press(Frame(L().charLogScroll, 0), Pointer::Up);
    screen.Press(Frame(L().charLogScroll, 2), Pointer::Down);
    // the hints: "L1 R1: Next page" from its glyph to the next hint, "cross: Back" from its glyph on
    Rect next = Frame(L().charLogHintGlyphs, 0), back = Frame(L().charLogHintGlyphs, 1);
    screen.Press({next.x0, next.y0 - 4, back.x0 - 8, next.y1 + 4}, Pointer::R1);
    screen.Press({back.x0, back.y0 - 4, back.x0 + 120, back.y1 + 4}, Pointer::Cross);
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

// The options' screens: the options' state's byte 1 is 5 while one is up, its 16-bit word at 4 the row that opened it (1
// sound, 2 brightness, 3 the screen's position, 4 save/load, 5 the network configuration's editor; File 2 0x55C7D0), its
// byte 2 the screen's step (each takes the pad at its own: sound 3 at 0x55DC20, brightness 3 at 0x55CE10, the screen's
// position 2 at 0x55E310, save/load 2 at 0x55E7F0, the editor 2 at 0x55F4C0). Their hints are at the same places: cross
// (EXIT) and SELECT (DEFAULT SETTINGS)
constexpr uint8_t OptionsScreen = 5;
enum OptionsRow : uint16_t
{
    SoundRow = 1,
    BrightnessRow = 2,
    ScreenRow = 3,
    SaveLoadRow = 4,
    NetworkRow = 5,
};
constexpr uint32_t ScreenStep = 2, ScreenCursor = 6;

uint32_t OptionsScreenUp(OptionsRow row, uint8_t step)
{
    uint32_t state = State();
    uint32_t sub = SubState();
    if (state == 0 || sub == 0 || Byte(state) != Submenu || Word(state + ChosenRow) != Options || Byte(sub + 1) != OptionsScreen ||
        Half(sub + SubCursor) != row || Byte(sub + ScreenStep) != step)
    {
        return 0;
    }
    return sub;
}

// The hints along the bottom: the cross's (EXIT) and, where there is one, SELECT's (DEFAULT SETTINGS), their glyphs at the
// frames' (16, 16 and 40 x 16 at y 418) and the words after them
void OptionsHints(Pointer::Screen& screen, bool defaults)
{
    screen.Press(Box(108, 412, 120, 28), Pointer::Cross);
    if (defaults)
    {
        screen.Press(Box(268, 412, 260, 28), Pointer::Select);
    }
}

bool SoundSettings(Pointer::Screen& screen)
{
    uint32_t sub = L().soundFrames != 0 ? OptionsScreenUp(SoundRow, 3) : 0;
    if (sub == 0)
    {
        return false;
    }
    screen.name = "sound settings";
    screen.moveSound = MoveSound();
    uint32_t table = L().soundFrames;
    // Row 0, the output (stereo or mono): its label and value with the arrows either side
    Rect label = Frame(table, 8);
    screen.Setting({label.x0, label.y0 - 8, Frame(table, 12).x1 + 16, label.y1 + 8}, sub + ScreenCursor, 2, 0,
                   Frame(table, 11), Frame(table, 12));
    // Rows 1 and 2, the volumes (0-7, after the output's byte): seven bar segments 16 apart, the first `level`
    // lit; a click on a segment steps the level toward it (left of the last lit one lowers it, right of it raises it)
    for (int row = 1; row <= 2; row++)
    {
        Rect name = Frame(table, 8 + row);
        Rect first = Frame(table, 13);
        float y0 = name.y0 - 8, y1 = name.y1 + 8, right = first.x0 + 16 * 7 + 40;
        float lit = first.x0 - 4 + 16.0f * static_cast<float>(Byte(sub + L().settings + static_cast<uint32_t>(row)));
        screen.Setting({name.x0, y0, right, y1}, sub + ScreenCursor, 2, row, {first.x0 - 40, y0, lit - 16, y1}, {lit, y0, right, y1});
    }
    OptionsHints(screen, true);
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

// Brightness: one value (the settings' byte 5, 0x80 the middle), its slider's marker moved from the middle by the
// value's distance times a step (0x55D0C0): a click left of the marker lowers it, right of it raises it, as the wheel does
constexpr uint32_t BrightnessValue = 5;
bool Brightness(Pointer::Screen& screen)
{
    uint32_t sub = L().brightnessFrames != 0 ? OptionsScreenUp(BrightnessRow, 3) : 0;
    if (sub == 0)
    {
        return false;
    }
    screen.name = "brightness";
    screen.moveSound = MoveSound();
    uint32_t table = L().brightnessFrames;
    Rect bar = Frame(table, 14), end = Frame(table, 16), marker = Frame(table, 17);
    float step;
    uint32_t bits = Word(L().brightnessStep);
    static_assert(sizeof(step) == sizeof(bits));
    std::memcpy(&step, &bits, sizeof(step));
    int value = Byte(sub + L().settings + BrightnessValue) - 0x80;
    float x = (marker.x0 + marker.x1) / 2 + (value < 0 ? -step * static_cast<float>((-value) / 2) : step * static_cast<float>(value));
    Rect slider = {bar.x0 - 16, marker.y0 - 8, end.x1 + 16, end.y1 + 8};
    screen.Press({slider.x0, slider.y0, x, slider.y1}, Pointer::Left);
    screen.Press({x, slider.y0, slider.x1, slider.y1}, Pointer::Right);
    OptionsHints(screen, true);
    screen.wheelUp = Pointer::Right;
    screen.wheelDown = Pointer::Left;
    return true;
}

// The screen's position: four arrows (each its direction), the wheel up and down
bool ScreenPosition(Pointer::Screen& screen)
{
    uint32_t sub = L().screenFrames != 0 ? OptionsScreenUp(ScreenRow, 2) : 0;
    if (sub == 0)
    {
        return false;
    }
    screen.name = "screen position";
    screen.moveSound = MoveSound();
    const uint32_t ways[4] = {Pointer::Up, Pointer::Left, Pointer::Right, Pointer::Down};
    for (int n = 0; n < 4; n++)
    {
        Rect arrow = Frame(L().screenFrames, 16 + n);
        screen.Press({arrow.x0 - 6, arrow.y0 - 6, arrow.x1 + 6, arrow.y1 + 6}, ways[n]);
    }
    OptionsHints(screen, true);
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

// Save/load: SAVE DATA and LOAD DATA (the screen's cursor, 0 and 1), then the memory card's own screens
bool SaveLoad(Pointer::Screen& screen)
{
    uint32_t sub = L().saveLoadFrames != 0 ? OptionsScreenUp(SaveLoadRow, 2) : 0;
    if (sub == 0)
    {
        return false;
    }
    screen.name = "save/load";
    screen.moveSound = MoveSound();
    Rect rows[2] = {Frame(L().saveLoadFrames, 6), Frame(L().saveLoadFrames, 7)};
    Pointer::Tile(rows, 2);
    for (int n = 0; n < 2; n++)
    {
        screen.Item(rows[n], sub + ScreenCursor, 2, n);
    }
    screen.Press(Box(268, 412, 120, 28), Pointer::Cross);
    screen.wheelUp = Pointer::Up;
    screen.wheelDown = Pointer::Down;
    return true;
}

// The network configuration's editor's question: Yes or No on one line (the options' state's byte 3, 1 Yes; left and right
// change it, 0x55F4C0), drawn at y 182 (0x55F250: the table's y 142 and two lines below); the words' places measured in a
// picture of the English text (the line's x comes from its text's drawn width)
bool NetworkConfiguration(Pointer::Screen& screen)
{
    uint32_t sub = L().networkQuestion ? OptionsScreenUp(NetworkRow, 2) : 0;
    if (sub == 0)
    {
        return false;
    }
    screen.name = "network configuration's question";
    screen.moveSound = MoveSound();
    screen.Item(Box(300, 178, 42, 28), sub + 3, 1, 1);
    screen.Item(Box(342, 178, 34, 28), sub + 3, 1, 0);
    return true;
}

bool OptionsList(Pointer::Screen& screen)
{
    uint32_t state = State();
    uint32_t sub = SubState();
    if (state == 0 || sub == 0 || Byte(state) != Submenu || Word(state + ChosenRow) != Options ||
        Byte(sub + 1) != OptionsChoosing)
    {
        return false;
    }
    screen.name = "options";
    screen.moveSound = MoveSound();
    uint32_t mode = Word(sub + L().optionsMode) != 0 ? 1 : 0;
    FramedRows(screen, L().optionsFrames[mode], 8, 0, static_cast<int>(Word(sub + L().optionsCount)), sub + SubCursor);
    return true;
}
}

bool TitleRunning() { return State() != 0; }

bool AtPressStart()
{
    uint32_t state = State();
    return state != 0 && Byte(state) == PressStart;
}

const Pointer::Finder TitleFinders[] = {MainMenu,     TwoRowSubmenu, CharacterLogList, CharacterLogPages,
                                        OptionsList,  SoundSettings, Brightness,       ScreenPosition,
                                        SaveLoad,     NetworkConfiguration};
const int TitleFinderCount = sizeof(TitleFinders) / sizeof(TitleFinders[0]);
}
