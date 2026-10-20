// Capcom's AVE-TCP wrapper (MODULES/NETMOD_E/AVEWRAPE.IRX for Ethernet, NETMOD/AVEWRAPM.IRX for modems,
// "Capcom_AVE_TCP_Wrapper"), stood in natively: the RPC server the EE's network library calls (0x01270030), each function
// a call of ACCESS's AVE-TCP or its device glue, which the runtime stands in for on the Mac's network (ps2/avetcp.h).
//
// The RPC block (the EE's at 0x316680, sent and received whole): word 0 an argument of some functions, word 6 (0x18) the
// result, the arguments from 0x1C. As in the module, the functions work in the block where it landed in the IOP's memory
// (data sent and received, names and addresses are read and written there) and build AVE-TCP's parameter blocks beside
// it. Each function is the module's (its dispatch is at 0x2098; offsets are AVEWRAPE.IRX's). The EE's network library
// (File 2's 0x1C75B0, File 1's 0x1C3CD0, and their callers) calls functions 1-7, 11, 13, 15, 16, 19, 22, 30, 31, 33-49,
// 54-56 and 58-63, 70-72 and 74, in both games; of those, the PPP (modem) ones (33-40) aren't here. The module's
// passive opens (8-10) and UDP (81-84) are never called. A call of a function not here stops the game, naming it.
#include "network.h"

