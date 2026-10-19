// Online play on the Mac (src/network.cpp): the revived servers' DNS, and the network configuration as the Mac's
#pragma once

namespace Network
{
// The DNS server obsrv.org publishes for the Japanese discs (its DNS answers the names the game looks up with its own servers)
constexpr const char* ObsrvDns = "208.72.237.31";
// Capcom's IOP network modules stood in (src/usbsrch.cpp...): their RPC servers
void RegisterLoadHigh();
void RegisterUsbSearcher();
void RegisterAveWrapper();
}
