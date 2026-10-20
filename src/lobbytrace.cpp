// The lobby's and game server's messages in the log. Past the login page the game speaks Capcom's protocol with obsrv.org's
// lobby (port 8200) and game server (8590) over TCP: each message a 12-byte header (who sent it, query or answer, a 16-bit
// command, the payload's 16-bit length, a 16-bit message id, an error byte, three bytes the server sends as FF), all big
// endian, then the payload. What the game sends and gets is logged as headers only (no payload: it has the session's id at
// login, and the players' chat), one line each, so a player's log says where a lobby step stopped. The connections to port
// 443 (HTTPS: DNAS, the browser) aren't this protocol and aren't traced.
#include "lobbytrace.h"

#include "ps2/iop.h"

#include <arpa/inet.h>

#include <map>
#include <mutex>
#include <vector>

namespace LobbyTrace
{
namespace
{
constexpr u32 HeaderBytes = 12;
constexpr u16 HttpsPort = 443;

// What came on one direction of a connection and isn't a whole message yet
struct Stream
{
    std::vector<u8> pending;
    bool lost = false; // a header made no sense: the stream isn't this protocol, or bytes were missed
};

struct Connection
{
    u16 port = 0;
    Stream sent, received;
};

std::mutex g_lock;
std::map<s32, Connection> g_connections;

void Take(Connection& c, Stream& s, const char* way, const u8* data, u32 size)
{
    if (s.lost)
    {
        return;
    }
    s.pending.insert(s.pending.end(), data, data + size);
    while (s.pending.size() >= HeaderBytes)
    {
        const u8* h = s.pending.data();
        const u32 length = (h[4] << 8) | h[5];
        if (length > 0x10000 - HeaderBytes)
        {
            Ps2::Log("lobby (port %u): %s bytes that aren't a message header (%02x %02x %02x %02x ...): not traced further",
                     c.port, way, h[0], h[1], h[2], h[3]);
            s.lost = true;
            s.pending.clear();
            return;
        }
        if (s.pending.size() < HeaderBytes + length)
        {
            return;
        }
        Ps2::Log("lobby (port %u): %-4s %04x  from %02x, kind %02x, %u bytes, id %u%s", c.port, way, (h[2] << 8) | h[3], h[0], h[1],
                 length, (h[6] << 8) | h[7], h[8] != 0 ? ", error set" : "");
        s.pending.erase(s.pending.begin(), s.pending.begin() + HeaderBytes + length);
    }
}
}

void Opened(s32 handle, u16 remotePort)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (handle < 0)
    {
        return;
    }
    const u16 port = ntohs(remotePort);
    if (port == HttpsPort)
    {
        g_connections.erase(handle);
        return;
    }
    g_connections[handle] = Connection{port, {}, {}};
}

void Sent(s32 handle, const u8* data, u32 size)
{
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_connections.find(handle);
    if (it != g_connections.end())
    {
        Take(it->second, it->second.sent, "sent", data, size);
    }
}

void Received(s32 handle, const u8* data, u32 size)
{
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_connections.find(handle);
    if (it != g_connections.end())
    {
        Take(it->second, it->second.received, "got", data, size);
    }
}

void Closed(s32 handle)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_connections.erase(handle);
}
}
