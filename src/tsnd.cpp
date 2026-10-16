// Capcom's IOP sound driver ("SoundDriver : Ver%d.%d (%s %s) (C)CAPCOM", tsnddrv.c and tsnddef.c built Oct 24 2003 15:06:40),
// DRIVE.DAT's module 10 in both Files (the same bytes), stood in natively on the runtime's modhsyn and modmsin (ps2/hsyn.h).
//
// The EE asks through two RPC servers. Server 0 (TsndFunc) takes a stream of requests, each a byte and its operands, ended
// by 0xFF: play, change and stop a port's TQ sequences; load a port's HD, BD and TQ from the EE's memory; volumes, reverb,
// mute and stops. Server 1 (TsndPutFunc) answers questions: whether a sequence plays, a port's load state, loads pending.
// Every 200 samples (the module's hard timer: 0x25800 cycles of 36.864 MHz) the tick runs the playing sequences into
// modmsin's ports, then modhsyn's tick plays them.
//
// The module's threads become waits. A request that waits for the next tick (DelayATick) or a few microseconds
// (DelayThread) waits for them in the sound's time, holding the EE as the EE's sceSifCallRpc would be held. The DMA thread
// (a mailbox of loads, at a lower priority than the RPC thread) runs whenever the RPC thread waits or is done; its own wait
// for a tick ends at the tick.
//
// Addresses are the module's (10.irx; the function names are its own symbols), and so are the offsets in its structures
#include "tsnd.h"

