// Sony's DNAS IOP helper ("Sys1_driver04", which DNAS 2.8's EE library builds in memory and loads from a buffer), stood in
// natively. The EE side asks it, over RPC server 0x80000F03, for the console's and the disc's identities, which only the
// IOP's cdvdman (and the HDD's driver) can read: the clock, the console's model ID and GUID (its i.LINK ID), the disc's ID,
// and the HDD's identity. Each request and answer is a 0x280 byte block wrapped so it can't be read in between: the request
// starts with a 16 byte key and 8 bytes that, XORed with the key, must match the module's constant; the answer is the payload
// after a random 16 byte nonce, a SHA-1 of nonce and payload after it, all but the nonce XORed with a running stream from a
// SHA-1 of the key and nonce, and the rest of the block random.
//
// What the Mac's "console" answers: the clock (the runtime's, cdvd's); the model ID and GUID as cdvdman makes them on a
// console whose mechacon has none (its 0x3EC4: the GUID from the system time under Sony's IEEE OUI 08:00:46, the model
// 0x1A0002); no HDD. The disc's ID (cdvdman's sceCdReadDiskID, 0xBCC8: the mechacon's key read for sector 75, its bytes
// 10-14 when the valid bits' 4 is set) isn't in a disc's image (it's read from the disc's lead-in), so it's made from the
// disc's serial number the way PCSX2 models the mechacon's key (its cdvdReadKey: four zeros, then the digits' bits 5-9
// shifted down by 2, with 4): DNAS's EE library refuses a console that has none (its error -402), and the revived
// servers' DNAS answers take the disc's title from elsewhere in the request.
//
// Offsets are the module's (dumped with $PS2_DUMP_MODULES from Biohazard Outbreak File 2's netdnas, 14264 bytes): the
// dispatch at 0x470, the request's check 0x15B4, the answer's wrap 0x133C (the key's SHA-1 0xFE0, the stream 0x11A4), the
// SHA-1 0x23DC, the random numbers 0x176C, its constants at 0x34E8 and 0x34F8.
#include "iop.h"
#include "ps2/runtime.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>

namespace Ps2
{
extern const Ps2GameInfo* g_game;
}

