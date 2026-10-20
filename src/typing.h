// The game's software keyboard (src/typing.cpp): while it's up the Mac's keyboard types into it
#pragma once

#include "ps2/recomp.h"

namespace Typing
{
// The software keyboard ran a step now (the overrides of netwk's step, file1.cpp and file2.cpp)
void SoftKeyboardStep();
// Ps2GameInfo::typing: whether it's up
int Typing();

// The software keyboard's work area and the functions its mode change calls (netwk's, as each game has them)
struct SoftKeyboard
{
    s32 work;      // the work area's address, from gp
    u32 key;       // the work area's byte with the USB keyboard's key just pressed (its usage)
    using Fn = void (*)(Ps2Cpu*);
    Fn select;     // a category's layout chosen (File 2's 0x555080: the category, where it and the layout go; 0 done)
    Fn layout4;    // what layout 4 needs set up (0x555660)
    Fn redraw[4];  // the keys, the cursor, the line and the help made again (0x555350, 0x555480, 0x554EE0, 0x5A25D0)
    Fn closeSub;   // a sub-window closed (0x5B6C80) and the input reset (0x552E80, with 0, 0)
    Fn resetInput;
};
// A key of the USB keyboard handled (the override of File 2's 0x59FEF0, File 1's 0x5A51C0; `handle` its body): a key that
// types a character first puts the software keyboard in its Latin letters
void UsbKey(Ps2Cpu* c, const SoftKeyboard& keyboard, SoftKeyboard::Fn handle);
}
