// ACCESS's AVE-TCP 3.1 (avetcp.irx, "AVE_TCP_31_Resident_Lib", with its device glue devglue.irx, "AVE_TCP_DEVGLUE_LIB",
// and its DHCP client avedhcp.irx), the TCP/IP stack of the Japanese PS2 games of KDDI's Multi-Matching BB (Biohazard
// Outbreak, Monster Hunter...), stood in natively on the Mac's own network (runtime/src/iop/avetcp.cpp). A game's own IOP
// module (its "wrapper") calls the stack; its stand-in calls these, which do what the modules' functions do.
//
// AVE-TCP's API is one entry, AT_apiCall (the module's export 4): a function code (0x41xx) and two parameter blocks, in and
// out, in the IOP's memory, laid out as the module reads them; its result is a 16-bit status (0 or a handle; negative an
// error). The device glue's are plain calls (its export table "devglue", 4 to 8).
#pragma once

#include "ps2/recomp.h"

#include <functional>

namespace Ps2::AveTcp
{
// AT_init (export 36) and AT_disp (export 32): the stack started (its block at the IOP address `parameters`: the
// module's thread's priority and the like, nothing the Mac's stack needs) and stopped, its connections closed. 0
s32 Init(u32 parameters);
s32 Dispose();

// AT_apiCall: the function `code` with its blocks at the IOP addresses `in` and `out` (0: none)
s16 ApiCall(u16 code, u32 in, u32 out);

// A host function the stack calls where AVE-TCP calls a function of the caller's (an ASR, an asynchronous service routine:
// a UDP endpoint's receiver): its token goes in the block where the function's address would. The receiver is called with
// the endpoint's handle, the datagram, and its source (address in network order, port), on the thread that polls the stack
// (ApiCall's caller)
using UdpReceiver = std::function<void(s16 handle, const u8* data, u32 size, u32 address, u16 port)>;
u32 RegisterUdpReceiver(UdpReceiver receiver);

// The device glue. Its device records (0x30C bytes: the ID, the bus (0 USB, 1 the expansion bay, 2 other), 1 for an
// Ethernet device (0 a modem), then the vendor's, the product's and the driver module's names, 256 bytes each)
constexpr u32 DeviceRecordBytes = 0x30C;
// devglue 4: the records of the devices there into `out` (room bytes): their count
s32 DeviceList(u8* out, u32 room);
// devglue 5: the device that's used (its ID): 0, or -1 for no such device
s32 DeviceSelect(u32 id);
// devglue 6: the count of the devices there into count; 1 when they changed since their list was last taken, else 0
s32 DeviceCount(u32& count);
// devglue 7 and 8: a parameter of the glue's set from or got into a block of `size` bytes: 0 (set) or the bytes got, -1 for an
// unknown one
s32 DeviceSetParameter(u32 key, const u8* value, u32 size);
s32 DeviceGetParameter(u32 key, u8* value, u32 size);
}
