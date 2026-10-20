// The game's software keyboard on the Mac. Network Play takes text (a login's ID and password in the browser's forms, a
// handle name, chat) through netwk's software keyboard: a step of it runs once a frame while it's up (File 2's 0x552F10,
// File 1's 0x553820; ten callers, the browser's forms and the lobby's among them). The game reads a USB keyboard there too
// (libusbkb, through netwk's 0x59FD60), which the runtime makes of the Mac's keyboard while the game takes typed text: so
// while the software keyboard is up the keys type into it (Return confirms, Backspace deletes; the arrows still move over
// its keys), and press the pad's buttons otherwise.
#include "typing.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace Typing
{
namespace
{
using Clock = std::chrono::steady_clock;
// The software keyboard's last step: up while it ran within this long (its steps come once a game frame; a second covers
// frames slowed by a busy Mac, and a key pressed in the second after it closes types nothing)
constexpr auto Up = std::chrono::seconds(1);
std::atomic<Clock::rep> g_lastStep{0};
}

void SoftKeyboardStep() { g_lastStep = Clock::now().time_since_epoch().count(); }

int Typing()
{
    const Clock::time_point last{Clock::duration{g_lastStep.load()}};
    return g_lastStep.load() != 0 && Clock::now() - last < Up ? 1 : 0;
}

namespace
{
// The work area's bytes the mode uses (netwk's 0x553DC0, the mode keys' change): the category (0 hiragana, 1 katakana, 2
// Latin letters and digits, 3 symbols), its layout, and two flags the change clears or acts on
constexpr u32 Category = 0x23, Layout = 0x22, Disabled = 0x24, Flag39 = 0x39, SubWindow = 0x3A;
constexpr u8 Latin = 2;
// The Latin category's layouts, half-width first (15, the browser's fields' own; 7 its other case, as the Caps Lock key's
// change makes it, 0x5A0DA0: a layout XOR 8): the category's choice (0x555080) prefers full-width 10, which made "ｍａｒｋ"
constexpr u8 HalfWidth[] = {15, 7};

// The usages of the keys that type a character: letters, digits, and the symbols' keys (not ` , which the game takes as the
// Japanese keyboard's half/full-width key and changes the mode with)
bool TypesCharacter(u8 usage) { return (usage >= 0x04 && usage <= 0x27) || (usage >= 0x2D && usage <= 0x38 && usage != 0x35); }

void Call(Ps2Cpu* c, SoftKeyboard::Fn f, u32 a0 = 0, u32 a1 = 0, u32 a2 = 0)
{
    c->r[4].ud[0] = a0;
    c->r[5].ud[0] = a1;
    c->r[6].ud[0] = a2;
    f(c);
}
}

// The keys typed in kana mode were taken as romaji (the Mac's "m a r" became "ま r"): the Mac's keyboard types the characters
// on its keys, so the software keyboard goes to its Latin category first, as its own mode key does (0x553DC0's change, without
// its sound). Kana stay for the pad and the mouse: the category chosen on the software keyboard is used until a key is typed
void UsbKey(Ps2Cpu* c, const SoftKeyboard& keyboard, SoftKeyboard::Fn handle)
{
    const u32 work = rd32(c->r[28].uw[0] + static_cast<u32>(keyboard.work));
    const u8 category = rd8(work + Category);
    if (TypesCharacter(rd8(work + keyboard.key)) && category != Latin)
    {
        R128 saved[32];
        std::memcpy(saved, c->r, sizeof(saved));
        Call(c, keyboard.select, Latin, work + Category, work + Layout);
        if (c->r[2].sw[0] == 0)
        {
            const u32 disabled = rd32(work + Disabled);
            for (u8 layout : HalfWidth)
            {
                if ((disabled & (1u << layout)) == 0)
                {
                    wr8(work + Layout, layout);
                    break;
                }
            }
            wr8(work + Flag39, 0);
            if (rd8(work + Layout) == 4)
            {
                Call(c, keyboard.layout4);
            }
            for (SoftKeyboard::Fn f : keyboard.redraw)
            {
                Call(c, f);
            }
            if (rd8(work + SubWindow) != 0)
            {
                Call(c, keyboard.closeSub);
                Call(c, keyboard.resetInput, 0, 0);
            }
        }
        std::memcpy(c->r, saved, sizeof(saved));
    }
    handle(c);
}
}
