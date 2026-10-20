// AVE-TCP's TCP functions on the Mac's sockets (avetcp.h). A handle is a TCP control block of the module's (its table at
// 0x16180, 0x17C bytes each); here it's a non-blocking socket. Its state is TCP's, numbered as BSD numbers them, which the
// module keeps (0x13A: 2 SYN_SENT after an open, 0xCE88; 4 ESTABLISHED and 5 CLOSE_WAIT, the states the wrapper sends and
// receives in; 1 LISTEN, 10 TIME_WAIT, 0xCA20). Why a connection went down is its "reason" (0x142: 4 reset, 6 timed out),
// which the status function turns into its result (0x101F4).
//
// The blocks, as the module reads them: an open (0x4110, 0xFA8C) the remote address at 0, the remote port at 4, the local port
// at 6 (0: any), addresses and ports in network order as AVE-TCP keeps them, options at 8-0xF and an ASR at 0x10; a send (0x4118, 0x102E4) the handle at 0, an ASR
// at 4 (0: wait until sent), the count of pieces at 9 (up to 4) and the pieces from 0xC, each a 16-bit length and an
// address 8 bytes apart; a receive (0x4119, 0x1090C) the handle at 0, the length at 2, the buffer's address at 4, an ASR at 8
// (0: wait for data). A status (0x4117, 0x101F4) writes the state, the count of connections a listener has, the room to
// send and the bytes waiting to be received (16 bits each). An option set or got (0x4108, 0x10CE4 and 0xD058; 0x4109,
// 0x10D2C and 0xD204): the handle at 0, the option at 2, the value's size at 4, the value's address at 8.
#include "avetcp.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <string>

