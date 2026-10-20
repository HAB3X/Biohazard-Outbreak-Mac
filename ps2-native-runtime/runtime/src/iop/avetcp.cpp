// ACCESS's AVE-TCP 3.1, its device glue and its DHCP client stood in on the Mac's own network (ps2/avetcp.h). The console's
// stack runs TCP/IP itself over a device (Sony's network adaptor through its driver, smap.irx, or a USB modem); here the
// Mac's sockets do the TCP/IP (avetcp_socket.cpp) and its resolver's protocol the DNS (avetcp_dns.cpp), and the device is
// the Mac's network, shown to the game as Sony's network adaptor once the game loads that driver. What the stack would set
// up on the device (its address, routes, DHCP) is answered with what the Mac has: the Mac's own address, mask and router,
// leased for good.
//
// Offsets and values are the modules' (MODULES/NETMOD/AVETCP.IRX, MODULES/NETMOD_E/DEVGLUE.IRX, AVEDHCP.IRX and SMAP.IRX on
// Biohazard Outbreak File 2's disc); each function says where it was read.
#include "avetcp.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <resolv.h>
#include <string>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <vector>

namespace Ps2::AveTcp
{
std::recursive_mutex& StackLock()
{
    static std::recursive_mutex lock;
    return lock;
}

namespace
{
// --- the Mac's network
struct HostNetwork
{
    u32 address = 0;   // network order, as the stack keeps addresses
    u32 mask = 0;
    u32 broadcast = 0;
    u32 router = 0;
    u32 dns[2] = {};
    std::string domain;
};

// The default route's gateway and interface (the routing table as sysctl gives it), and that interface's IPv4 address
HostNetwork ReadHostNetwork()
{
    HostNetwork net;
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    size_t size = 0;
    unsigned index = 0;
    if (sysctl(mib, 6, nullptr, &size, nullptr, 0) == 0 && size > 0)
    {
        std::vector<char> table(size);
        if (sysctl(mib, 6, table.data(), &size, nullptr, 0) == 0)
        {
            for (size_t at = 0; at < size;)
            {
                auto* rtm = reinterpret_cast<rt_msghdr*>(table.data() + at);
                auto* dst = reinterpret_cast<sockaddr_in*>(rtm + 1);
                if ((rtm->rtm_addrs & RTA_DST) && (rtm->rtm_addrs & RTA_GATEWAY) && dst->sin_family == AF_INET &&
                    dst->sin_addr.s_addr == 0)
                {
                    auto* gw = reinterpret_cast<sockaddr_in*>(reinterpret_cast<char*>(dst) +
                                                              ((dst->sin_len + 3) & ~3u));
                    if (gw->sin_family == AF_INET)
                    {
                        net.router = gw->sin_addr.s_addr;
                        index = rtm->rtm_index;
                        break;
                    }
                }
                at += rtm->rtm_msglen;
            }
        }
    }
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0)
    {
        for (ifaddrs* a = list; a != nullptr; a = a->ifa_next)
        {
            if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) ||
                !(a->ifa_flags & IFF_UP) || (index != 0 && if_nametoindex(a->ifa_name) != index))
            {
                continue;
            }
            net.address = reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr;
            net.mask = reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr;
            net.broadcast = net.address | ~net.mask;
            break;
        }
        freeifaddrs(list);
    }
    struct __res_state state = {};
    if (res_ninit(&state) == 0)
    {
        for (int n = 0, k = 0; n < state.nscount && k < 2; n++)
        {
            if (state.nsaddr_list[n].sin_family == AF_INET)
            {
                net.dns[k++] = state.nsaddr_list[n].sin_addr.s_addr;
            }
        }
        net.domain = state.defdname;
        res_ndestroy(&state);
    }
    if (net.address == 0)
    {
        Log("AVE-TCP: the Mac has no IPv4 address on its default route's interface: the network looks down to the game");
    }
    return net;
}