#include "ps2/hsyn.h"
#include "ps2/iop.h"
#include "ps2/sound.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace Tsnd
{
namespace
{
using namespace Ps2;
using Lock = std::unique_lock<std::recursive_mutex>;

constexpr u32 FuncServer = 0;     // TsndFunc
constexpr u32 PutStateServer = 1; // TsndPutFunc
constexpr u32 TickSamples = 200;
constexpr u32 TickMicroseconds = 0x1047; // TsndInitModule's (0xC18) to sceHSyn_Init
constexpr u32 EeRamBytes = 32 * 1024 * 1024;

// TsndInitMem(14, 0) (0x7E0): the sequence (SE) ports, then the MIDI out ports (none); together the synthesizer's ports
constexpr int SePorts = 14;
constexpr int MidiOutPorts = 0;
constexpr int SynthPorts = SePorts + MidiOutPorts;

// tsnddef.c's tables (.data). Tsnd_spuadr_tbl (0x4050, Tsnd_spuadrtbl_max 14): the banks' samples' addresses in the sound
// memory
constexpr std::array<u32, 14> SpuAddresses = {0x8000,  0x10000, 0x4D000, 0x7D000,  0xB5000,  0xCD000,  0xE5000,
                                              0xFD000, 0x115000, 0x124000, 0x133000, 0x142000, 0x151000, 0x1E1000};
// Tsnd_hd_size (0x4088) and Tsnd_tq_size (0x40C8): each port's room in the IOP's memory for its HD and its TQ
constexpr std::array<u32, SynthPorts> HdBytes = {0x800,  0x2000, 0x1000, 0x10000, 0x1000, 0x1000, 0x1000,
                                                 0x1000, 0x1000, 0x1000, 0x1000,  0x1000, 0x4000, 0x1000};
constexpr std::array<u32, SynthPorts> TqBytes = {0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
                                                 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000};
// Tsnd_load_tbl (0x4108, Tsnd_load_max 14 entries of 16 bytes: bytes 0-0xB and 0xE are zero and unread): a load ID's
// sound memory address (+0xC, an index into SpuAddresses, or for an autoloaded port into Autoloads), its port (+0xD: below
// 0x20 the port itself, 0x20-0xFE a code an autoloaded port is known by) and what its TQ is (+0xF: 0 none, 1 loaded only, 2
// the port's sequences)
struct LoadEntry
{
    u8 spu;
    u8 port;
    u8 tqKind;
};
constexpr std::array<LoadEntry, 14> Loads = {{{0, 0, 2},
                                              {1, 1, 2},
                                              {2, 2, 2},
                                              {3, 3, 2},
                                              {4, 4, 2},
                                              {5, 5, 2},
                                              {6, 6, 2},
                                              {7, 7, 2},
                                              {8, 8, 2},
                                              {9, 9, 2},
                                              {10, 10, 2},
                                              {11, 11, 2},
                                              {12, 12, 2},
                                              {13, 13, 2}}};
// Tsnd_autoload_def (0x41E8, Tsnd_autoload_max 1, all zero): ports that loads share (the first and how many), the first's
// sound memory address (an index into SpuAddresses) and the stride from one port's to the next's
struct Autoload
{
    u8 first;
    u8 spu;
    s16 count;
    u32 stride;
};
constexpr std::array<Autoload, 1> Autoloads = {{{0, 0, 0, 0}}};

// PortInfo (0x6420, 24 bytes each, one a synthesizer port)
struct PortInfo
{
    u8 state = 0;     // +0: low nibble 1 its sequences ready, 3 loaded without, 0 nothing; high nibble its loads pending
    u8 code = 0xFF;   // +1: the load table's port code it was loaded by (0xFF none)
    s16 load = -1;    // +2: the load ID
    s32 users = 0;    // +4: the autoloads of the same ID sharing it
    u8 channels[16] = {}; // +8: sceHSyn_GetChStat's, each tick while its state is 1
};
constexpr u8 SequencesReady = 1;
constexpr u8 LoadedWithout = 3;
constexpr u8 Pending = 0x10; // a load pending, in the high nibble

// A sequence playing (tqreq_tbl 0x4820: 16 a sequence port, 0x1C bytes each; its index is its MIDI channel)
struct Slot
{
    u32 flags = 0;     // +0 (a word: the next two bytes are its own)
    u8 priority = 0;   // +2
    u8 programAdd = 0; // +3: added to its program changes
    u16 wait = 0;      // +4: ticks until its next command
    u8 sequence = 0;   // +6
    u8 id = 0;         // +7
    u8 volume = 0;     // +8: the request's
    u8 pan = 0;        // +9: the request's (0x40 centre)
    u16 bend = 0;      // +0xA: the request's (0x2000 centre)
    u8 ownVolume = 0;  // +0xC: the sequence's own (0xE0)
    u8 ownPan = 0;     // +0xD (0xE1)
    u16 ownBend = 0;   // +0xE (0xE3)
    u16 sinceNote = 0; // +0x10: ticks since its last note
    u32 age = 0;       // +0x14: ticks
    u32 at = 0;        // +0x18: its next command (in the IOP's memory)
};
// Its flags
constexpr u32 Playing = 0x1;
constexpr u32 Stopping = 0x2;
constexpr u32 StopsWithPort = 0x4; // a request with the port's flag stops it
constexpr u32 StopsWithId = 0x8;
constexpr u32 StopsWithSequence = 0x10;
constexpr u32 SendVolume = 0x20;
constexpr u32 SendPan = 0x40;
constexpr u32 SendBend = 0x80;
constexpr u32 Started = 0x100; // set as it starts, cleared by its program changes; nothing reads it

// A sequence port's TQ (tq_adr 0x4760, tq_num 0x47A0) and its slots (tqreq_cnt 0x47E0 counts those playing)
struct SequencePort
{
    u32 tq = 0;
    u32 sequences = 0;
    s32 playing = 0;
    std::array<Slot, 16> slots;
};

// A load for the DMA thread (DmaLoadMsg 0x4260: 32 of 0x28 bytes)
struct LoadMessage
{
    s16 id = -1;     // +8: -1 free
    u8 oldState = 0; // +0xA: its port's as it was asked for
    u8 port = 0;     // +0xB
    u32 spu = 0;     // +0xC
    u32 hd = 0;      // +0x10: in the EE's memory
    u32 bd = 0;      // +0x14
    u32 tq = 0;      // +0x18
    u32 hdBytes = 0; // +0x1C
    u32 bdBytes = 0; // +0x20
    u32 tqBytes = 0; // +0x24
};

struct Module
{
    bool started = false;
    std::array<PortInfo, SynthPorts> ports;
    std::array<SequencePort, SePorts> sequencePorts;
    // HdAddr (0x125A0) and TqAddr (0x12560): each port's room, one block each, and its end (gp-0x7F90, gp-0x7FB8)
    std::array<u32, SynthPorts> hd = {};
    std::array<u32, SynthPorts> tq = {};
    u32 hdEnd = 0;
    u32 tqEnd = 0;
    std::array<LoadMessage, 32> messages;
    // The DMA thread: its mailbox, the message it's at (waiting for the tick after its port was stopped) and whether that
    // tick has come; it runs only while the RPC thread (server 0's) waits
    std::deque<int> mailbox;
    int loading = -1;
    bool loadDue = false;
    bool requestRunning = false;
    // TfuncResult (0xB470) and PutStResult (0x66A0): the replies, 0x40 bytes each before what follows them
    std::array<u8, 0x40> funcResult = {};
    std::array<u8, 0x40> putResult = {};
    // The ticks run, for the waits; notified at each tick and each delay's sample
    u64 ticks = 0;
    std::condition_variable_any ran;
};
Module g;

u8 Byte(u32 address) { return *IopMemory(address); }

u32 Word(u32 address)
{
    u32 v;
    std::memcpy(&v, IopMemory(address), 4);
    return v;
}

void SetResult(std::array<u8, 0x40>& result, u32 index, u32 value) { std::memcpy(&result[index * 4], &value, 4); }

void Reply(RpcCall& c, const std::array<u8, 0x40>& result)
{
    if (c.recvSize > result.size())
    {
        Fatal("Tsnd: server %u function %08x asked for %u bytes back, more than the module's %zu (what follows them isn't "
              "known)",
              c.sid, c.fno, c.recvSize, result.size());
    }
    std::memcpy(c.out.data(), result.data(), c.recvSize);
}

// sceSifGetOtherData's source: bytes of the EE's memory
const u8* Ee(u32 address, u32 bytes)
{
    u32 at = address & 0x1FFFFFFF;
    if (at > EeRamBytes || bytes > EeRamBytes - at)
    {
        Fatal("Tsnd: a load from the EE's memory past its end (%08x, %u bytes)", address, bytes);
    }
    return EeMemory(at);
}

// sceMSIn_PutMsg: a channel message for a port's next tick (a full buffer drops it, unchecked as the module leaves it)
void Message(int port, u8 status, u8 data1, u8 data2) { Hsyn::PutMsg(port, status | data1 << 8 | data2 << 16); }

// --------------------------------------------------------------------------------------------------------------- the waits
void DmaThread();

// DelayATick(1) (0x1138) on the RPC thread: the DMA thread runs while it waits; it goes on as the next tick ends
void DelayTick(Lock& lock)
{
    DmaThread();
    u64 tick = g.ticks;
    g.ran.wait(lock, [&] { return g.ticks != tick; });
}

// DelayThread(20) (allstop_voice's): until the sound's next sample
void DelaySample(Lock& lock)
{
    DmaThread();
    bool done = false;
    int timer = 0;
    timer = SoundAddTimer(1, [&] {
        done = true;
        SoundRemoveTimer(timer);
        g.ran.notify_all();
    });
    g.ran.wait(lock, [&] { return done; });
}

// --------------------------------------------------------------------------------------------------------------- the ports
// portinfo_init (0xA8C)
void PortInit(int port)
{
    PortInfo& p = g.ports[port];
    p.code = 0xFF;
    p.state = 0;
    p.load = -1;
    p.users = 0;
}

// get_port (0x158C): a port by a code an autoloaded port was loaded by (0x20-0xFE), else by its index; -1 none
int GetPort(s32 code)
{
    int port = code;
    if (static_cast<u32>(code - 0x20) < 0xDF)
    {
        port = SynthPorts - 1;
        while (port >= 0 && g.ports[port].code != code)
        {
            port--;
        }
    }
    return port >= 0 && port < SynthPorts ? port : -1;
}

// tq_getport (0x1608): the sequence port's index (-1 none)
int SequencePortOf(s32 code)
{
    int port = GetPort(code);
    return port < MidiOutPorts ? -1 : port - MidiOutPorts;
}

// ---------------------------------------------------------------------------------------------------------- the sequences
// Tsnd_tq_portstop (0x3050)
void StopSequencePort(int port)
{
    if (port >= SePorts)
    {
        return;
    }
    for (Slot& s : g.sequencePorts[port].slots)
    {
        if (s.flags & Playing)
        {
            s.flags |= Stopping;
        }
    }
}

// port_stop (0x11F8): by what its state was, its sequences told to stop (1) or its voices cut (others but 2, which is none)
void PortStop(int port, u8 state)
{
    if (state == 2)
    {
        return;
    }
    if (state == SequencesReady)
    {
        StopSequencePort(port - MidiOutPorts);
    }
    else
    {
        Hsyn::AllSoundOff(port);
    }
}

// Tsnd_tqreq (0x29BC): a sequence started in a free slot, or one of no higher priority (the oldest of those) taken over.
// The port's, sequence's and ID's bytes carry a flag in bit 7: the slots playing with the same flag (and the same sequence,
// ID) are stopped, and the new one is marked to be stopped by such requests in turn
void StartSequence(u8 portByte, u8 sequenceByte, u8 idByte, u8 volume, u8 pan, u16 bend, u8 programAdd)
{
    int port = portByte & 0x7F;
    if (port >= SePorts || g.ports[port + MidiOutPorts].state != SequencesReady)
    {
        return;
    }
    SequencePort& sp = g.sequencePorts[port];
    if (sp.tq == 0)
    {
        return;
    }
    u8 sequence = sequenceByte & 0x7F, id = idByte & 0x7F;
    // The TQ's +0x10 on: a word a sequence, its offset from the TQ (low half) and its priority (top byte); a TQ without
    // sequences plays from +4 at priority 0x40
    u32 entry;
    if (sequence < sp.sequences)
    {
        entry = Word(sp.tq + 0x10 + sequence * 4);
    }
    else if (sequence == 0 && sp.sequences == 0)
    {
        entry = 0x40000004;
    }
    else
    {
        return;
    }
    u8 priority = static_cast<u8>(entry >> 24);
    int pick = -1;
    for (int i = 15; i >= 0 && pick < 0; i--)
    {
        if (!(sp.slots[i].flags & Playing))
        {
            pick = i;
        }
    }
    if (pick < 0)
    {
        u8 highest = priority != 0 ? priority - 1 : 0;
        u32 oldest = 0;
        for (int i = 15; i >= 0; i--)
        {
            const Slot& s = sp.slots[i];
            if (s.priority <= highest && oldest < s.age)
            {
                oldest = s.age;
                highest = s.priority;
                pick = i;
            }
        }
        if (pick < 0)
        {
            return;
        }
    }
    for (Slot& s : sp.slots)
    {
        bool stops = ((portByte & 0x80) && (s.flags & StopsWithPort)) ||
                     ((idByte & 0x80) && (s.flags & StopsWithId) && s.id == id) ||
                     ((sequenceByte & 0x80) && (s.flags & StopsWithSequence) && s.sequence == sequence);
        if ((s.flags & Playing) && stops)
        {
            s.flags |= Stopping;
        }
    }
    // The slot taken over sends nothing to end what it played: its channel just goes on with the new sequence
    Slot& s = sp.slots[pick];
    s.ownVolume = 0x7F;
    s.ownPan = 0x40;
    s.wait = 0;
    s.sequence = sequence;
    s.id = id;
    s.volume = volume;
    s.pan = pan;
    s.bend = bend;
    s.ownBend = 0x2000;
    s.age = 0;
    s.priority = priority;
    s.at = sp.tq + (entry & 0xFFFF);
    s.programAdd = programAdd;
    if (!(s.flags & Playing))
    {
        sp.playing++;
    }
    s.flags = (s.flags & ~(Stopping | StopsWithPort | StopsWithId | StopsWithSequence)) | Playing |
              ((portByte & 0x80) ? StopsWithPort : 0) | ((idByte & 0x80) ? StopsWithId : 0) |
              ((sequenceByte & 0x80) ? StopsWithSequence : 0) | SendVolume | SendPan | SendBend | Started;
}

// The slots playing a port's sequence with an ID (Tsnd_tqchg, Tsnd_tqcancel, Tsnd_tqchk): none unless its sequences are
// ready
template <typename F> void ForEachPlaying(u8 port, u8 sequence, u8 id, F f)
{
    port &= 0x7F;
    if (port >= SePorts || g.ports[port + MidiOutPorts].state != SequencesReady)
    {
        return;
    }
    for (int i = 15; i >= 0; i--)
    {
        Slot& s = g.sequencePorts[port].slots[i];
        if ((s.flags & Playing) && s.sequence == (sequence & 0x7F) && s.id == (id & 0x7F))
        {
            f(s);
        }
    }
}

// Tsnd_tqchg (0x2DAC): the request's volume, pan and bend changed (each when it isn't negative)
void SequenceChange(u8 port, u8 sequence, u8 id, s8 volume, s8 pan, s16 bend)
{
    ForEachPlaying(port, sequence, id, [&](Slot& s) {
        if (volume >= 0)
        {
            s.volume = static_cast<u8>(volume);
            s.flags |= SendVolume;
        }
        if (pan >= 0)
        {
            s.pan = static_cast<u8>(pan);
            s.flags |= SendPan;
        }
        if (bend >= 0)
        {
            s.bend = static_cast<u16>(bend);
            s.flags |= SendBend;
        }
    });
}

u8 Clamp7(int v) { return static_cast<u8>(std::clamp(v, 0, 0x7F)); }

// tsnd_tq_chgreq (0x3170): what changed sent: the volume (the sequence's scaled by the request's), the pan (the request's
// offset from the centre added to the sequence's) and the bend (likewise)
void SendChanges(Slot& s, int port, u8 channel)
{
    if (s.flags & SendVolume)
    {
        s.flags &= ~SendVolume;
        Message(port, 0xB0 | channel, 7, static_cast<u8>(std::min(s.ownVolume * s.volume / 127, 0x7F)));
    }
    if (s.flags & SendPan)
    {
        s.flags &= ~SendPan;
        Message(port, 0xB0 | channel, 10, Clamp7(static_cast<s8>(s.pan) - 0x40 + s.ownPan));
    }
    if (s.flags & SendBend)
    {
        s.flags &= ~SendBend;
        int bend = std::clamp(s.ownBend + s.bend - 0x2000, 0, 0x3FFF);
        Message(port, 0xE0 | channel, bend & 0x7F, (bend >> 7) & 0x7F);
    }
}

// The slot's end: its channel's notes off (0x78, or 0x7B), the slot free
void EndSlot(Slot& s, SequencePort& sp, int port, u8 channel, u8 controller)
{
    Message(port, 0xB0 | channel, controller, 0);
    s.flags &= ~Playing;
    sp.playing--;
}

// tsnd_tq (0x3358): a slot's commands up to its next wait, as MIDI and modhsyn's messages on the port's channel. Below 0x80
// a wait (in ticks: a byte and a second below 0x80 make cmd + (second << 7), with a second of 0x80 and over it's cmd * 4 + 3
// and the second stays for next); 0x80-0xDF a note (key cmd & 0x7F, 12 below what's played; its velocity, an ID first in a
// byte with bit 7); 0xE0-0xFF controllers, jumps and the end
void Sequence(Slot& s, int port, u8 channel)
{
    SequencePort& sp = g.sequencePorts[port];
    u32 at = s.at;
    if (s.flags & Stopping)
    {
        // Stopped: unless it's at an FE 00 (what goes on after a stop), its channel's notes end with it
        if (Byte(at) == 0xFE && Byte(at + 1) == 0)
        {
            at += 2;
            s.at = at;
            s.flags &= ~Stopping;
        }
        else
        {
            Message(port, 0xB0 | channel, 0x78, 0);
            s.flags &= ~Playing;
            sp.playing--;
            return;
        }
    }
    s.age = s.age == 0xFFFFFFFF ? s.age : s.age + 1;
    s.sinceNote = s.sinceNote == 0xFFFF ? s.sinceNote : s.sinceNote + 1;
    if (s.wait != 0)
    {
        s.wait--;
        SendChanges(s, port, channel);
        return;
    }
    // The channel message being built (s5, fp and s7), kept from one command to the next within the tick: 0xEB sends it
    // again, and 0xEC-0xEE's sub-command is kept in the same place, which 0xEF uses as it is
    u8 status = 0, data1 = 0, data2 = 0;
    // Ends the tick (the commands' position kept, unless back at a command that waits)
    auto finish = [&] {
        s.at = at;
        SendChanges(s, port, channel);
    };
    for (;;)
    {
        u8 cmd = Byte(at++);
        if (cmd < 0x80)
        {
            u8 next = Byte(at);
            if (next & 0x80)
            {
                s.wait = static_cast<u16>(cmd * 4 + 3);
            }
            else
            {
                s.wait = static_cast<u16>(cmd + (next << 7));
                at++;
            }
            finish();
            return;
        }
        if (cmd < 0xE0)
        {
            s.sinceNote = 0;
            u8 v = Byte(at++), id = 0;
            if (v & 0x80)
            {
                id = v & 0x7F;
                v = Byte(at++);
            }
            const u8 note[7] = {0xFD, 0x10, channel, static_cast<u8>((cmd & 0x7F) + 12), id, v, 0};
            Hsyn::PutHsMsg(port, note);
            continue;
        }
        if (cmd < 0xEC)
        {
            switch (cmd)
            {
            case 0xE0: // the sequence's volume
                s.flags &= ~SendVolume;
                s.ownVolume = Byte(at++);
                status = 0xB0, data1 = 7, data2 = static_cast<u8>(std::min(s.ownVolume * s.volume / 127, 0x7F));
                break;
            case 0xE1: // its pan
                s.flags &= ~SendPan;
                s.ownPan = Byte(at++);
                status = 0xB0, data1 = 10, data2 = Clamp7(static_cast<s8>(s.pan) - 0x40 + s.ownPan);
                break;
            case 0xE2: // a program, the request's added
                s.flags &= ~Started;
                status = 0xC0, data1 = Clamp7(Byte(at++) + s.programAdd), data2 = 0;
                break;
            case 0xE3: // its bend (low 7 bits, then high)
            {
                s.flags &= ~SendBend;
                u8 low = Byte(at++), high = Byte(at++);
                s.ownBend = static_cast<u16>(low | high << 7);
                int bend = std::clamp(s.ownBend + s.bend - 0x2000, 0, 0x3FFF);
                status = 0xE0, data1 = bend & 0x7F, data2 = (bend >> 7) & 0x7F;
                break;
            }
            case 0xE4:
                status = 0xB0, data1 = 11, data2 = Byte(at++);
                break;
            case 0xE5:
                status = 0xB0, data1 = 0x7B, data2 = 0;
                break;
            case 0xE6:
                status = 0xB0, data1 = 0x78, data2 = 0;
                break;
            case 0xE7:
                status = 0xB0, data1 = 1, data2 = Byte(at++);
                break;
            case 0xE8:
                status = 0xB0, data1 = 2, data2 = Byte(at++);
                break;
            case 0xE9:
                status = 0xB0, data1 = 0x40, data2 = Byte(at++);
                break;
            case 0xEA:
                status = 0xB0, data1 = 0x54, data2 = Byte(at++);
                break;
            default: // 0xEB: the last message again
                break;
            }
            Message(port, status | channel, data1 & 0x7F, data2 & 0x7F);
            continue;
        }
        if (cmd < 0xF0)
        {
            // modhsyn's own messages for the next note (0xF9) or a note by its ID (0xFD): 0xEC its expression, 0xED its
            // pan, 0xEE its bend (with one more byte)
            u8 v = Byte(at++), id = 0, extra = 0;
            if (v & 0x80)
            {
                id = v & 0x7F;
                v = Byte(at++);
            }
            if (cmd == 0xEC)
            {
                status = 0;
            }
            else if (cmd == 0xED)
            {
                status = 1;
            }
            else if (cmd == 0xEE)
            {
                status = 2;
                extra = Byte(at++);
            }
            if (id != 0)
            {
                const u8 m[7] = {0xFD, status, static_cast<u8>(channel | 0x10), 0, id, v, extra};
                Hsyn::PutHsMsg(port, m);
            }
            else
            {
                const u8 m[5] = {0xF9, status, channel, v, extra};
                Hsyn::PutHsMsg(port, m);
            }
            continue;
        }
        switch (cmd)
        {
        case 0xF8: // a jump within the TQ
            at = sp.tq + (Byte(at) | Byte(at + 1) << 8);
            continue;
        case 0xF9: // reverb preset (NRPN 2/0x10): on channel 0 whatever the slot's, as the module sends it
            Message(port, 0xB0, 0x63, 2);
            Message(port, 0xB0, 0x62, 0x10);
            Message(port, 0xB0, 6, Byte(at++) & 0x7F);
            continue;
        case 0xFA: // a reverb parameter (NRPN 3/0x10 + n), on channel 0 likewise
        {
            u8 parameter = Byte(at++);
            Message(port, 0xB0, 0x63, 3);
            Message(port, 0xB0, 0x62, (parameter | 0x10) & 0x7F);
            Message(port, 0xB0, 6, Byte(at++) & 0x7F);
            continue;
        }
        case 0xFB: // reverb depth
            Message(port, 0xB0 | channel, 0x5B, Byte(at++) & 0x7F);
            continue;
        case 0xFD:
            s.priority = Byte(at++);
            continue;
        case 0xFE:
        case 0xFF:
        {
            // 0xFE 00 and 0xFF wait (on this command) while the channel has voices or a note started this tick; then
            // 0xFE 00 goes on and 0xFF ends the slot. 0xFE and another byte end it at once (0xFE FE with all sound off)
            u8 v = cmd == 0xFE ? Byte(at) : 0;
            if (cmd == 0xFE && v != 0)
            {
                at++;
                EndSlot(s, sp, port, channel, v == 0xFE ? 0x7B : 0x78);
                finish();
                return;
            }
            if (g.ports[port + MidiOutPorts].channels[channel] != 0 || s.sinceNote == 0)
            {
                at--;
                finish();
                return;
            }
            if (cmd == 0xFE)
            {
                at++;
                continue;
            }
            EndSlot(s, sp, port, channel, 0x78);
            finish();
            return;
        }
        default: // 0xF0-0xF7, 0xFC: nothing
            continue;
        }
    }
}

// TsndTQ_ATick (0xF9C)
void SequencesTick()
{
    for (int port = 0; port < SePorts; port++)
    {
        SequencePort& sp = g.sequencePorts[port];
        if (sp.playing == 0)
        {
            continue;
        }
        for (u8 channel = 0; channel < 16; channel++)
        {
            if (sp.slots[channel].flags & Playing)
            {
                Sequence(sp.slots[channel], port, channel);
            }
        }
    }
}

// --------------------------------------------------------------------------------------------------------------- the loads
// set_port_state_of_load (0x1198): one load fewer pending; the last one done leaves the state given (none pending: 0)
void SetLoadState(int port, u8 state)
{
    PortInfo& p = g.ports[port];
    u8 pending = p.state & 0xF0;
    if (pending == 0)
    {
        p.state = 0;
    }
    else
    {
        p.state = pending - Pending != 0 ? static_cast<u8>(pending - Pending) : state;
    }
}

// snd_dmaload (0x12B8) after its wait: the HD into the port's room, the BD into the sound memory, the bank loaded into
// modhsyn, then the TQ; a load with another one pending after it on the same port is skipped. -1 when it fails
int FinishLoad(const LoadMessage& m)
{
    int port = m.port;
    if ((g.ports[port].state & 0xF0) > Pending)
    {
        SetLoadState(port, 0);
        return 0;
    }
    u32 hd = g.hd[port];
    if (hd + m.hdBytes > g.hdEnd)
    {
        Log("Tsnd: load %d: its HD (%u bytes) is past the HD room's end", m.id, m.hdBytes);
        return -1;
    }
    std::memcpy(IopMemory(hd), Ee(m.hd, m.hdBytes), m.hdBytes);
    // Through the module's 0x8000 byte DMA buffer in its time; at once here
    SdVoiceTransWrite(m.spu, Ee(m.bd, m.bdBytes), m.bdBytes);
    if (Hsyn::Load(port, m.spu, hd, 0) != 0)
    {
        Log("Tsnd: load %d: port %d's HD isn't a bank", m.id, port);
        return -1;
    }
    u8 kind = Loads[m.id].tqKind;
    if (kind == 0)
    {
        SetLoadState(port, LoadedWithout);
    }
    else
    {
        u32 tq = g.tq[port];
        if (tq + m.tqBytes > g.tqEnd)
        {
            Log("Tsnd: load %d: its TQ (%u bytes) is past the TQ room's end", m.id, m.tqBytes);
            return -1;
        }
        std::memcpy(IopMemory(tq), Ee(m.tq, m.tqBytes), m.tqBytes);
        if (kind == 2)
        {
            // snd_load_tqinit (0x1270): the TQ's +0xE its count of sequences
            SequencePort& sp = g.sequencePorts[port - MidiOutPorts];
            sp.tq = tq;
            u16 count;
            std::memcpy(&count, IopMemory(tq + 0xE), 2);
            sp.sequences = count;
            SetLoadState(port, SequencesReady);
        }
        else if (kind != 1)
        {
            SetLoadState(port, LoadedWithout);
        }
    }
    if (IopLogging())
    {
        Log("Tsnd: load %d into port %d: HD %u bytes, BD %u bytes at %05x, TQ %u bytes (%u sequences); state %02x", m.id,
            port, m.hdBytes, m.bdBytes, m.spu, kind != 0 ? m.tqBytes : 0, g.sequencePorts[port - MidiOutPorts].sequences,
            g.ports[port].state);
    }
    return 0;
}

// TsndDmaLoad's loop (0x14CC) as far as it goes before it waits: the load whose tick has come finished, then the next
// message's port stopped (snd_dmaload's start) to wait for the next tick
void DmaThread()
{
    if (g.loading >= 0)
    {
        if (!g.loadDue)
        {
            return;
        }
        LoadMessage& m = g.messages[g.loading];
        if (FinishLoad(m) < 0)
        {
            SetLoadState(m.port, 0);
        }
        m.id = -1;
        g.loading = -1;
        g.loadDue = false;
    }
    if (g.mailbox.empty())
    {
        return;
    }
    g.loading = g.mailbox.front();
    g.mailbox.pop_front();
    const LoadMessage& m = g.messages[g.loading];
    PortStop(m.port, m.oldState);
}

bool DmaIdle() { return g.loading < 0 && g.mailbox.empty(); }

// load_getport (0x196C): the load's port and its samples' address; an autoloaded port (a code of 0x20-0xFE) is the one with
// the same load, else the first free one of its range. -1 none
int LoadPort(u8 code, int id, u32& spu)
{
    u8 index = Loads[id].spu;
    if (static_cast<u32>(code - 0x20) < 0xDF)
    {
        if (index >= Autoloads.size())
        {
            return -1;
        }
        const Autoload& a = Autoloads[index];
        int port = -1;
        for (int p = a.first; p < a.first + a.count; p++)
        {
            if (g.ports[p].load == id)
            {
                port = p;
                break;
            }
            if (port < 0 && g.ports[p].state == 0)
            {
                port = p;
            }
        }
        if (port < 0 || a.spu >= SpuAddresses.size())
        {
            return -1;
        }
        spu = SpuAddresses[a.spu] + static_cast<u32>(port - a.first) * a.stride;
        return port;
    }
    if (code >= SynthPorts || index >= SpuAddresses.size())
    {
        return -1;
    }
    spu = SpuAddresses[index];
    return code;
}

// treq_ALL_DMALOAD (0x1AD8) and treq_ALL_DMAAUTOLOAD (0x1DD0): a load ID (2 bytes), then the HD's, BD's and TQ's address and
// size in the EE's memory (6 bytes each, big-endian: the address's low nibble is the size's bits 20-23, the next two bytes
// its bits 12-19 and 4-11). An autoload of an ID its port already has only counts one more user
void LoadRequest(const u8* b, bool autoload)
{
    s16 id = static_cast<s16>(b[0] << 8 | b[1]);
    if (id >= static_cast<s16>(Loads.size()))
    {
        return;
    }
    if (id < 0)
    {
        Fatal("Tsnd: load %d asked for (the module reads before its table)", id);
    }
    auto area = [&](int at, u32& address, u32& bytes) {
        u32 w = static_cast<u32>(b[at] << 24 | b[at + 1] << 16 | b[at + 2] << 8 | b[at + 3]);
        bytes = (w & 0xF) << 20 | b[at + 4] << 12 | b[at + 5] << 4;
        address = w & ~0xFu;
    };
    int index = 0;
    while (index < static_cast<int>(g.messages.size()) && g.messages[index].id >= 0)
    {
        index++;
    }
    if (index == static_cast<int>(g.messages.size()))
    {
        return;
    }
    LoadMessage& m = g.messages[index];
    u8 code = Loads[id].port;
    int port = LoadPort(code, id, m.spu);
    if (port < 0)
    {
        return;
    }
    PortInfo& p = g.ports[port];
    if (autoload && p.load == id)
    {
        p.users++;
        return;
    }
    m.id = id;
    m.port = static_cast<u8>(port);
    area(2, m.hd, m.hdBytes);
    area(8, m.bd, m.bdBytes);
    area(14, m.tq, m.tqBytes);
    m.oldState = p.state;
    p.state = static_cast<u8>((p.state & 0xF0) + Pending);
    if ((p.state & 0xF0) == 0)
    {
        // Sixteen pending: this one dropped
        m.id = -1;
        p.state = 0xF0;
        return;
    }
    p.code = code;
    p.load = id;
    p.users = autoload ? 1 : 0;
    g.mailbox.push_back(index);
}

// -------------------------------------------------------------------------------------------------------------- the requests
// A request stream (TsndFunc's buffer), read as the module reads it
class Requests
{
public:
    Requests(const u8* data, u32 size) : m_at(data), m_end(data + size), m_size(size) {}
    u8 Peek() const
    {
        Need(1);
        return *m_at;
    }
    u8 Next()
    {
        Need(1);
        return *m_at++;
    }
    u16 Next16()
    {
        u8 high = Next();
        return static_cast<u16>(high << 8 | Next());
    }
    const u8* Take(u32 n)
    {
        Need(n);
        const u8* at = m_at;
        m_at += n;
        return at;
    }

private:
    void Need(u32 n) const
    {
        if (static_cast<u32>(m_end - m_at) < n)
        {
            Fatal("Tsnd: the requests ran past what the EE sent (%u bytes) without their 0xFF", m_size);
        }
    }
    const u8* m_at;
    const u8* m_end;
    u32 m_size;
};

// treq_TQ (0x164C), requests 0x00-0x0F: a port (bit 7 its flag), a sequence and an ID (each with its flag), then 8 cancels;
// 9-0xF change (bit 0 a volume, 1 a pan, 2 a bend of 2 bytes follow, then a byte unread); 0-7 play (the same bits, the
// defaults 0x7F, 0x40 and 0x2000, then a byte added to the sequence's programs)
void SequenceRequest(u8 r, Requests& in)
{
    u8 portByte = in.Next(), sequence = in.Next(), id = in.Next();
    u8 port = static_cast<u8>(SequencePortOf(portByte & 0x7F));
    if (r == 8)
    {
        ForEachPlaying(port, sequence, id, [](Slot& s) { s.flags |= Stopping; });
        return;
    }
    if (r & 8)
    {
        s8 volume = (r & 1) ? static_cast<s8>(in.Next()) : -1;
        s8 pan = (r & 2) ? static_cast<s8>(in.Next()) : -1;
        s16 bend = (r & 4) ? static_cast<s16>(in.Next16()) : -1;
        in.Next();
        SequenceChange(port, sequence, id, volume, pan, bend);
        return;
    }
    u8 volume = (r & 1) ? in.Next() : 0x7F;
    u8 pan = (r & 2) ? in.Next() : 0x40;
    u16 bend = (r & 4) ? in.Next16() : 0x2000;
    u8 programAdd = in.Next();
    StartSequence(static_cast<u8>(port | (portByte & 0x80)), sequence, id, volume, pan, bend, programAdd);
}

// spucore_init (0x27C)
void CoreInit(int core)
{
    SdSetAddr(static_cast<u16>(SD_A_EEA | core), 0x1FFFFF);
    SdSetEffectAttr(core, 0x100, 0, 0, 0, 0);
    SdSetCoreAttr(static_cast<u16>(SD_C_EFFECT_ENABLE | core), 1);
    SdSetParam(static_cast<u16>(SD_P_BVOLL | core), 0);
    SdSetParam(static_cast<u16>(SD_P_BVOLR | core), 0);
    SdSetParam(static_cast<u16>(SD_P_MVOLL | core), 0x3FFF);
    SdSetParam(static_cast<u16>(SD_P_MVOLR | core), 0x3FFF);
}

// treq_ALL (0x2244), requests 0x40-0x4E by its table (0x3FD8); false for the unknown ones
bool AllRequest(u8 r, Requests& in, Lock& lock)
{
    switch (r)
    {
    case 0x40: // every port stopped
        for (int port = SynthPorts - 1; port >= 0; port--)
        {
            PortStop(port, g.ports[port].state);
        }
        DelayTick(lock);
        return true;
    case 0x41: // mute (the cores' MUTE_ENABLE set when the byte is 0)
    {
        u16 on = in.Next() == 0;
        SdSetCoreAttr(SD_C_MUTE_ENABLE, on);
        SdSetCoreAttr(SD_C_MUTE_ENABLE | 1, on);
        return true;
    }
    case 0x42: // stereo (a byte not 0) or mono
        Hsyn::SetOutputMode(in.Next() != 0);
        return true;
    case 0x43: // allstop_voice (0x2118): every port stopped, then every voice parked on the silent block, keyed on and off
        for (int port = SynthPorts - 1; port >= 0; port--)
        {
            PortStop(port, g.ports[port].state);
        }
        DelayTick(lock);
        for (int voice = 23; voice >= 0; voice--)
        {
            SdSetAddr(static_cast<u16>(SD_VA_SSA | voice << 1), 0x5080);
            SdSetAddr(static_cast<u16>(SD_VA_SSA | voice << 1 | 1), 0x5080);
        }
        DelaySample(lock);
        SdSetSwitch(SD_S_KON, 0xFFFFFF);
        SdSetSwitch(SD_S_KON | 1, 0xFFFFFF);
        DelaySample(lock);
        SdSetSwitch(SD_S_KOFF, 0xFFFFFF);
        SdSetSwitch(SD_S_KOFF | 1, 0xFFFFFF);
        return true;
    case 0x44: // treq_ALL_REV (0x187C): the reverb of the cores in bits 6 and 7, mode (bits 0-5), depth, delay, feedback
    {
        u8 which = in.Next();
        u16 depth = in.Next16();
        u8 delay = in.Next(), feedback = in.Next();
        for (int core = 0; core < 2; core++)
        {
            if (which >> (6 + core) & 1)
            {
                SdSetEffectAttr(core, which & 0x3F, depth, depth, delay, feedback);
                SdClearEffectWorkArea(core, 0, which & 0x3F);
            }
        }
        return true;
    }
    case 0x45: // the effect returns' volume (BVOL) of the cores in the mask
    {
        u8 mask = in.Next();
        u16 volume = in.Next16();
        for (int core = 0; core < 2; core++)
        {
            if (mask >> core & 1)
            {
                SdSetParam(static_cast<u16>(SD_P_BVOLL | core), volume);
                SdSetParam(static_cast<u16>(SD_P_BVOLR | core), volume);
            }
        }
        return true;
    }
    case 0x47: // a port's user gone: the last one frees it (unless it's loading)
    {
        int port = GetPort(in.Next());
        if (port >= 0 && (g.ports[port].state & 0xF0) == 0 && --g.ports[port].users <= 0)
        {
            PortInit(port);
        }
        return true;
    }
    case 0x48: // a port freed (unless it's loading)
    {
        int port = GetPort(in.Next());
        if (port >= 0 && (g.ports[port].state & 0xF0) == 0)
        {
            PortInit(port);
        }
        return true;
    }
    case 0x49: // an autoload's ports freed (those not loading)
    {
        u8 index = in.Next();
        if (index >= Autoloads.size())
        {
            Fatal("Tsnd: autoload %u's ports freed (the module reads past its table)", index);
        }
        const Autoload& a = Autoloads[index];
        for (int port = a.first; port < a.first + a.count; port++)
        {
            if ((g.ports[port].state & 0xF0) == 0)
            {
                PortInit(port);
            }
        }
        return true;
    }
    case 0x4A: // a port stopped
    {
        int port = GetPort(in.Next());
        if (port >= 0)
        {
            PortStop(port, g.ports[port].state);
        }
        DelayTick(lock);
        return true;
    }
    case 0x4B: // the master volume, both cores
    {
        u16 volume = in.Next16();
        for (int core = 0; core < 2; core++)
        {
            SdSetParam(static_cast<u16>(SD_P_MVOLL | core), volume);
            SdSetParam(static_cast<u16>(SD_P_MVOLR | core), volume);
        }
        return true;
    }
    case 0x4C: // a port's volume
    {
        u8 code = in.Next();
        u16 volume = in.Next16();
        if (int port = GetPort(code); port >= 0)
        {
            Hsyn::SetVolume(port, volume);
        }
        return true;
    }
    case 0x4D:
    case 0x4E:
        LoadRequest(in.Take(20), r == 0x4E);
        return true;
    default: // 0x46, 0x4F
        return false;
    }
}

// tsndReqFunc (0x2670)
bool Request(u8 r, Requests& in, Lock& lock)
{
    switch (r & 0xF0)
    {
    case 0x00:
        SequenceRequest(r, in);
        return true;
    case 0x20:
        if (r == 0x28) // Tsnd_tq_allstop
        {
            for (int port = SePorts - 1; port >= 0; port--)
            {
                StopSequencePort(port);
            }
            return true;
        }
        if (r == 0x29) // Tsnd_tq_setmvol: the volume of every synthesizer port from the sequence ports on
        {
            u16 volume = in.Next16();
            for (int port = SynthPorts - 1; port >= MidiOutPorts; port--)
            {
                Hsyn::SetVolume(port, volume);
            }
            return true;
        }
        return false;
    case 0x40:
        return AllRequest(r, in, lock);
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------- the servers
// TsndFunc (0x2728): function 0 the requests, up to the 0xFF that ends them; its reply's word 0 is 0 (the function number
// for the others)
void Func(RpcCall& c)
{
    Lock lock = SoundLock();
    if (c.fno == 0)
    {
        g.requestRunning = true;
        Requests in(c.in, c.inSize);
        while (in.Peek() != 0xFF)
        {
            u8 r = in.Next();
            if (!Request(r, in, lock))
            {
                // tsndReqError: the rest of the stream ignored
                if (Once("tsnd request " + std::to_string(r)))
                {
                    Log("Tsnd: request %02x unknown: the rest of its stream ignored, as the module does", r);
                }
                break;
            }
        }
        g.requestRunning = false;
        DmaThread();
        SetResult(g.funcResult, 0, 0);
    }
    else
    {
        SetResult(g.funcResult, 0, c.fno);
    }
    Reply(c, g.funcResult);
}

// TsndPutFunc (0x279C): by the function's low byte (bit 31 only raises the thread's priority for the call)
void PutState(RpcCall& c)
{
    Lock lock = SoundLock();
    u32 data = c.In32(0);
    switch (c.fno & 0xFF)
    {
    case 4: // a port's sequence with an ID playing (1) or not
    {
        u8 port = static_cast<u8>(SequencePortOf((data >> 16) & 0x7F));
        bool playing = false;
        ForEachPlaying(port, (data >> 8) & 0x7F, data & 0x7F, [&](Slot&) { playing = true; });
        SetResult(g.putResult, 0, playing);
        break;
    }
    case 6: // a port's state (0x10 while loading, -1 no such port) and with its sequences ready their count
    {
        SetResult(g.putResult, 0, 0);
        SetResult(g.putResult, 1, 0);
        int port = GetPort(static_cast<s32>(data));
        if (port < 0)
        {
            SetResult(g.putResult, 0, static_cast<u32>(-1));
            break;
        }
        u8 state = (g.ports[port].state & 0xF0) ? Pending : g.ports[port].state;
        SetResult(g.putResult, 0, state);
        if (state == SequencesReady)
        {
            SetResult(g.putResult, 1, g.sequencePorts[port - MidiOutPorts].sequences);
        }
        break;
    }
    case 0xC: // the loads pending; with a word of 0, waits for them all
    {
        u32 pending = 0;
        for (const LoadMessage& m : g.messages)
        {
            pending += m.id >= 0;
        }
        if (data == 0 && pending != 0)
        {
            DmaThread();
            g.ran.wait(lock, DmaIdle);
            pending = 0;
        }
        SetResult(g.putResult, 0, pending);
        break;
    }
    case 0x10: // the S/PDIF output's mode
        SetResult(g.putResult, 0, 0);
        SdSetCoreAttr(SD_C_SPDIF_MODE, static_cast<u16>(data));
        break;
    default:
        SetResult(g.putResult, 0, static_cast<u32>(-10));
        break;
    }
    Reply(c, g.putResult);
}

// ATick (0x107C): the ports' channel states, the sequences, then modhsyn's tick; then the waits for it end
void Tick()
{
    for (int port = 0; port < SynthPorts; port++)
    {
        if (g.ports[port].state == SequencesReady)
        {
            Hsyn::GetChStat(port, g.ports[port].channels);
        }
    }
    SequencesTick();
    Hsyn::ATick();
    if (g.loading >= 0)
    {
        g.loadDue = true;
    }
    if (!g.requestRunning)
    {
        DmaThread();
    }
    g.ticks++;
    g.ran.notify_all();
}

// TsndLoop's start (0x48): libsd and the cores, modhsyn and modmsin, the ports' room, the silent block, the tick, the servers
void Start(const std::vector<std::string>&)
{
    if (g.started)
    {
        Fatal("Tsnd: the driver loaded a second time");
    }
    g.started = true;
    // TsndInitMem (0x7E0); its DMA buffer isn't needed here
    u32 hdTotal = 0, tqTotal = 0;
    for (int port = 0; port < SynthPorts; port++)
    {
        g.hd[port] = hdTotal;
        g.tq[port] = tqTotal;
        hdTotal += HdBytes[port];
        tqTotal += TqBytes[port];
    }
    u32 hdBase = IopAlloc(hdTotal), tqBase = IopAlloc(tqTotal);
    for (int port = 0; port < SynthPorts; port++)
    {
        g.hd[port] += hdBase;
        g.tq[port] += tqBase;
    }
    g.hdEnd = hdBase + hdTotal;
    g.tqEnd = tqBase + tqTotal;
    {
        Lock lock = SoundLock();
        SdInit();
        CoreInit(0);
        CoreInit(1);
        SdSetParam(SD_P_MMIX | 1, 0xFFCF);
        SdSetParam(SD_P_AVOLL | 1, 0x7FFF);
        SdSetParam(SD_P_AVOLR | 1, 0x7FFF);
        Hsyn::Init(SynthPorts, TickMicroseconds);
        for (int port = 0; port < SePorts; port++)
        {
            Hsyn::SetVolume(port, 0x100);
        }
        // TsndInitDma (0xA40): 64 bytes of silence at 0x5080, its first block looping on itself (flags 7)
        u8 silence[0x40] = {};
        silence[1] = 7;
        SdVoiceTransWrite(0x5080, silence, sizeof silence);
        for (int port = 0; port < SynthPorts; port++)
        {
            PortInit(port);
        }
        SoundAddTimer(TickSamples, Tick);
    }
    IopRegisterServer(FuncServer, "Tsnd requests", Func);
    IopRegisterServer(PutStateServer, "Tsnd state", PutState);
    Log("Tsnd: Capcom's sound driver started (%d ports; HD room %06x, TQ room %06x)", SynthPorts, hdBase, tqBase);
}
}

void Register() { IopRegisterUnnamedModule("SoundDriver : Ver%d.%d (%s %s) (C)CAPCOM", Start); }
}
