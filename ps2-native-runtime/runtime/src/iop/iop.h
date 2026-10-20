// The IOP's high level stand-in: the SIF (sif.cpp) and the RPC servers of the modules (servers.cpp)
#pragma once

#include "../runtime.h"
#include "ps2/iop.h"

namespace Ps2
{
// Where things live in the IOP's 2 MB
constexpr u32 IopCmdBuffer = 0x00001000;
constexpr u32 IopServerBase = 0x00002000;
// The rest is the heap (the iop heap server's allocations and the servers' receive buffers): no module of the console's runs
// here, so a game finds at least the room it would on the console (CTTR asks for 264 KB after binding 14 servers)
constexpr u32 IopHeapBase = 0x00010000;

// The heap as sysmem has it: AllocSysMemory's modes (0 the lowest block that fits, 1 the highest, 2 at an address; 0 when
// there's no room), QueryTotalFreeMemSize or QueryMaxFreeMemSize, QueryBlockTopAddress or QueryBlockSize (bit 31 set for a
// free block, -1 outside the heap)
u32 IopAllocMode(u32 mode, u32 size, u32 address);
u32 IopFreeTotal(bool largest);
u32 IopBlockOf(u32 address, bool size);
void IopReset();
// The IOP replacement image the reset loads (its name gives the modules' version)
void IopSetImage(const std::string& path);
void IopServersInit();
void IopServe(RpcCall& call);
// The memory card server (mcserv.cpp)
void McServ(RpcCall& call);
// The pad server (padman.cpp)
void PadMan(RpcCall& call);
// The Dbc_Manager's RPC servers (0x80001300, 0x8000131B, 0x8000131C) and its drivers' modules loading (dbcman.cpp)
void DbcServe(RpcCall& call);
void DbcModuleLoaded(const std::string& name, const u8* elf, size_t size);
// A port's DualShock 2 as it answers a read in its pressure mode (0x79), after the answer's first three bytes: the buttons
// (active low), the sticks (right x, right y, left x, left y), the pressures of right, left, up, down, triangle, circle, cross,
// square, L1, R1, L2, R2. From the runtime's input (PadSetInput)
void PadDualShock2Report(u32 port, u8 report[18]);
const char* IopServerName(u32 sid);
// The console's clock as cdvdman's sceCdReadClock gives it: status, seconds, minutes, hours, a pad, day, month, year (BCD),
// in Japan's time (GMT+9) as a console keeps it; games make it local with the time zone GetOsdConfigParam gives (the Mac's)
void CdClock(u8 clock[8]);
// AVE-TCP's device glue sees the network adaptor's driver load (avetcp.cpp)
namespace AveTcp
{
void ModuleLoaded(const std::string& name);
}
// Sony's DNAS IOP helper (Sys1_driver04), started as the game loads it (dnas.cpp)
namespace Dnas
{
void Register();
}
// Sony's network configuration's RPC server (netcnfif, 0x80001101): the Mac's network as the one configuration (netcnf.cpp)
namespace Netcnf
{
void Register();
}
void SifReplyTaken();
}