// --- the device glue (DEVGLUE.IRX)
// Its devices: up to two modems (IDs 0 and 1) and two Ethernet devices (2 and 3), each registered by its driver. The Mac's
// network is the one Ethernet device, as smap.irx registers Sony's network adaptor (its netdev record, 0x33EC: the module
// "smap", the vendor "SCE", the product "Ethernet (Network Adaptor)", the bus 5)
struct Glue
{
    bool ethernet = false;     // the adaptor's driver loaded
    bool changed = false;      // devices came or went since the list was taken (0x9730)
    s32 selected = -1;         // the Ethernet device in use (0x970C)
    u32 parameters[10] = {};   // what the glue's parameters were set to (devglue 7)
    bool ndiInit = false;      // NDI_init (0x3488) done: the driver's entry made
    bool ndiStarted = false;   // NDI_start (0x3728)
    u32 driverEntry = 0;       // the entry NDI_getDriverEntry gives (an IOP block: the module's is at 0xDBCC)
};
Glue g_glue;

constexpr u32 AdaptorId = 2;
constexpr u32 AdaptorBus = 1; // devglue's record says 1 for a driver's bus 5 (its 0x440)

void PutName(u8* at, const char* name)
{
    std::memset(at, 0, 256);
    std::strncpy(reinterpret_cast<char*>(at), name, 255);
}

// --- the stack (AVETCP.IRX) and its DHCP client (AVEDHCP.IRX)
struct Stack
{
    bool started = false;
    u16 option1 = 0;           // option 1 (0x412E, kept at 0x16194)
    u32 attached[4] = {};      // the drivers' entries attached to the interfaces (0x4141, 0x16190)
    bool addressSet = false;   // the interface's address set (0x4105)
    u32 address[3] = {};       // address, mask, broadcast as set (0x17E00)
    // DHCP: its state (export 8, 0x2EB0: 0 idle, 1 discovering, 3 bound, -1 failed), the addresses the start asked for
    // to be written (export 11: 0x4254-0x425C), the host name (export 9)
    s32 dhcp = 0;
    u32 dhcpOut[3] = {};
    std::string hostName;
    HostNetwork host;
};
Stack g_stack;

// 0x412E (0x11210): an option set: its ID (16 bits), its size (16 bits), the address of its value. Option 1 is a switch
// (0 or 1, 4 bytes); what it switches in the module isn't known (it's kept at 0x16194)
s16 SetOption(u32 in)
{
    const u16 id = R16(in);
    const u16 size = R16(in + 2);
    if (id != 1 || size < 4)
    {
        return ErrorParameter;
    }
    const u32 value = R32(R32(in + 4));
    if (value >= 2)
    {
        return ErrorParameter;
    }
    g_stack.option1 = static_cast<u16>(value);
    return 0;
}

// 0x4105 (0xF8F0): the interface's address set (the interface (0) at 0, the address, mask and broadcast at 4, 8, 12, network
// order); 0x4106 (0xFA40) clears it. The Mac's sockets take the Mac's own address: kept for the game to read back
s16 SetAddress(u32 in, bool set)
{
    if (R16(in) != 0)
    {
        return ErrorFailed;
    }
    g_stack.addressSet = set;
    for (u32 n = 0; n < 3; n++)
    {
        g_stack.address[n] = set ? R32(in + 4 + n * 4) : 0;
    }
    if (set)
    {
        in_addr a{g_stack.address[0]};
        Log("AVE-TCP: the interface's address set to %s", inet_ntoa(a));
    }
    return 0;
}

// 0x4130 (0x112EC) adds a route (its destination, gateway and metric), 0x4131 (0x113A8) deletes one: the Mac's own routing
// table routes the Mac's sockets, so they're taken as done
s16 Route(u32 in, bool add)
{
    in_addr destination{R32(in)}, gateway{R32(in + 4)};
    std::string d = inet_ntoa(destination);
    PS2_LOG(LogIop, "AVE-TCP: route %s %s via %s", add ? "added" : "deleted", d.c_str(), inet_ntoa(gateway));
    return 0;
}