namespace Ps2::AveTcp
{
namespace
{
enum TcpState : s16
{
    Closed = 0,
    Listen = 1,
    SynSent = 2,
    Established = 4,
    CloseWait = 5,
    FinWait1 = 6,
    LastAck = 8,
    FinWait2 = 9,
};

// Why a connection went down (the module's 0x142)
enum Reason : u8
{
    None = 0,
    Reset = 4,
    TimedOut = 6,
    Other = 1,
};

struct Tcp
{
    bool used = false;
    int fd = -1;
    TcpState state = Closed;
    Reason reason = None;
    bool sentFin = false;
    u32 remote = 0;     // network order, as the blocks have them
    u16 remotePort = 0;
    u16 localPort = 0;
    // The options (0x4108), as a new handle has them (0xC968)
    u32 ackAtOnce = 1;      // option 0x2001 (0x178: the module's default, its 0x15740)
    u32 keepAlive = 0;      // option 8: the idle seconds before keep-alive probes, 0 none (0x16C, in half seconds)
    u32 noDelay = 0;        // option 0x2008 (0x13F's bit 2)
};

// The module's TCP handles (its count, 0x1619B, is set as it starts; 16 here)
constexpr int TcpHandles = 16;
std::array<Tcp, TcpHandles> g_tcp;

std::string AddressText(u32 address, u16 port)
{
    in_addr a{address};
    return std::string(inet_ntoa(a)) + ":" + std::to_string(ntohs(port));
}

void Drop(Tcp& t)
{
    if (t.fd >= 0)
    {
        close(t.fd);
    }
    t = Tcp{};
}

Tcp* Find(u32 in)
{
    const s16 handle = static_cast<s16>(R16(in));
    if (handle < 0 || handle >= TcpHandles || !g_tcp[handle].used)
    {
        return nullptr;
    }
    return &g_tcp[handle];
}

// Where a connection is now, from its socket: a connect finished or failed, the peer closed or reset it
void Update(Tcp& t)
{
    if (t.fd < 0)
    {
        return;
    }
    if (t.state == SynSent)
    {
        pollfd p{t.fd, POLLOUT, 0};
        if (poll(&p, 1, 0) <= 0)
        {
            return;
        }
        int error = 0;
        socklen_t size = sizeof(error);
        getsockopt(t.fd, SOL_SOCKET, SO_ERROR, &error, &size);
        if (error == 0)
        {
            t.state = Established;
            Log("AVE-TCP: connected to %s", AddressText(t.remote, t.remotePort).c_str());
        }
        else
        {
            t.state = Closed;
            t.reason = error == ETIMEDOUT ? TimedOut : (error == ECONNREFUSED || error == ECONNRESET) ? Reset : Other;
            Log("AVE-TCP: connecting to %s failed: %s", AddressText(t.remote, t.remotePort).c_str(), strerror(error));
        }
        return;
    }
    if (t.state == Established || t.state == FinWait1 || t.state == FinWait2)
    {
        int waiting = 0;
        if (ioctl(t.fd, FIONREAD, &waiting) == 0 && waiting > 0)
        {
            return;
        }
        u8 byte;
        const ssize_t got = recv(t.fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (got == 0)
        {
            t.state = t.sentFin ? Closed : CloseWait;
        }
        else if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            t.state = Closed;
            t.reason = errno == ETIMEDOUT ? TimedOut : errno == ECONNRESET ? Reset : Other;
            Log("AVE-TCP: the connection to %s went down: %s", AddressText(t.remote, t.remotePort).c_str(), strerror(errno));
        }
    }
}

s16 ReasonResult(const Tcp& t)
{
    switch (t.reason)
    {
    case None:
        return 0;
    case Reset:
        return ErrorReset;
    case TimedOut:
        return ErrorTimeout;
    default:
        return ErrorClosing;
    }
}

// 0x4110 (0xFA8C): an active open; the handle, or an error
s16 Open(u32 in)
{
    const u32 remote = R32(in);
    const u16 remotePort = R16(in + 4);
    const u16 localPort = R16(in + 6);
    if (R32(in + 0x10) != 0)
    {
        Fatal("AVE-TCP: an open with an ASR (%08x) isn't done", R32(in + 0x10));
    }
    auto free = std::find_if(g_tcp.begin(), g_tcp.end(), [](const Tcp& t) { return !t.used; });
    if (free == g_tcp.end())
    {
        return ErrorNoHandle;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return ErrorNoHandle;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    if (localPort != 0)
    {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = localPort;
        if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
        {
            close(fd);
            return ErrorInUse;
        }
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = remote;
    to.sin_port = remotePort;
    Tcp& t = *free;
    t = Tcp{};
    t.used = true;
    t.fd = fd;
    t.remote = remote;
    t.remotePort = remotePort;
    t.localPort = localPort;
    t.state = SynSent;
    Log("AVE-TCP: connecting to %s (handle %d)", AddressText(remote, remotePort).c_str(),
        static_cast<int>(free - g_tcp.begin()));
    if (connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof(to)) != 0 && errno != EINPROGRESS)
    {
        const int error = errno;
        Log("AVE-TCP: connecting to %s failed: %s", AddressText(remote, remotePort).c_str(), strerror(error));
        Drop(t);
        return error == ECONNREFUSED ? ErrorClosing : ErrorTimeout;
    }
    return static_cast<s16>(free - g_tcp.begin());
}

// 0x4117 (0x101F4)
s16 Status(u32 in, u32 out)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    Update(*t);
    int waiting = 0, queued = 0, room = 0;
    socklen_t size = sizeof(int);
    if (t->fd >= 0)
    {
        ioctl(t->fd, FIONREAD, &waiting);
        getsockopt(t->fd, SOL_SOCKET, SO_NWRITE, &queued, &size);
        size = sizeof(int);
        getsockopt(t->fd, SOL_SOCKET, SO_SNDBUF, &room, &size);
    }
    W16(out, static_cast<u16>(t->state));
    W16(out + 2, 0);
    W16(out + 4, static_cast<u16>(std::clamp(room - queued, 0, 0x7FFF)));
    W16(out + 6, static_cast<u16>(std::clamp(waiting, 0, 0x7FFF)));
    return ReasonResult(*t);
}

// 0x4118 (0x102E4): the pieces sent; waits until the Mac's socket took them all
s16 Send(u32 in)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    if (R32(in + 4) != 0)
    {
        Fatal("AVE-TCP: a send with an ASR (%08x) isn't done", R32(in + 4));
    }
    const u8 pieces = *IopMemory(in + 9);
    if (pieces > 4)
    {
        return -12;
    }
    s32 total = 0;
    for (u32 k = 0; k < pieces; k++)
    {
        const u16 length = R16(in + 0xC + k * 8);
        const u8* data = IopMemory(R32(in + 0x10 + k * 8));
        for (u32 done = 0; done < length;)
        {
            const ssize_t n = send(t->fd, data + done, length - done, 0);
            if (n > 0)
            {
                done += static_cast<u32>(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                pollfd p{t->fd, POLLOUT, 0};
                poll(&p, 1, 1000);
                continue;
            }
            Log("AVE-TCP: sending to %s failed: %s", AddressText(t->remote, t->remotePort).c_str(), strerror(errno));
            t->state = Closed;
            t->reason = errno == ECONNRESET ? Reset : Other;
            return ErrorClosing;
        }
        total += length;
    }
    PS2_LOG(LogIop, "AVE-TCP: %d bytes sent to %s", total, AddressText(t->remote, t->remotePort).c_str());
    return static_cast<s16>(total);
}

// 0x4119 (0x1090C): what's waiting, up to the length. With no ASR the module waits until data comes (its count) or the
// connection goes down meanwhile (-8); after the peer's FIN with nothing left, -10 at once (0xDCD8). Out word 0 is a 16-bit
// flag of the module's receive (0x17EA4) the wrapper doesn't use: 0
s16 Receive(u32 in, u32 out)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    const u16 length = R16(in + 2);
    u8* buffer = IopMemory(R32(in + 4));
    if (R32(in + 8) != 0)
    {
        Fatal("AVE-TCP: a receive with an ASR (%08x) isn't done", R32(in + 8));
    }
    if (out != 0)
    {
        W16(out, 0);
    }
    bool waited = false;
    for (;;)
    {
        const ssize_t got = recv(t->fd, buffer, length, MSG_DONTWAIT);
        if (got > 0)
        {
            PS2_LOG(LogIop, "AVE-TCP: %zd bytes received from %s", got, AddressText(t->remote, t->remotePort).c_str());
            return static_cast<s16>(got);
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // The game's thread waits in the module's receive as it would on the console (the whole game waits here: the
            // runtime serves an RPC on the caller's thread)
            if (!waited)
            {
                Log("AVE-TCP: waiting for data from %s", AddressText(t->remote, t->remotePort).c_str());
                waited = true;
            }
            pollfd p{t->fd, POLLIN, 0};
            poll(&p, 1, -1);
            continue;
        }
        if (got < 0)
        {
            t->state = Closed;
            t->reason = errno == ETIMEDOUT ? TimedOut : errno == ECONNRESET ? Reset : Other;
            Log("AVE-TCP: the connection to %s went down: %s", AddressText(t->remote, t->remotePort).c_str(), strerror(errno));
            return ErrorNoData;
        }
        // The peer's FIN
        Update(*t);
        return waited ? ErrorNoData : ErrorReceiveDone;
    }
}

// 0x4108 (0xD058) sets a handle's option (0), 0x4109 (0xD204) gets it (the value's size at 4 at least 4: the size got, 4). The
// options: 8 keep-alive probes after this many idle seconds (0 none); 0x2001 1 to acknowledge each segment at once, 0 to
// delay acknowledgements; 0x2008 1 to send small segments at once (no Nagle). Sizes other than 4 (8: less than 4), values
// past 1 and other options are -13. Option 0x2001 is kept for the game to read back and not applied: the Mac's TCP has no
// switch for it per connection (its delayed acknowledgements are system-wide), and it changes only when a peer hears ACKs
// An option of the Mac's socket set; a failure logged (the connection goes on without it, as the game can't be told)
void SetSocketOption(const Tcp& t, int level, int name, int value, const char* what)
{
    if (setsockopt(t.fd, level, name, &value, sizeof(value)) != 0)
    {
        Log("AVE-TCP: %s on the connection to %s not set: %s", what, AddressText(t.remote, t.remotePort).c_str(),
            strerror(errno));
    }
}

s16 Option(u16 code, u32 in)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    const u16 option = R16(in + 2);
    const u16 size = R16(in + 4);
    const u32 value = R32(in + 8);
    u32* kept = option == 8 ? &t->keepAlive : option == 0x2001 ? &t->ackAtOnce : option == 0x2008 ? &t->noDelay : nullptr;
    if (kept == nullptr)
    {
        return ErrorParameter;
    }
    if (code == 0x4109)
    {
        if (size < 4)
        {
            return ErrorParameter;
        }
        W32(value, *kept);
        return 4;
    }
    const u32 set = R32(value);
    if ((option == 8 ? size < 4 : size != 4) || (option != 8 && set > 1))
    {
        return ErrorParameter;
    }
    // Option 8 is kept in half seconds in 16 bits, read back with the sign: the module's own wrap for large values
    *kept = option == 8 ? static_cast<u32>(static_cast<s16>(static_cast<u16>(set << 1)) >> 1) : set;
    if (option == 0x2008)
    {
        SetSocketOption(*t, IPPROTO_TCP, TCP_NODELAY, static_cast<int>(set), "TCP_NODELAY");
    }
    else if (option == 8)
    {
        // The module probes every min(idle, 75) seconds and gives up after about 8 unanswered (0xC088), as BSD's TCP does
        const int idle = static_cast<s32>(t->keepAlive);
        SetSocketOption(*t, SOL_SOCKET, SO_KEEPALIVE, idle > 0 ? 1 : 0, "SO_KEEPALIVE");
        if (idle > 0)
        {
            SetSocketOption(*t, IPPROTO_TCP, TCP_KEEPALIVE, idle, "TCP_KEEPALIVE");
            SetSocketOption(*t, IPPROTO_TCP, TCP_KEEPINTVL, std::min(idle, 75), "TCP_KEEPINTVL");
            SetSocketOption(*t, IPPROTO_TCP, TCP_KEEPCNT, 8, "TCP_KEEPCNT");
        }
    }
    return 0;
}

