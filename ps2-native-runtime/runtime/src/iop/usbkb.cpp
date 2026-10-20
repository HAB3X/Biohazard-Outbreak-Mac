// Sony's USB keyboard driver (usbkb.irx, "USB_Keyboard_Driver", the IOP side of libusbkb: RPC server 0x80000211) stood in
// natively. The Mac's keyboard is a USB keyboard plugged into port 1, which reports keys while the game takes typed text
// (Ps2GameInfo::typing: its pad has the keys otherwise, input.cpp), so text can be typed where the game reads a keyboard (a
// software keyboard's line, a chat) without the same keys pressing the pad's buttons.
//
// The module (Biohazard Outbreak File 2's MODULES/USBKB.IRX; offsets are its own) keeps per keyboard a ring of 3 reports
// (0x42 bytes each: the LED state then, the report's length, the report: HID boot protocol, the modifiers, a reserved byte
// and six keys' usages) filled at each interrupt transfer (0x16AC, which also toggles the LEDs for the lock keys: Num Lock
// 0x53 bit 0, Caps Lock 0x39 bit 1 when the LED mode is 1 (or 2 with Shift), Scroll Lock 0x47 bit 2, Left GUI 0xE3 bit 3,
// Kana 0x90 bit 4), a full ring dropping its oldest. Its functions (0x76C), on the RPC block: 1 GetInfo (0x844: the most
// keyboards, the count connected, then a byte each, 1 connected); 2 Read (0x8C4: keyboard byte 0; the oldest report taken,
// as byte 0 the LED state, byte 1 the length, the report from 2; with none, the LED state, 0 and zeros); 3 GetLocation
// (0xA50: its 7 byte USB port path); 4 SetLED (0xB8C: byte 1 the LEDs; byte 1 answered as set, or its complement when it
// failed); 5 SetLEDMode (0xCF4: the same); 6 ClearBuffer (0xD88).
#include "iop.h"

#include <array>
#include <cstring>
#include <mutex>

namespace Ps2::UsbKb
{
namespace
{
constexpr u32 Server = 0x80000211;
constexpr u8 MostKeyboards = 2; // the module's default ("keybd=" changes it)
constexpr u8 ReportBytes = 8;   // a boot keyboard's report (the module asks the device's endpoint for its size)

struct Keyboard
{
    std::array<std::array<u8, 2 + ReportBytes>, 3> ring{};
    int readAt = 0;
    int writeAt = 0;
    int count = 0;
    u8 led = 0;
    u8 lockKeysHeld = 0;   // the lock keys in the last report (0x21)
    u8 ledMode = 1;        // 0x22
    std::array<u8, ReportBytes> last{};
};

std::mutex g_lock;
Keyboard g_keyboard; // keyboard 0, the Mac's: always connected

// The lock keys in a report, as the module counts them for the LEDs
u8 LockKeys(const u8* report, u8 ledMode)
{
    u8 keys = 0;
    for (int k = 2; k < ReportBytes; k++)
    {
        switch (report[k])
        {
        case 0x53:
            keys |= 1;
            break;
        case 0x39:
            if (ledMode == 1 || (ledMode == 2 && (report[0] & 0x22) != 0))
            {
                keys |= 2;
            }
            break;
        case 0x47:
            keys |= 4;
            break;
        case 0xE3:
            keys |= 8;
            break;
        case 0x90:
            keys |= 0x10;
            break;
        default:
            break;
        }
    }
    return keys;
}

void Serve(RpcCall& c)
{
    std::lock_guard<std::mutex> lock(g_lock);
    c.out.assign(std::max<u32>(c.recvSize, 2 + ReportBytes), 0);
    const u8 index = c.inSize > 0 ? c.in[0] : 0;
    Keyboard& kb = g_keyboard;
    switch (c.fno)
    {
    case 1:
        c.out[0] = MostKeyboards;
        c.out[1] = 1;
        c.out[2] = 1;
        return;
    case 2:
        if (index != 0)
        {
            c.out[0] = index;
            c.out[1] = 0xFF;
            return;
        }
        if (kb.count == 0)
        {
            c.out[0] = kb.led;
            return;
        }
        std::memcpy(c.out.data(), kb.ring[kb.readAt].data(), 2 + ReportBytes);
        kb.readAt = (kb.readAt + 1) % 3;
        kb.count--;
        return;
    case 3:
        // Port 1 of the console's USB, no hub
        if (index == 0)
        {
            c.out[0] = 1;
        }
        return;
    case 4:
    case 5:
        c.out[0] = index;
        if (index != 0)
        {
            c.out[1] = static_cast<u8>(~c.in[1]);
            return;
        }
        if (c.fno == 4)
        {
            kb.led = c.in[1];
        }
        else
        {
            kb.ledMode = c.in[1];
        }
        c.out[1] = c.in[1];
        return;
    case 6:
        if (index == 0)
        {
            kb.readAt = kb.writeAt = kb.count = 0;
        }
        c.out[0] = index;
        return;
    default:
        Fatal("usbkb: function %u (the module has 1-6)", c.fno);
    }
}
}

void Register() { IopRegisterServer(Server, "usbkb", Serve); }

namespace
{
void Push(Keyboard& kb, const u8* report)
{
    std::memcpy(kb.last.data(), report, ReportBytes);
    const u8 locks = LockKeys(report, kb.ledMode);
    if (locks != kb.lockKeysHeld && kb.ledMode != 0)
    {
        kb.led ^= locks;
        kb.lockKeysHeld = locks;
    }
    auto& entry = kb.ring[kb.writeAt];
    entry[0] = kb.led;
    entry[1] = ReportBytes;
    std::memcpy(entry.data() + 2, report, ReportBytes);
    kb.writeAt = (kb.writeAt + 1) % 3;
    if (kb.count < 3)
    {
        kb.count++;
    }
    else
    {
        kb.readAt = (kb.readAt + 1) % 3;
    }
}
}

// A report whose modifiers and keys both changed goes as two, the modifiers' first: libusbkb takes a report whose modifiers
// changed as that change alone (its read, 0x1DCB88 in File 2: no key then), so Shift and a letter pressed within one of the
// runtime's input updates would lose the letter, where a keyboard polled faster reports them apart
void Report(const u8 report[8])
{
    std::lock_guard<std::mutex> lock(g_lock);
    Keyboard& kb = g_keyboard;
    if (std::memcmp(report, kb.last.data(), ReportBytes) == 0)
    {
        return;
    }
    if (report[0] != kb.last[0] && std::memcmp(report + 2, kb.last.data() + 2, ReportBytes - 2) != 0)
    {
        u8 modifiers[ReportBytes];
        std::memcpy(modifiers, kb.last.data(), ReportBytes);
        modifiers[0] = report[0];
        Push(kb, modifiers);
    }
    Push(kb, report);
}
}