// 0x4141 (export 23, 0xE318): a driver's entry (at 4) attached to an interface (the index at 0); 0x4142 (export 24) detached;
// 0x4143 (export 25) gives an interface's entry (out word 0)
s16 Attach(u32 in, u32 out, u16 code)
{
    const s16 index = static_cast<s16>(R16(in));
    if (index < 0 || index >= 4)
    {
        return -1;
    }
    if (code == 0x4141)
    {
        const u32 entry = R32(in + 4);
        if (entry == 0 || entry != g_glue.driverEntry || g_stack.attached[index] != 0)
        {
            return -1;
        }
        g_stack.attached[index] = entry;
        return 0;
    }
    if (code == 0x4142)
    {
        if (g_stack.attached[index] == 0)
        {
            return -1;
        }
        g_stack.attached[index] = 0;
        return 0;
    }
    W32(out, g_stack.attached[index]);
    return 0;
}

// 0x4161-0x4165, the device glue's NDI functions (DEVGLUE's table at 0x2C30): NDI_init (0x3488) makes the selected Ethernet
// device's driver entry, NDI_disp (0x3618) and NDI_finish (0x393C) end it, NDI_getDriverEntry (0x36AC) gives the entry (out
// word 0), NDI_start (0x3728) starts the device: its block's word 1 (16 bits) 3 asks whether the link is up (1) instead
s16 Ndi(u16 code, u32 in, u32 out)
{
    switch (code)
    {
    case 0x4161:
        if (g_glue.selected < 0)
        {
            return -1;
        }
        if (g_glue.driverEntry == 0)
        {
            g_glue.driverEntry = IopAlloc(0x230);
            std::memset(IopMemory(g_glue.driverEntry), 0, 0x230);
        }
        g_glue.ndiInit = true;
        return 0;
    case 0x4162:
    case 0x4165:
        g_glue.ndiStarted = false;
        return 0;
    case 0x4163:
        W32(out, g_glue.ndiInit ? g_glue.driverEntry : 0);
        return 0;
    case 0x4164:
    {
        const u16 mode = in != 0 ? R16(in + 2) : 1;
        if (mode == 3)
        {
            // The link is up when the Mac has an address to go out on
            g_stack.host = ReadHostNetwork();
            return g_glue.ndiStarted && g_stack.host.address != 0 ? 1 : 0;
        }
        g_glue.ndiStarted = g_glue.ndiInit;
        return g_glue.ndiStarted ? 0 : -1;
    }
    default:
        return ErrorUnsupported;
    }
}

