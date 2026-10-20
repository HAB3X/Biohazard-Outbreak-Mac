// Capcom's USB device searcher (MODULES/NETMOD/USBSRCH.IRX, "Capcom_USB_Device_Searcher", started by LOADHIGH.IRX), stood
// in natively. On the console it watches the USB bus for the modems and Ethernet adaptors the game supports (Conexant and
// SUNTAC modems, I-O Data's USB Ethernet) and the network setup lists what it found. The Mac has none of them: its network
// is presented as Sony's network adaptor (the runtime's netcnf), which the game finds by dev9 being loaded, not on USB.
//
// RPC server 0x01270010 (its dispatch, 0x1A8), the block's words: function 0 counts the devices connected (word 0, the
// count); function 1 gives slot word 0's device (33 slots of 4 words: the vendor and product IDs, a word of the device's,
// the USB device ID; -1 for an empty slot, as the module's table starts, 0x57C)
#include "network.h"

#include "ps2/iop.h"

namespace Network
{
namespace
{
using namespace Ps2;

constexpr u32 Server = 0x01270010;
constexpr u32 Slots = 0x21;

void Serve(RpcCall& c)
{
    switch (c.fno)
    {
    case 0:
        c.Out32(0, 0);
        break;
    case 1:
        if (c.In32(0) >= Slots)
        {
            Log("USB searcher: slot %u asked (it has %u)", c.In32(0), Slots);
        }
        for (u32 word = 0; word < 4; word++)
        {
            c.Out32(word * 4, 0xFFFFFFFF);
        }
        break;
    default:
        Fatal("USB searcher: function %u (the module has 0 and 1)", c.fno);
    }
}
}

void RegisterUsbSearcher() { IopRegisterServer(Server, "Capcom USB searcher", Serve); }
}
