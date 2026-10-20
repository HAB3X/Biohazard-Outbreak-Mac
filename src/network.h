// Online play on the Mac (src/network.cpp): the revived servers' DNS, and the network configuration as the Mac's
#pragma once

#include "ps2/recomp.h"

namespace Network
{
// The DNS server obsrv.org publishes for the Japanese discs (its DNS answers the names the game looks up with its own servers)
constexpr const char* ObsrvDns = "208.72.237.31";
// Capcom's IOP network modules stood in (src/usbsrch.cpp...): their RPC servers
void RegisterLoadHigh();
void RegisterUsbSearcher();
void RegisterAveWrapper();

// Network Play's connection setup screen (netwk's step, File 2's 0x5BFD40, File 1's 0x5C88C0), where its two state words
// are (offsets from gp: the screen's work area's address, the step's state)
struct SetupScreen
{
    s32 work;
    s32 step;
};
constexpr SetupScreen File2Setup{-0x5C20, -0x5C14};
constexpr SetupScreen File1Setup{-0x5A4C, -0x5A40};
// The step run (`step`, its recompiled body), the configuration list skipped (src/network.cpp)
void SetupStep(Ps2Cpu* c, const SetupScreen& screen, void (*step)(Ps2Cpu*));
}
