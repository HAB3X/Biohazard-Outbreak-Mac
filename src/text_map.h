// Where the game drew its text in its last finished frame: each string its two text functions drew (the executable's, File 2
// 0x1B5090 and 0x1B7130, File 1 0x1B43C0 and 0x1B5FB0: the second Network Play's), with the place it was drawn at and the place
// the drawing left the text position, read from the font's own state as the functions use it. The menus whose items are
// text are pointed at by the strings they draw (src/menus.h): the place is the game's own, whatever the language
#pragma once

#include "pointer.h"
#include "ps2/recomp.h"

#include <cstdint>

namespace TextMap
{
// One game's font state and frame counter (its addresses)
struct Layout
{
    uint32_t font;         // the font's state: its line height at 0x6C, the text position (x, y) at 0x70 and 0x74
    uint32_t frameCount;   // a word the game's main loop counts its frames in (File 2 0x1BA1D0's)
};
extern const Layout File1;
extern const Layout File2;
void Use(const Layout& layout);

// A text function's override: the string drawn (the text function's own, `draw`, run) and its place kept
void Draw(Ps2Cpu* c, void (*draw)(Ps2Cpu*));

// The place of a string drawn in the last finished frame, false when it wasn't drawn: the game's string at `text`, drawn from
// there or from a copy of it (the menus that print their texts into a buffer first: the same bytes, up to the copy kept)
bool Find(uint32_t text, Pointer::Rect& place);

// The strings drawn in the last finished frame, in the order drawn: where from, the place, and the string's first bytes as
// they were when drawn (a buffer the game draws from is written again later)
constexpr int KeptBytes = 40;
struct Drawn
{
    uint32_t text;
    Pointer::Rect place;
    char bytes[KeptBytes];
};
int Last(const Drawn*& drawn);
}
