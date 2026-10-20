// The game's software keyboard (src/typing.cpp): while it's up the Mac's keyboard types into it
#pragma once

namespace Typing
{
// The software keyboard ran a step now (the overrides of netwk's step, file1.cpp and file2.cpp)
void SoftKeyboardStep();
// Ps2GameInfo::typing: whether it's up
int Typing();
}