// 0x4171-0x417C, AVE-DHCP's functions (its table, AVEDHCP.IRX 0x60-0x2A0). The Mac's own address is the lease: a start binds
// at once, with no lease time (AVE-DHCP's 0xFFFFFFFF), so it's never renewed. The addresses are network order, as the
// module keeps them from the packets; the times host order
s16 Dhcp(u16 code, u32 in, u32 out)
{
    (void)out;
    auto bound = [] { return g_stack.dhcp == 3; };
    switch (code)
    {
    case 0x4171: // init (export 4)
        g_stack.dhcp = 0;
        return 0;
    case 0x4172: // end (export 12)
        g_stack.dhcp = 0;
        return 0;
    case 0x4173: // export 5: the domain name (a string), the DNS servers (export 4's ids 3, 4, 5)
        if (!bound())
        {
            return -1;
        }
        std::strcpy(reinterpret_cast<char*>(IopMemory(R32(in + 4))), g_stack.host.domain.c_str());
        W32(R32(in + 8), g_stack.host.dns[0]);
        W32(R32(in + 12), g_stack.host.dns[1]);
        return 0;
    case 0x4174: // export 6: the router (id 6)
        if (!bound())
        {
            return -1;
        }
        W32(R32(in + 4), g_stack.host.router);
        return 0;
    case 0x4175: // export 7: the lease's time, T1 and T2 (ids 7, 8, 9)
        if (!bound())
        {
            return -1;
        }
        for (u32 n = 0; n < 3; n++)
        {
            W32(R32(in + n * 4), 0xFFFFFFFF);
        }
        return 0;
    case 0x4176: // export 8: the state
        return static_cast<s16>(g_stack.dhcp);
    case 0x4177: // export 9: the host name sent (1 to 30 characters), the block itself
    {
        const std::string name(reinterpret_cast<const char*>(IopMemory(in)), strnlen(reinterpret_cast<const char*>(IopMemory(in)), 256));
        if (name.empty() || name.size() > 31)
        {
            return -1;
        }
        g_stack.hostName = name;
        return 0;
    }
    case 0x4178: // export 10: a start that waits for the lease (0 bound)
    case 0x4179: // export 11: a start; export 13 polls it (the address, mask and broadcast written where its words 4-6 say)
    {
        if (g_stack.dhcp != 0)
        {
            return -1;
        }
        for (u32 n = 0; n < 3; n++)
        {
            g_stack.dhcpOut[n] = R32(in + 0x10 + n * 4);
            if (g_stack.dhcpOut[n] == 0)
            {
                return -1;
            }
        }
        g_stack.host = ReadHostNetwork();
        if (g_stack.host.address == 0)
        {
            g_stack.dhcp = -1;
            return -1;
        }
        g_stack.dhcp = 3;
        W32(g_stack.dhcpOut[0], g_stack.host.address);
        W32(g_stack.dhcpOut[1], g_stack.host.mask);
        W32(g_stack.dhcpOut[2], g_stack.host.broadcast);
        in_addr a{g_stack.host.address};
        Log("AVE-TCP: DHCP bound to the Mac's address %s", inet_ntoa(a));
        return 0;
    }
    case 0x417A: // export 13: the state after a step of the client
        return static_cast<s16>(g_stack.dhcp);
    case 0x417B: // export 14: the lease released, waiting until it is (0)
    case 0x417C: // export 15: the lease's release started (state 6); export 13 polls it back to 0
        if (g_stack.dhcp == 3)
        {
            Log("AVE-TCP: DHCP lease released");
        }
        g_stack.dhcp = 0;
        return 0;
    default:
        Fatal("AVE-TCP: DHCP function %04x isn't done", code);
    }
}
}

void ModuleLoaded(const std::string& name)
{
    if (name == "INET_SMAP_driver")
    {
        Lock lock(StackLock());
        g_glue.ethernet = true;
        g_glue.changed = true;
        Log("AVE-TCP: the Mac's network is Sony's network adaptor (device %u)", AdaptorId);
    }
}

// devglue 4 (0x56C): each device's record, the modems' then the Ethernet devices' (0x440 writes an Ethernet device's)
s32 DeviceList(u8* out, u32 room)
{
    Lock lock(StackLock());
    s32 count = 0;
    if (g_glue.ethernet)
    {
        if (room < DeviceRecordBytes)
        {
            return 0;
        }
        std::memset(out, 0, DeviceRecordBytes);
        const u32 words[3] = {AdaptorId, AdaptorBus, 1};
        std::memcpy(out, words, sizeof(words));
        PutName(out + 0x0C, "SCE");
        PutName(out + 0x10C, "Ethernet (Network Adaptor)");
        PutName(out + 0x20C, "smap");
        count++;
    }
    g_glue.changed = false;
    return count;
}

// devglue 5 (0x6AC): -1 while the devices changed and their list wasn't taken, or for no such device
s32 DeviceSelect(u32 id)
{
    Lock lock(StackLock());
    if (g_glue.changed || id != AdaptorId || !g_glue.ethernet)
    {
        return -1;
    }
    g_glue.selected = static_cast<s32>(id - 2);
    return 0;
}