#include "ps2/avetcp.h"
#include "ps2/iop.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Network
{
namespace
{
using namespace Ps2;

constexpr u32 Server = 0x01270030;

// The connection's kind (0x2C64): set by function 5 and by function 72 from the selected device's record (an Ethernet
// device on the expansion bay 2, on USB 5; a modem 1 or 4). A send or receive in the modem's kinds checks the line first
u32 g_mode = 0;
// The driver's entry attached by function 41 (0x30CC)
u32 g_driverEntry = 0;
// The DHCP lease's addresses as the non-waiting start (function 54) has them written (0x3DE4-0x3DEC)
u32 g_dhcpOut = 0;
// The resolver's ticket taken by function 61 (0x31B8: one slot, -1 when free) and its work area (0x31C0, 0xC24 bytes)
s32 g_ticket = -1;
u32 g_ticketWork = 0;

// The parameter blocks the module builds on its stack: a block of the IOP's memory of the stand-in's, and its work areas
constexpr u32 ParameterBytes = 0x100;
u32 Parameters()
{
    static const u32 at = IopAlloc(ParameterBytes + 16 + 0xC24);
    g_dhcpOut = at + ParameterBytes;
    g_ticketWork = at + ParameterBytes + 16;
    return at;
}

u16 R16(u32 a)
{
    u16 v;
    std::memcpy(&v, IopMemory(a), 2);
    return v;
}
u32 R32(u32 a)
{
    u32 v;
    std::memcpy(&v, IopMemory(a), 4);
    return v;
}
void W16(u32 a, u16 v) { std::memcpy(IopMemory(a), &v, 2); }
void W32(u32 a, u32 v) { std::memcpy(IopMemory(a), &v, 4); }

// A call: the RPC block at its address in the IOP's memory, and a parameter block being built
struct Call
{
    u32 block;
    u32 inSize;
    u32 p = Parameters();
    u32 In(u32 off) const { return R32(block + off); }
    u16 In16(u32 off) const { return R16(block + off); }
    void Result(s32 v) { W32(block + 0x18, static_cast<u32>(v)); }
    // A parameter block of 16-bit and 32-bit fields
    Call& P16(u32 off, u16 v)
    {
        W16(p + off, v);
        return *this;
    }
    Call& P32(u32 off, u32 v)
    {
        W32(p + off, v);
        return *this;
    }
    Call& Clear(u32 bytes)
    {
        std::memset(IopMemory(p), 0, bytes);
        return *this;
    }
    s16 Api(u16 code, u32 out = 0) { return AveTcp::ApiCall(code, p, out); }
};

// 5 (0xA8): the stack started for the connection's kind (word 1): the device glue's threads' priorities (9, 8), AT_init
// (its block: the priority 9, word 0), then option 1 on
void Start(Call& call)
{
    g_mode = call.In(4);
    const u32 nine = 9, eight = 8;
    AveTcp::DeviceSetParameter(1, reinterpret_cast<const u8*>(&nine), 4);
    AveTcp::DeviceSetParameter(2, reinterpret_cast<const u8*>(&eight), 4);
    call.Clear(0x40).P16(0, 0).P32(4, 9).P32(8, call.In(0));
    AveTcp::Init(call.p);
    const u32 value = call.p + 0x30;
    W32(value, 1);
    call.Clear(0x10).P16(0, 1).P16(2, 4).P32(4, value);
    call.Result(call.Api(0x412E));
}

// 1 (0x208): 0x4105, the interface's address (words 0x1C-0x24: address, mask, broadcast); 2 (0x264): 0x4106 clears it
void SetAddress(Call& call, bool set)
{
    call.Clear(0x10).P16(0, 0);
    if (set)
    {
        call.P32(4, call.In(0x1C)).P32(8, call.In(0x20)).P32(12, call.In(0x24));
    }
    call.Result(call.Api(set ? 0x4105 : 0x4106));
}

// 3 (0x2A4): 0x4108 sets the option at 0x1E of the handle at 0x1C to the value at 0x22 (its size the 16-bit 0x20); 4 (0x304):
// 0x4109 gets it into 0x22 (the result its size, 4)
void Option(Call& call, bool set)
{
    call.Clear(0x10).P16(0, call.In16(0x1C)).P16(2, call.In16(0x1E)).P16(4, call.In16(0x20)).P32(8, call.block + 0x22);
    call.Result(call.Api(set ? 0x4108 : 0x4109));
}

// 7 (0x364): 0x4110, an active open to word 0x1C's address, 16-bit 0x20's port, from 0x22's local port
void Open(Call& call)
{
    call.Clear(0x18).P32(0, call.In(0x1C)).P16(4, call.In16(0x20)).P16(6, call.In16(0x22));
    call.Result(call.Api(0x4110));
}

// 11-13 (0x564, 0x5A8, 0x5EC): 0x4113 close, 0x4114 shut down, 0x4115 abort the handle at 0x1C; 22 (0x97C) 0x411C deletes it.
// 21 (0x930): 0x411A, with the 16-bit 0x1E
void HandleCall(Call& call, u16 code)
{
    call.Clear(8).P16(0, call.In16(0x1C)).P16(2, code == 0x411A ? call.In16(0x1E) : 0);
    call.Result(call.Api(code));
}

// 14 (0x630): 0x4116, the handle's addresses: the remote address at 0x20, the ports at 0x24 and 0x26 (the module puts the
// block's 16-bit 6 at 0x24 and 4 at 0x26)
void Info(Call& call)
{
    call.Clear(8).P16(0, call.In16(0x1C));
    const u32 out = call.p + 0x20;
    const s16 r = call.Api(0x4116, out);
    call.Result(r);
    W32(call.block + 0x20, R32(out));
    W16(call.block + 0x26, R16(out + 4));
    W16(call.block + 0x24, R16(out + 6));
}

// The handle's status (0x4117): the result, and the state, listener's count, room to send and bytes waiting into out
s16 Status(Call& call, u16 handle, u16 out[4])
{
    call.Clear(8).P16(0, handle);
    const u32 o = call.p + 0x20;
    const s16 r = call.Api(0x4117, o);
    for (u32 n = 0; n < 4; n++)
    {
        out[n] = R16(o + n * 2);
    }
    return r;
}

// 15 (0x6A4): the status of the handle at 0x1C into 0x1E-0x25
void StatusCall(Call& call)
{
    u16 s[4];
    call.Result(Status(call, call.In16(0x1C), s));
    for (u32 n = 0; n < 4; n++)
    {
        W16(call.block + 0x1E + n * 2, s[n]);
    }
}

// A modem's line checked before a send or receive (0x10): in the modem's kinds, the line must be up (0x4156's state 4)
bool LineUp(Call& call)
{
    if (g_mode == 1 || g_mode == 4)
    {
        Fatal("AVE wrapper: a modem's connection (kind %u) isn't done", g_mode);
    }
    (void)call;
    return true;
}

// 16 (0x724): 0x4118, the 16-bit 0x1E bytes at 0x20 sent on the handle at 0x1C, if it's established (else -6)
void Send(Call& call)
{
    if (!LineUp(call))
    {
        return;
    }
    u16 s[4];
    s16 r = Status(call, call.In16(0x1C), s);
    if (r >= 0)
    {
        if (static_cast<s16>(s[0]) == 4)
        {
            call.Clear(0x18).P16(0, call.In16(0x1C)).P32(4, 0);
            *IopMemory(call.p + 8) = 0;
            *IopMemory(call.p + 9) = 1;
            call.P16(0xC, call.In16(0x1E)).P32(0x10, call.block + 0x20);
            r = call.Api(0x4118);
        }
        else
        {
            r = -6;
        }
    }
    call.Result(r);
}

// 19 (0x7DC): 0x4119, up to the 16-bit 0x1E bytes into 0x20 from the handle at 0x1C (waiting for them)
void Receive(Call& call)
{
    call.Clear(0x10).P16(0, call.In16(0x1C)).P16(2, call.In16(0x1E)).P32(4, call.block + 0x20).P32(8, 0);
    call.Result(call.Api(0x4119, call.p + 0x20));
}

// 20 (0x834): what's waiting on the handle at 0x1C (up to the 16-bit 0x1E bytes) into 0x20: 0 when nothing is; -6 when the
// connection is neither established nor closing with data left
void ReceiveWaiting(Call& call)
{
    if (!LineUp(call))
    {
        return;
    }
    u16 s[4];
    s16 r = Status(call, call.In16(0x1C), s);
    const s16 state = static_cast<s16>(s[0]);
    const u16 waiting = s[3];
    if (r >= 0)
    {
        if (state == 4 || ((state == 5 || state == -1) && waiting != 0))
        {
            if (waiting == 0)
            {
                call.Result(0);
                return;
            }
            const u16 length = std::min<u16>(waiting, call.In16(0x1E));
            call.Clear(0x10).P16(0, call.In16(0x1C)).P16(2, length).P32(4, call.block + 0x20).P32(8, 0);
            r = call.Api(0x4119, call.p + 0x20);
        }
        else
        {
            r = -6;
        }
    }
    call.Result(r);
}

// 30 and 31 (0xC38, 0xC8C): 0x4130 adds a route (words 0x1C and 0x20, the metric 1), 0x4131 deletes it
void Route(Call& call, bool add)
{
    call.Clear(0x10).P32(0, call.In(0x1C)).P32(4, call.In(0x20)).P32(8, add ? 1 : 0);
    call.Result(call.Api(add ? 0x4130 : 0x4131));
}

// 41 (0x122C): the device's driver started (NDI_init), its entry taken and attached to interface 0; 42 (0x12CC) the reverse
void Attach(Call& call)
{
    s16 r = call.Clear(8).Api(0x4161);
    if (r == 0)
    {
        const u32 out = call.p + 0x20;
        W32(out, 0);
        r = call.Api(0x4163, out);
        const u32 entry = R32(out);
        if (r == 0 && entry != 0)
        {
            call.Clear(8).P16(0, 0).P32(4, entry);
            if (call.Api(0x4141) == 0)
            {
                g_driverEntry = entry;
                call.Result(0);
                return;
            }
        }
    }
    call.Result(-1);
}

void Detach(Call& call)
{
    call.Clear(8).P16(0, 0).Api(0x4142);
    call.Result(call.Clear(8).Api(0x4162));
}

// 43-45 (0x131C, 0x1370, 0x13B8): NDI_start (word 0 a modem's dialling kind, devglue parameter 9; then the start, mode 2),
// whether the link is up (mode 3), NDI_finish
void DeviceStart(Call& call, u16 mode)
{
    if (mode == 2)
    {
        static const u32 dial[5] = {0xCF, 1, 2, 4, 8};
        if (call.In(0) <= 4)
        {
            AveTcp::DeviceSetParameter(9, reinterpret_cast<const u8*>(&dial[call.In(0)]), 4);
        }
    }
    call.Result(call.Clear(8).P16(0, 0).P16(2, mode).Api(0x4164));
}

// 46-58 (0x13F4-0x17D0): AVE-DHCP's functions
void Dhcp(Call& call, u32 fno)
{
    switch (fno)
    {
    case 46:
        call.Result(call.Clear(8).Api(0x4171));
        break;
    case 47:
        call.Result(call.Clear(8).Api(0x4172));
        break;
    case 48: // the domain name into 0x1C, the DNS servers into 0x11C and 0x120
        call.Result(call.Clear(0x10).P32(0, 0).P32(4, call.block + 0x1C).P32(8, call.block + 0x11C)
                        .P32(12, call.block + 0x120).Api(0x4173));
        break;
    case 49: // the router into 0x1C
        call.Result(call.Clear(8).P32(0, 0).P32(4, call.block + 0x1C).Api(0x4174));
        break;
    case 50: // the lease's times into 0x1C-0x24
        call.Result(call.Clear(0x10).P32(0, call.block + 0x1C).P32(4, call.block + 0x20).P32(8, call.block + 0x24)
                        .Api(0x4175));
        break;
    case 51:
        call.Result(call.Clear(8).Api(0x4176));
        break;
    case 52: // the host name at 0x1C
        call.Result(AveTcp::ApiCall(0x4177, call.block + 0x1C, 0));
        break;
    case 53: // a start that waits: word 0x1C, the addresses into 0x20-0x28, word 0x2C
    case 54: // a start: word 0x1C, the addresses into the module's 0x3DE4-0x3DEC, word 0x20
        call.Clear(0x20).P32(8, call.In(0x1C));
        if (fno == 53)
        {
            call.P32(0x10, call.block + 0x20).P32(0x14, call.block + 0x24).P32(0x18, call.block + 0x28)
                .P32(0x1C, call.In(0x2C));
        }
        else
        {
            call.P32(0x10, g_dhcpOut).P32(0x14, g_dhcpOut + 4).P32(0x18, g_dhcpOut + 8).P32(0x1C, call.In(0x20));
        }
        call.Result(call.Api(fno == 53 ? 0x4178 : 0x4179));
        break;
    case 55: // bound (states 3-5): the addresses the start had written, into 0x1C-0x24
    {
        const s16 state = call.Clear(8).Api(0x4176);
        if (state >= 3 && state <= 5)
        {
            call.Result(0);
            for (u32 n = 0; n < 3; n++)
            {
                W32(call.block + 0x1C + n * 4, R32(g_dhcpOut + n * 4));
            }
        }
        else
        {
            call.Result(-1);
        }
        break;
    }
    case 56:
        call.Result(call.Clear(8).Api(0x417A));
        break;
    case 57: // the lease released (waiting), 58 its release started: words 0x1C and 0x20
    case 58:
        call.Result(call.Clear(0x10).P16(0, 0).P32(4, call.In(0x1C)).P32(8, call.In(0x20)).Api(fno == 57 ? 0x417B : 0x417C));
        break;
    default:
        Fatal("AVE wrapper: DHCP function %u (AVE-TCP 0x417%X) isn't done", fno, fno - 46);
    }
}

// 59-63 (0x1820-0x1A24): the resolver
void Dns(Call& call, u32 fno)
{
    switch (fno)
    {
    case 59: // started: the domain name at 0x1C, the servers words 0x11C and 0x120
        g_ticket = -1;
        call.Result(call.Clear(0x10).P32(0, call.block + 0x1C).P32(4, call.In(0x11C)).P32(8, call.In(0x120)).Api(0x4181));
        break;
    case 60:
        call.Result(call.Clear(8).Api(0x4182));
        break;
    case 61: // a ticket for the name at 0x1C, in the one slot
        if (g_ticket >= 0)
        {
            call.Result(-1);
            break;
        }
        g_ticket = call.Clear(8).P32(0, call.block + 0x1C).P32(4, g_ticketWork).Api(0x4183);
        call.Result(g_ticket);
        break;
    case 62: // the ticket at 0x1C released
        if (static_cast<s32>(static_cast<s16>(call.In16(0x1C))) == g_ticket)
        {
            g_ticket = -1;
        }
        call.Result(call.Clear(8).P16(0, call.In16(0x1C)).Api(0x4184));
        break;
    case 63: // the ticket at 0x1C's answer: one address into 0x20
        call.Result(call.Clear(0x10).P16(0, call.In16(0x1C)).P32(4, call.block + 0x20).P16(8, 1).Api(0x4185));
        break;
    default:
        Fatal("AVE wrapper: resolver function %u isn't done", fno);
    }
}

// 70 (0x1BCC): devglue 6, the count of the devices at word 0
void DeviceCount(Call& call)
{
    u32 count = 0;
    call.Result(AveTcp::DeviceCount(count));
    W32(call.block, count);
}

// 71 (0x1C00): the device record at word 0's place in devglue 4's list: the result the list's count, its ID, bus and kind at
// 0x1C, its vendor's, product's and module's names at 0x28, 0x128, 0x228
void DeviceInfo(Call& call)
{
    const u32 index = call.In(0);
    if (index >= 4)
    {
        call.Result(-1);
        return;
    }
    std::vector<u8> table(4 * AveTcp::DeviceRecordBytes);
    const s32 count = AveTcp::DeviceList(table.data(), static_cast<u32>(table.size()));
    call.Result(count);
    const u8* record = table.data() + index * AveTcp::DeviceRecordBytes;
    std::memcpy(IopMemory(call.block + 0x1C), record, 12);
    std::memcpy(IopMemory(call.block + 0x28), record + 0x0C, 0x300);
}

// 72 (0x1EC0): devglue 5 selects device word 0, and the connection's kind follows its record
void DeviceSelect(Call& call)
{
    const u32 id = call.In(0);
    s32 result = id > 3 ? -1 : AveTcp::DeviceSelect(id);
    if (result == 0)
    {
        std::vector<u8> table(4 * AveTcp::DeviceRecordBytes);
        const s32 count = AveTcp::DeviceList(table.data(), static_cast<u32>(table.size()));
        s32 found = -1;
        for (s32 n = 0; n < count; n++)
        {
            u32 recordId;
            std::memcpy(&recordId, table.data() + n * AveTcp::DeviceRecordBytes, 4);
            if (recordId == id)
            {
                found = n;
            }
        }
        if (found < 0)
        {
            call.Result(-1);
            return;
        }
        u32 words[3];
        std::memcpy(words, table.data() + found * AveTcp::DeviceRecordBytes, 12);
        const u32 bus = words[1];
        const bool ethernet = words[2] == 1;
        if (bus <= 1)
        {
            g_mode = ethernet ? (bus == 1 ? 2 : 5) : (bus == 1 ? 1 : 4);
        }
    }
    call.Result(result);
}

// 73 and 74 (0x2030, 0x2064): devglue 7 and 8, the parameter word 0x1C, its size word 0x20, its bytes at 0x24
void DeviceParameter(Call& call, bool set)
{
    const u32 key = call.In(0x1C);
    const u32 size = call.In(0x20);
    if (size > call.inSize - 0x24)
    {
        Fatal("AVE wrapper: device parameter %u of %u bytes, past the block", key, size);
    }
    u8* value = IopMemory(call.block + 0x24);
    call.Result(set ? AveTcp::DeviceSetParameter(key, value, size) : AveTcp::DeviceGetParameter(key, value, size));
}

void Serve(RpcCall& c)
{
    Call call{static_cast<u32>(c.in - IopMemory(0)), c.inSize};
    if (IopLogging())
    {
        Log("AVE wrapper: function %u: %08x, %08x %08x %08x %08x", c.fno, call.In(0), call.In(0x1C), call.In(0x20),
            call.In(0x24), call.In(0x28));
    }
    switch (c.fno)
    {
    case 1:
    case 2:
        SetAddress(call, c.fno == 1);
        break;
    case 3:
    case 4:
        Option(call, c.fno == 3);
        break;
    case 5:
        Start(call);
        break;
    case 6:
        call.Result(AveTcp::Dispose());
        break;
    case 7:
        Open(call);
        break;
    case 11:
        HandleCall(call, 0x4113);
        break;
    case 12:
        HandleCall(call, 0x4114);
        break;
    case 13:
        HandleCall(call, 0x4115);
        break;
    case 14:
        Info(call);
        break;
    case 15:
        StatusCall(call);
        break;
    case 16:
        Send(call);
        break;
    case 19:
        Receive(call);
        break;
    case 20:
        ReceiveWaiting(call);
        break;
    case 21:
        HandleCall(call, 0x411A);
        break;
    case 22:
        HandleCall(call, 0x411C);
        break;
    case 30:
    case 31:
        Route(call, c.fno == 30);
        break;
    case 41:
        Attach(call);
        break;
    case 42:
        Detach(call);
        break;
    case 43:
        DeviceStart(call, 2);
        break;
    case 44:
        DeviceStart(call, 3);
        break;
    case 45:
        call.Result(call.Clear(8).Api(0x4165));
        break;
    case 70:
        DeviceCount(call);
        break;
    case 71:
        DeviceInfo(call);
        break;
    case 72:
        DeviceSelect(call);
        break;
    case 73:
    case 74:
        DeviceParameter(call, c.fno == 73);
        break;
    default:
        if (c.fno >= 46 && c.fno <= 58)
        {
            Dhcp(call, c.fno);
        }
        else if (c.fno >= 59 && c.fno <= 65)
        {
            Dns(call, c.fno);
        }
        else
        {
            Fatal("AVE wrapper: function %u isn't done (%08x, %08x %08x %08x %08x)", c.fno, call.In(0), call.In(0x1C),
                  call.In(0x20), call.In(0x24), call.In(0x28));
        }
    }
    // The answer is the block as the functions left it
    c.out.assign(IopMemory(call.block), IopMemory(call.block) + c.recvSize);
}
}

void RegisterAveWrapper() { IopRegisterServer(Server, "Capcom AVE-TCP wrapper", Serve); }
}
