// The executable's save data screens (formatting, saving, loading: the title's options and continue, the collection's data
// convert, the game's saves; File 2 0x1C2E20 runs them, its state's address in the word at 0x24AE14; File 1 0x1C0C20, 0x23BBE0).
// Their questions end with YES/NO on the question's last line: the choice is the state's word at 0x38 (0 YES, 1 NO; left
// and right toggle it), shown while its word at 0x30 is set (File 2 0x1C64F0 draws it; the steps at 0x1C5B90...). The two
// words are the game's own strings (a table of the save screens' texts, its words at 0x10 and 0x14), found where they were
// drawn (src/text_map.h)
#include "menus.h"
#include "text_map.h"

namespace Menus
{
namespace
{
using Pointer::Word;

struct Layout
{
    uint32_t state;        // the word holding the save screens' state's address
    // The save screens' texts: File 2 a table of tables by language (its byte at `language` picks one, the second word
    // there the texts' table; 0x1C1980 with 1), File 1 the second word of its table (0x1C01E0 with 1)
    uint32_t texts;
    uint32_t language;     // 0: no language table
    Pointer::Sound toggle; // the choice's sound (File 2 0x1C7470, File 1 0x1C3BC0)
};

const Layout File2 = {0x24ae14, 0x21ec60, 0x3137dd, {0x1c7470}};
const Layout File1 = {0x23bbe0, 0x217e40, 0, {0x1c3bc0}};

const Layout& L() { return Current() == Disc::File2 ? File2 : File1; }

constexpr uint32_t Shown = 0x30, Choice = 0x38, YesText = 0x10, NoText = 0x14;
constexpr int Yes = 0, No = 1;

bool SaveQuestion(Pointer::Screen& screen)
{
    uint32_t state = Word(L().state) & 0x1FFFFFF;
    if (state == 0 || state >= 0x1FFF000 || Word(state + Shown) == 0)
    {
        return false;
    }
    uint32_t table = L().language != 0 ? Word(L().texts + 4 * Pointer::Byte(L().language)) : L().texts;
    uint32_t texts = Word((table & 0x1FFFFFF) + 4) & 0x1FFFFFF;
    Pointer::Rect yes, no;
    if (!TextMap::Find(Word(texts + YesText), yes) || !TextMap::Find(Word(texts + NoText), no))
    {
        return false; // not drawn this frame: not up (or between its own frames)
    }
    screen.name = "save data's question";
    screen.moveSound = L().toggle;
    // Each word to the middle of the slash between them, a little above and below the line
    float middle = (yes.x1 + no.x0) / 2;
    screen.Item({yes.x0 - 8, yes.y0 - 6, middle, yes.y1 + 6}, state + Choice, 4, Yes);
    screen.Item({middle, no.y0 - 6, no.x1 + 8, no.y1 + 6}, state + Choice, 4, No);
    screen.wheelUp = Pointer::Left;
    screen.wheelDown = Pointer::Right;
    return true;
}
}

const Pointer::Finder SaveFinders[] = {SaveQuestion};
const int SaveFinderCount = sizeof(SaveFinders) / sizeof(SaveFinders[0]);
}