// devglue 6 (0x27C)
s32 DeviceCount(u32& count)
{
    Lock lock(StackLock());
    count = g_glue.ethernet ? 1 : 0;
    return g_glue.changed ? 1 : 0;
}

// devglue 7 (0x730): 1 and 2 the glue's threads' priorities, 3 a switch (0 or 1), 9 a modem's; 4 and 5 the modem's too. Kept,
// and given back by devglue 8: the Mac's network has no threads or modem of the glue's to apply them to
s32 DeviceSetParameter(u32 key, const u8* value, u32 size)
{
    Lock lock(StackLock());
    if ((key == 1 || key == 2 || key == 3 || key == 9) && size == 4)
    {
        u32 v;
        std::memcpy(&v, value, 4);
        if (key == 3 && v > 1)
        {
            return -1;
        }
        g_glue.parameters[key] = v;
        return 0;
    }
    Fatal("AVE-TCP: device glue parameter %u (%u bytes) isn't done", key, size);
}

// devglue 8 (0x850): 1-3 as set; 8 the selected Ethernet device's link status (0x50F4: its driver's
// sceInetNDCC_GET_LINK_STATUS, which smap.irx answers with its link count, 0x38 of its record: more than 0 when up)
s32 DeviceGetParameter(u32 key, u8* value, u32 size)
{
    Lock lock(StackLock());
    if ((key == 1 || key == 2 || key == 3) && size >= 4)
    {
        std::memcpy(value, &g_glue.parameters[key], 4);
        return 4;
    }
    if (key == 8 && size >= 4)
    {
        if (g_glue.selected < 0)
        {
            const u32 none = 0;
            std::memcpy(value, &none, 4);
            return 4;
        }
        g_stack.host = ReadHostNetwork();
        const u32 up = g_stack.host.address != 0 ? 1 : 0;
        std::memcpy(value, &up, 4);
        return 4;
    }
    Fatal("AVE-TCP: device glue parameter %u (%u bytes) asked, which isn't done", key, size);
}

s32 Init(u32 parameters)
{
    (void)parameters;
    Lock lock(StackLock());
    SocketsClosed();
    DnsReset();
    g_stack = Stack{};
    g_stack.started = true;
    Log("AVE-TCP: started on the Mac's network");
    return 0;
}

s32 Dispose()
{
    Lock lock(StackLock());
    SocketsClosed();
    DnsReset();
    g_stack = Stack{};
    Log("AVE-TCP: stopped");
    return 0;
}

s16 ApiCall(u16 code, u32 in, u32 out)
{
    Lock lock(StackLock());
    if (!g_stack.started)
    {
        Fatal("AVE-TCP: function %04x called before the stack started", code);
    }
    SocketsPoll();
    if (code == 0x4108 || code == 0x4109 || (code >= 0x4110 && code <= 0x4125))
    {
        return SocketCall(code, in, out);
    }
    if (code >= 0x4161 && code <= 0x4165)
    {
        return Ndi(code, in, out);
    }
    if (code >= 0x4171 && code <= 0x417C)
    {
        return Dhcp(code, in, out);
    }
    if (code >= 0x4181 && code <= 0x4187)
    {
        return DnsCall(code, in, out);
    }
    switch (code)
    {
    case 0x412E:
        return SetOption(in);
    case 0x4105:
    case 0x4106:
        return SetAddress(in, code == 0x4105);
    case 0x4130:
    case 0x4131:
        return Route(in, code == 0x4130);
    case 0x4141:
    case 0x4142:
    case 0x4143:
        return Attach(in, out, code);
    default:
        break;
    }
    Fatal("AVE-TCP: function %04x (in %06x, out %06x) isn't done", code, in, out);
}
}
