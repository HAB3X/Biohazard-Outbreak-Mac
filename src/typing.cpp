// The game's software keyboard on the Mac. Network Play takes text (a login's ID and password in the browser's forms, a
// handle name, chat) through netwk's software keyboard: a step of it runs once a frame while it's up (File 2's 0x552F10,
// File 1's 0x553820; ten callers, the browser's forms and the lobby's among them). The game reads a USB keyboard there too
// (libusbkb, through netwk's 0x59FD60), which the runtime makes of the Mac's keyboard while the game takes typed text: so
// while the software keyboard is up the keys type into it (Return confirms, Backspace deletes; the arrows still move over
// its keys), and press the pad's buttons otherwise.
#include "typing.h"

#include <atomic>
#include <chrono>

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
}
