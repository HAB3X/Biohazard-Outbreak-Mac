// Capcom's AVE-TCP wrapper (MODULES/NETMOD_E/AVEWRAPE.IRX for Ethernet, NETMOD/AVEWRAPM.IRX for modems,
// "Capcom_AVE_TCP_Wrapper"), stood in natively: the RPC server the EE's network library calls (0x01270030), each function a
// call of ACCESS's AVE-TCP API (the runtime's ps2/avetcp.h stands in for the stack itself on the Mac's sockets).
//
// The RPC block (the EE's at 0x316680, sent and received whole): word 6 (0x18) the result, the arguments from 0x1C. The
// functions (the module's dispatch, 0x2098, AVEWRAPE.IRX's offsets) and the AVE-TCP calls each makes are listed below by
// what's been seen of the game using them.
#include "network.h"

#include "ps2/iop.h"

#include <algorithm>

namespace Network
{
namespace
{
using namespace Ps2;

constexpr u32 Server = 0x01270030;

void Serve(RpcCall& c)
{
    c.out.assign(c.in, c.in + std::min(c.inSize, c.recvSize));
    c.out.resize(std::max<size_t>(c.out.size(), c.recvSize));
    Log("AVE wrapper: function %u, %u bytes: %08x %08x %08x %08x", c.fno, c.inSize, c.inSize > 0x1C ? c.In32(0x1C) : 0,
        c.inSize > 0x20 ? c.In32(0x20) : 0, c.inSize > 0x24 ? c.In32(0x24) : 0, c.inSize > 0x28 ? c.In32(0x28) : 0);
    Fatal("AVE wrapper: function %u isn't done", c.fno);
}
}

void RegisterAveWrapper() { IopRegisterServer(Server, "Capcom AVE-TCP wrapper", Serve); }
}