namespace Ps2::Dnas
{
namespace
{
constexpr u32 Server = 0x80000F03;
constexpr u32 BlockBytes = 0x280;

// The module's constants: the key's mask (0x34E8) and the request's check (0x34F8)
constexpr u8 KeyMask[16] = {0xF2, 0x21, 0x5D, 0xC7, 0xC8, 0xC8, 0xA8, 0xC2, 0x86, 0x5C, 0x9B, 0xE0, 0x33, 0x01, 0x07, 0x5D};
constexpr u8 RequestCheck[8] = {0x8E, 0xDD, 0xCE, 0xFF, 0xEB, 0x51, 0x66, 0x98};

// The functions, by the numbers the EE calls them with (0x470)
enum Function : u32
{
    ReadClock = 0x22799,   // cdvdman 24, sceCdReadClock: 12 bytes (the result, the clock)
    ReadModelId = 0x57709, // cdvdman 82: 12 bytes (the result, the model ID)
    ReadDiskId = 0xC9C69,  // cdvdman 79, sceCdReadDiskID: 9 bytes (the result, 5 bytes)
    ReadGuid = 0xDCD29,    // cdvdman 80, sceCdReadGUID: 12 bytes (the result, 8 bytes)
    HddIdentity = 0x2E9E2, // "hdd0:" devctl 0x6838: 0x24 bytes (the result, 8 and 16 bytes of the drive's answer, 8 zeros)
    Wipe = 0x75C19,        // the block filled with random bytes
};

// --- the module's SHA-1 (0x23DC). Its padding differs from the standard's for a last part of 48 to 55 bytes: those get the
// 0x80 and zeros in a block of their own, and the length a block after
u32 Rotl(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

void Compress(u32 w[16], u32 h[5])
{
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; t++)
    {
        const int s = t & 15;
        if (t >= 16)
        {
            w[s] = Rotl(w[(s + 13) & 15] ^ w[(s + 8) & 15] ^ w[(s + 2) & 15] ^ w[s], 1);
        }
        u32 f, k;
        if (t < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        }
        else if (t < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if (t < 60)
        {
            f = (b & (c | d)) | (c & d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const u32 next = Rotl(a, 5) + f + e + w[s] + k;
        e = d;
        d = c;
        c = Rotl(b, 30);
        b = a;
        a = next;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void LoadWords(u32 w[16], const u8* bytes, int count)
{
    for (int k = 0; k < count; k++)
    {
        w[k] = (u32(bytes[0]) << 24) | (u32(bytes[1]) << 16) | (u32(bytes[2]) << 8) | bytes[3];
        bytes += 4;
    }
}

void Sha1(const u8* data, int length, u8 out[20])
{
    u32 h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    u32 w[16];
    const u32 bits = static_cast<u32>(length) << 3;
    int left = length;
    for (; left > 0x2F; left -= 0x40)
    {
        u8 block[64];
        const u8* from = data;
        if (left < 0x40)
        {
            std::memcpy(block, data, left);
            block[left] = 0x80;
            std::memset(block + left + 1, 0, 0x3F - left);
            from = block;
        }
        LoadWords(w, from, 16);
        Compress(w, h);
        data += 0x40;
    }
    if (left < 1)
    {
        w[0] = left == 0 ? 0x80000000 : 0;
        for (int k = 1; k < 14; k++)
        {
            w[k] = 0;
        }
    }
    else
    {
        u8 block[56];
        std::memcpy(block, data, left);
        block[left] = 0x80;
        std::memset(block + left + 1, 0, 0x37 - left);
        LoadWords(w, block, 14);
    }
    w[14] = 0;
    w[15] = bits;
    Compress(w, h);
    for (int k = 0; k < 5; k++)
    {
        out[k * 4] = static_cast<u8>(h[k] >> 24);
        out[k * 4 + 1] = static_cast<u8>(h[k] >> 16);
        out[k * 4 + 2] = static_cast<u8>(h[k] >> 8);
        out[k * 4 + 3] = static_cast<u8>(h[k]);
    }
}

// --- the module's random numbers (0x176C): seeded from the clock and the system time as it starts (0x180C)
u32 g_seed = 0;
u8 RandomByte()
{
    g_seed = g_seed * 0x41C64E6D + 0x75BCD15;
    const u32 r = ((g_seed << 16) & 0x7FFFFFFF) | (g_seed >> 16);
    return static_cast<u8>(r >> 23);
}

void Randomize(u8* bytes, u32 count)
{
    for (u32 k = 0; k < count; k++)
    {
        bytes[k] = RandomByte();
    }
}

// A request's key (its first 16 bytes), if its check holds (0x15B4)
bool RequestKey(const u8* block, u8 key[16])
{
    std::memcpy(key, block, 16);
    for (int k = 0; k < 8; k++)
    {
        if ((block[16 + k] ^ key[k]) != RequestCheck[k])
        {
            return false;
        }
    }
    return true;
}

// The answer wrapped (0x133C)
void Answer(u8* block, const u8* payload, u32 length, const u8 key[16])
{
    Randomize(block, BlockBytes);
    u8 seed[48];
    std::memcpy(seed, key, 16);
    std::memcpy(seed + 16, block, 16);
    std::memcpy(seed + 32, key, 16);
    for (int k = 0; k < 16; k++)
    {
        seed[16 + k] ^= KeyMask[k];
        seed[32 + k] ^= KeyMask[k];
    }
    u8 stream[20];
    Sha1(seed, 48, stream);
    std::memcpy(block + 0x10, payload, length);
    Sha1(block, static_cast<int>(length) + 0x10, block + 0x10 + length);
    u8* p = block + 0x10;
    for (u32 k = 0; k < BlockBytes - 0x10; k++)
    {
        if (k >= 20)
        {
            p[k] ^= p[k - 20];
        }
        p[k] ^= stream[k % 20];
    }
}

// The identities the Mac's "console" answers with (above)
struct Identity
{
    u8 guid[8];
    u32 model = 0x1A0002;
    u8 disk[5] = {};
};
Identity MakeIdentity()
{
    Identity id;
    // The serial's digits: the executable's name, "SLPM_656.92"
    u32 digits = 0;
    for (const char* p = g_game->executable; *p != 0; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            digits = digits * 10 + static_cast<u32>(*p - '0');
        }
    }
    id.disk[4] = static_cast<u8>(((digits & 0x3E0) >> 2) | 0x04);
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    const u32 low = static_cast<u32>(now) & 0xFF;
    const u32 words[2] = {low | 0x08004600, static_cast<u32>(now >> 32)};
    std::memcpy(id.guid, words, 8);
    return id;
}

void Serve(RpcCall& c)
{
    static const Identity identity = MakeIdentity();
    std::vector<u8> block(c.in, c.in + std::min<u32>(c.inSize, BlockBytes));
    block.resize(BlockBytes);
    u8 key[16];
    std::vector<u8> payload;
    auto result = [&](s32 r, const void* data, u32 size, u32 length) {
        payload.assign(length, 0);
        std::memcpy(payload.data(), &r, 4);
        std::memcpy(payload.data() + 4, data, size);
    };
    const bool valid = c.fno != Wipe && RequestKey(block.data(), key);
    switch (c.fno)
    {
    case ReadClock:
    {
        u8 clock[8];
        CdClock(clock);
        result(1, clock, 8, 12);
        break;
    }
    case ReadModelId:
        result(1, &identity.model, 4, 12);
        break;
    case ReadGuid:
        result(1, identity.guid, 8, 12);
        break;
    case ReadDiskId:
        result(1, identity.disk, 5, 9);
        break;
    case HddIdentity:
    {
        // The devctl fails (no drive: the runtime has no HDD); the module still copies the block's 8 bytes at 0x40 and 16
        // at 0x50 after the result (0x2F8)
        u8 copied[24];
        std::memcpy(copied, block.data() + 0x40, 8);
        std::memcpy(copied + 8, block.data() + 0x50, 16);
        result(-ENODEV, copied, 24, 0x24);
        break;
    }
    case Wipe:
        Randomize(block.data(), BlockBytes);
        break;
    default:
        // The module answers an unknown number with the block as it came
        Log("DNAS IOP: function %#x isn't one of the module's", c.fno);
        break;
    }
    if (!payload.empty())
    {
        if (!valid)
        {
            // The module's answer to a request that fails its check: random bytes where the payload would be, result 0
            Log("DNAS IOP: function %#x: the request's check failed", c.fno);
            Randomize(payload.data(), static_cast<u32>(payload.size()));
            const s32 failed = c.fno == HddIdentity ? -1 : 0;
            std::memcpy(payload.data(), &failed, 4);
        }
        Answer(block.data(), payload.data(), static_cast<u32>(payload.size()), key);
    }
    c.out.assign(block.begin(), block.begin() + std::min<u32>(c.recvSize, BlockBytes));
}
}

void Register()
{
    IopRegisterModule("Sys1_driver04", [](const std::vector<std::string>&) {
        u8 clock[8];
        CdClock(clock);
        const u32 seconds = (u32(clock[1]) << 24) | (u32(clock[2]) << 16) | (u32(clock[3]) << 8) | clock[5];
        g_seed = seconds + static_cast<u32>(std::chrono::steady_clock::now().time_since_epoch().count());
        IopRegisterServer(Server, "DNAS IOP", Serve);
        Log("DNAS IOP: Sys1_driver04 started");
    });
}
}