// 0x4116 (0x10130): the remote address and port, and the local port
s16 Info(u32 in, u32 out)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    getsockname(t->fd, reinterpret_cast<sockaddr*>(&local), &size);
    W32(out, t->remote);
    W16(out + 4, t->remotePort);
    W16(out + 6, local.sin_port);
    return 0;
}

// 0x4113 (0xD39C) closes (the remaining data sent, then FIN), 0x4114 (0xD634) shuts the sending down, 0x4115 (0xD704) aborts
// (RST), 0x411C (0xDFE4) deletes the handle
s16 Close(u16 code, u32 in)
{
    Tcp* t = Find(in);
    if (t == nullptr)
    {
        return ErrorHandle;
    }
    switch (code)
    {
    case 0x4113:
    case 0x411C:
        Log("AVE-TCP: the connection to %s closed", AddressText(t->remote, t->remotePort).c_str());
        Drop(*t);
        return 0;
    case 0x4114:
        shutdown(t->fd, SHUT_WR);
        t->sentFin = true;
        t->state = t->state == CloseWait ? LastAck : FinWait1;
        return 0;
    case 0x4115:
    {
        linger hard{1, 0};
        setsockopt(t->fd, SOL_SOCKET, SO_LINGER, &hard, sizeof(hard));
        Log("AVE-TCP: the connection to %s aborted", AddressText(t->remote, t->remotePort).c_str());
        Drop(*t);
        return 0;
    }
    default:
        return ErrorUnsupported;
    }
}
}

s16 SocketCall(u16 code, u32 in, u32 out)
{
    switch (code)
    {
    case 0x4108:
    case 0x4109:
        return Option(code, in);
    case 0x4110:
        return Open(in);
    case 0x4113:
    case 0x4114:
    case 0x4115:
    case 0x411C:
        return Close(code, in);
    case 0x4116:
        return Info(in, out);
    case 0x4117:
        return Status(in, out);
    case 0x4118:
        return Send(in);
    case 0x4119:
        return Receive(in, out);
    case 0x411A:
        // ASRs cancelled: the stand-in takes none
        return Find(in) != nullptr ? 0 : ErrorHandle;
    default:
        Fatal("AVE-TCP: socket function %04x (in %06x, out %06x) isn't done", code, in, out);
    }
}

void SocketsClosed()
{
    for (Tcp& t : g_tcp)
    {
        if (t.used)
        {
            Drop(t);
        }
    }
}

void SocketsPoll() {}

u32 RegisterUdpReceiver(UdpReceiver receiver)
{
    (void)receiver;
    Fatal("AVE-TCP: UDP receivers aren't done");
}
}
