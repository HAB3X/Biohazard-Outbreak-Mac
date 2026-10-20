// Sony's network configuration (netcnf.irx, "NET_configuration", and its RPC interface netcnfif.irx, "Netcnf_Interface",
// server 0x80001101) stood in for natively. On the console these read and write "Your Network Configuration" (the
// BWNETCNF files a network setup tool leaves on the memory card): the adaptor, the address (DHCP or fixed), PPPoE, the DNS
// servers. On the Mac the network is the Mac's: no adaptor or address of the console's is there to set up, and the network
// stack is the host's (avetcp.cpp). So the stand-in presents one configuration, the Mac's network, whatever the card holds:
// Sony's network adaptor (the device "SCE", "Ethernet (Network Adaptor)", as Sony's setup tools name it; games offer only
// the devices they know, by these names: Biohazard Outbreak's table at 0x21F3D0) with its address from DHCP, and the DNS
// server the runtime is given (--dns, or the game's Ps2GameInfo::networkDns; none: the Mac's own resolver). Adding,
// editing or deleting entries isn't possible: those calls fail loudly.
//
// The RPC block (netcnfif's, 0x310 bytes, the same in and out) as the module reads it (its dispatch, 0x3C0): word 0 an
// argument (a count, a size) and the result; word 1 the env's flags; word 2 the list's type (0 combinations, net*.cnf; 1
// interfaces, ifc*.cnf; 2 devices, dev*.cnf); word 3 an EE address the answer is sent to; 0x10 the file's name (256 bytes),
// 0x110 an entry's name (256), 0x210 a new name (256). A list's entries (GetList, sceNetcnfifList_t) are 0x240 bytes: type,
// stat, sys_name (256, the file), usr_name (256, the name shown). An entry loaded (LoadEntry, sceNetcnfifData_t, 0x1340
// bytes) as the module's dump (0xBE8) lays it out: 19 strings of 256 bytes, then ifc_type at 0x1300, mtu, ifc_idle_timeout,
// dev_type (0x130C), phy_config, dialing_type, dev_idle_timeout, then bytes: dhcp (0x1320), dns1_nego, dns2_nego, f_auth,
// auth, pppoe (0x1325), prc_nego, acc_nego, accm_nego. The "type" values are netcnf's keywords' (its parser, 0x5280): eth 1,
// ppp 2, nic 3; phy_config auto 1. 0xFF in a byte and -1 in a word are "not set", as netcnf leaves them.
#include "iop.h"

#include <cstring>

namespace Ps2::Netcnf
{
namespace
{
constexpr u32 Server = 0x80001101;
constexpr u32 ListEntryBytes = 0x240;
constexpr u32 DataBytes = 0x1340;
constexpr u32 TypeEth = 1;
constexpr u32 PhyAuto = 1;
// The one configuration's names: its files' (sys_name) and the name the game shows (usr_name)
constexpr const char* EntryName = "Mac";
const char* const FileNames[3] = {"net000.cnf", "ifc000.cnf", "dev000.cnf"};

enum Function : u32
{
    GetCount = 0,
    GetList = 1,
    LoadEntry = 2,
    AddEntry = 3,
    EditEntry = 4,
    DeleteEntry = 5,
    SetLatestEntry = 6,
    DeleteAll = 7,
    CheckCapacity = 8,
    ConvertAnsi = 9,
    CheckAdditionalAt = 10,
    SetEnv = 11,
    ClearEnv = 15,
    GetAddr = 100,
    AllocWorkArea = 101,
    FreeWorkArea = 102,
    SetEnvData = 103,
};

void PutString(u8* data, u32 offset, const std::string& s)
{
    std::memset(data + offset, 0, 256);
    std::memcpy(data + offset, s.data(), std::min<size_t>(s.size(), 255));
}

void Put32(u8* data, u32 offset, u32 value) { std::memcpy(data + offset, &value, 4); }

// The configuration as LoadEntry gives it: a combination (type 0) has the interface's and the device's parts, an interface
// (1) or a device (2) only its own
void FillData(u8* data, u32 type)
{
    std::memset(data, 0, DataBytes);
    for (u32 word = 0x1300; word < 0x1320; word += 4)
    {
        Put32(data, word, 0xFFFFFFFF);
    }
    std::memset(data + 0x1320, 0xFF, 9);
    if (type == 0 || type == 1)
    {
        if (type == 0)
        {
            // A combination names its interface and device by their files (sys_name), as the lists give them
            PutString(data, 0x000, FileNames[1]); // attach_ifc
            PutString(data, 0x100, FileNames[2]); // attach_dev
        }
        PutString(data, 0x600, g_options.dns); // dns1_address (empty: the Mac's resolver answers)
        Put32(data, 0x1300, TypeEth);          // ifc_type
        data[0x1320] = 1;                      // dhcp
    }
    if (type == 0 || type == 2)
    {
        PutString(data, 0xE00, "SCE");                        // vendor
        PutString(data, 0xF00, "Ethernet (Network Adaptor)"); // product
        Put32(data, 0x130C, TypeEth);                         // dev_type
        Put32(data, 0x1310, PhyAuto);                         // phy_config
    }
}

void Serve(RpcCall& c)
{
    // The answer is the block as it came, its word 0 the result
    c.out.assign(c.in, c.in + std::min(c.inSize, c.recvSize));
    c.out.resize(std::max<size_t>(c.out.size(), 16));
    const u32 type = c.In32(8);
    const u32 ee = c.In32(12);
    s32 result = 0;
    switch (c.fno)
    {
    case GetCount:
        result = type <= 2 ? 1 : -1;
        break;
    case GetList:
    {
        if (type > 2)
        {
            result = -1;
            break;
        }
        const u32 room = c.In32(0);
        if (room >= 1)
        {
            u8* entry = EeMemory(ee);
            std::memset(entry, 0, ListEntryBytes);
            Put32(entry, 0, type);
            PutString(entry, 8, FileNames[type]);
            PutString(entry, 0x108, EntryName);
        }
        result = 1;
        break;
    }
    case LoadEntry:
    {
        const std::string name = c.InString(0x110);
        if (type > 2 || name != EntryName)
        {
            Log("netcnfif: LoadEntry of \"%s\" (type %u): the only configuration is \"%s\"", name.c_str(), type, EntryName);
            result = -1;
            break;
        }
        FillData(EeMemory(ee), type);
        Log("netcnfif: \"%s\" (type %u) loaded: DHCP, DNS %s", name.c_str(), type,
            g_options.dns.empty() ? "the Mac's" : g_options.dns.c_str());
        break;
    }
    case SetLatestEntry:
        // The one entry is the latest already
        break;
    case CheckCapacity:
        // Room for the configuration: there's nothing to write
        break;
    case SetEnv:
    case ClearEnv:
    case AllocWorkArea:
    case FreeWorkArea:
        // The module's own working state (its env, the file callbacks, its work area): nothing the stand-in needs
        break;
    case AddEntry:
    case EditEntry:
    case DeleteEntry:
    case DeleteAll:
    case ConvertAnsi:
    case CheckAdditionalAt:
    case GetAddr:
    case SetEnvData:
    default:
        Log("netcnfif: function %u (type %u, \"%s\") isn't possible: the network configuration is the Mac's", c.fno, type,
            c.InString(0x110).c_str());
        result = -1;
        break;
    }
    std::memcpy(c.out.data(), &result, 4);
}
}

void Register() { IopRegisterServer(Server, "netcnfif", Serve); }
}
