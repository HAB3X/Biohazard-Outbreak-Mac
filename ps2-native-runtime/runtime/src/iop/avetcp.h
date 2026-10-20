// AVE-TCP's stand-in's parts (ps2/avetcp.h): the API's dispatch, the device glue and the interface (avetcp.cpp), the
// TCP and UDP endpoints on the Mac's sockets (avetcp_socket.cpp), the DNS resolver (avetcp_dns.cpp)
#pragma once

#include "iop.h"
#include "ps2/avetcp.h"

#include <cstring>
#include <mutex>

namespace Ps2::AveTcp
{
// One lock over the stack: the API's calls come from the game's RPC servers, the resolver's answers from its threads
std::recursive_mutex& StackLock();
using Lock = std::lock_guard<std::recursive_mutex>;

// AVE-TCP's errors, as its functions return them
constexpr s16 ErrorNoHandle = -2;      // no TCP handle free (0xFA8C)
constexpr s16 ErrorInUse = -3;         // the local port taken (0xFA8C)
constexpr s16 ErrorHandle = -4;        // no such handle (0x101F4 and others)
constexpr s16 ErrorClosing = -6;       // the connection went down, any other reason (0x101F4)
constexpr s16 ErrorTimeout = -7;       // the connection timed out (0x101F4: reason 6)
constexpr s16 ErrorNoData = -8;        // a receive that took nothing: the connection closed while it waited (0x1090C)
constexpr s16 ErrorReceiveDone = -10;  // a receive after the peer's FIN with nothing left to take (0xDCD8)
constexpr s16 ErrorReset = -11;        // the peer reset or refused the connection (0x101F4: reason 4)
constexpr s16 ErrorParameter = -13;    // a bad option or size (0x11210)
constexpr s16 ErrorFailed = -16;       // 0xFFF0: an interface or route call refused (0xF8F0, 0x112EC)
constexpr s16 ErrorUnsupported = -32;  // 0xFFE0: no such function (AT_apiCall)

// The IOP's memory as the API's blocks are read and written
inline u16 R16(u32 address)
{
    u16 v;
    std::memcpy(&v, IopMemory(address), 2);
    return v;
}
inline u32 R32(u32 address)
{
    u32 v;
    std::memcpy(&v, IopMemory(address), 4);
    return v;
}
inline void W16(u32 address, u16 v) { std::memcpy(IopMemory(address), &v, 2); }
inline void W32(u32 address, u32 v) { std::memcpy(IopMemory(address), &v, 4); }

// avetcp_socket.cpp: a TCP handle's options (0x4108, 0x4109), the TCP functions (0x4110-0x411C) and UDP's (0x4120-0x4125);
// every endpoint closed
s16 SocketCall(u16 code, u32 in, u32 out);
void SocketsClosed();
// The UDP receivers' calls for datagrams that came (on the polling thread: any API call polls)
void SocketsPoll();

// avetcp_dns.cpp: the resolver's functions (0x4181-0x4187); its tickets dropped
s16 DnsCall(u16 code, u32 in, u32 out);
void DnsReset();
}
