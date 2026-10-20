// AVE-TCP's resolver ("ntDns", AVETCP.IRX's exports 5-11, its API 0x4181-0x4187 at 0x14BEC) on the Mac (avetcp.h). The
// module asks the DNS servers it was started with (0x4181: a domain name's address, the first and second servers' addresses)
// for a host's A records over UDP, one ticket at a time (its ticket_max is 1, export 5); here the same query goes to the same
// servers from the Mac, on a thread of its own, so the game's servers (a revival's own DNS, say) answer as they would the
// console. With no server given, the Mac's own resolver answers.
//
// The blocks: GetTicket (0x4183, export 7) the host name's address at 0 (a work area's at 4, the module's own use): the
// ticket, or -1. LookUp (0x4185, export 9) the ticket at 0, where the addresses go at 4, how many at 8 (16 bits): the
// count found, -2 while the answer is awaited, -1 for none or an error, -6 when no server answered. ReleaseTicket (0x4184,
// export 8) the ticket at 0.
#include "avetcp.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Ps2::AveTcp
{
namespace
{
constexpr s16 Pending = -2;
constexpr s16 TimedOut = -6;
// Each server is asked this many times, waiting this long for each answer
constexpr int Tries = 2;
constexpr int AnswerMilliseconds = 3000;

struct Ticket
{
    std::string host;
    bool done = false;
    s16 result = Pending;          // the count, or -1, -6
    std::vector<u32> addresses;    // network order
};

struct Resolver
{
    u32 servers[2] = {};
    std::shared_ptr<Ticket> ticket; // the one ticket (ticket_max 1)
};
Resolver g_dns;

// A DNS query for host's A records (RFC 1035): the ID, recursion wanted, one question
std::vector<u8> Query(const std::string& host, u16 id)
{
    std::vector<u8> q = {static_cast<u8>(id >> 8), static_cast<u8>(id), 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    size_t start = 0;
    while (start <= host.size())
    {
        size_t dot = host.find('.', start);
        if (dot == std::string::npos)
        {
            dot = host.size();
        }
        const std::string label = host.substr(start, dot - start);
        if (label.empty() || label.size() > 63)
        {
            break;
        }
        q.push_back(static_cast<u8>(label.size()));
        q.insert(q.end(), label.begin(), label.end());
        start = dot + 1;
    }
    q.insert(q.end(), {0, 0, 1, 0, 1});
    return q;
}

// A name in a DNS message skipped (labels, or a pointer): where it ends, or 0 past the message
size_t SkipName(const std::vector<u8>& m, size_t at)
{
    while (at < m.size())
    {
        const u8 n = m[at];
        if ((n & 0xC0) == 0xC0)
        {
            return at + 2;
        }
        if (n == 0)
        {
            return at + 1;
        }
        at += 1 + n;
    }
    return 0;
}

// The answer's A records (class IN), as the module takes them (export 9: type 1, class 1, the 4 bytes as they are)
bool Answers(const std::vector<u8>& m, u16 id, std::vector<u32>& addresses, bool& refused)
{
    if (m.size() < 12 || ((m[0] << 8) | m[1]) != id || (m[2] & 0x80) == 0)
    {
        return false;
    }
    refused = (m[3] & 0x0F) != 0;
    const u16 questions = static_cast<u16>((m[4] << 8) | m[5]);
    const u16 answers = static_cast<u16>((m[6] << 8) | m[7]);
    size_t at = 12;
    for (u16 n = 0; n < questions && at != 0; n++)
    {
        at = SkipName(m, at);
        at = at != 0 ? at + 4 : 0;
    }
    for (u16 n = 0; n < answers && at != 0 && at + 10 <= m.size(); n++)
    {
        at = SkipName(m, at);
        if (at == 0 || at + 10 > m.size())
        {
            break;
        }
        const u16 type = static_cast<u16>((m[at] << 8) | m[at + 1]);
        const u16 cls = static_cast<u16>((m[at + 2] << 8) | m[at + 3]);
        const u16 length = static_cast<u16>((m[at + 8] << 8) | m[at + 9]);
        at += 10;
        if (at + length > m.size())
        {
            break;
        }
        if (type == 1 && cls == 1 && length == 4)
        {
            u32 a;
            std::memcpy(&a, &m[at], 4);
            addresses.push_back(a);
        }
        at += length;
    }
    return true;
}

// The query sent to each server in turn, as the module does (its LookUp's "next server")
void Resolve(std::shared_ptr<Ticket> ticket, std::vector<u32> servers)
{
    std::vector<u32> addresses;
    s16 result = TimedOut;
    if (servers.empty())
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        addrinfo* list = nullptr;
        result = -1;
        if (getaddrinfo(ticket->host.c_str(), nullptr, &hints, &list) == 0)
        {
            for (addrinfo* a = list; a != nullptr; a = a->ai_next)
            {
                addresses.push_back(reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr.s_addr);
            }
            freeaddrinfo(list);
        }
    }
    const int fd = servers.empty() ? -1 : socket(AF_INET, SOCK_DGRAM, 0);
    for (int attempt = 0; fd >= 0 && attempt < Tries * static_cast<int>(servers.size()) && result == TimedOut; attempt++)
    {
        const u32 server = servers[attempt % servers.size()];
        const u16 id = static_cast<u16>(arc4random());
        const std::vector<u8> query = Query(ticket->host, id);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(53);
        to.sin_addr.s_addr = server;
        sendto(fd, query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        pollfd p{fd, POLLIN, 0};
        while (poll(&p, 1, AnswerMilliseconds) > 0)
        {
            std::vector<u8> answer(1500);
            const ssize_t got = recv(fd, answer.data(), answer.size(), 0);
            if (got <= 0)
            {
                break;
            }
            answer.resize(static_cast<size_t>(got));
            bool refused = false;
            if (Answers(answer, id, addresses, refused))
            {
                result = refused ? -1 : 0;
                break;
            }
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (!addresses.empty())
    {
        result = static_cast<s16>(addresses.size());
    }
    else if (result == 0)
    {
        result = -1;
    }
    Lock lock(StackLock());
    ticket->addresses = std::move(addresses);
    ticket->result = result;
    ticket->done = true;
    if (result > 0)
    {
        in_addr first{ticket->addresses[0]};
        Log("AVE-TCP: %s is %s", ticket->host.c_str(), inet_ntoa(first));
    }
    else
    {
        Log("AVE-TCP: %s wasn't found (%d)", ticket->host.c_str(), result);
    }
}
}

s16 DnsCall(u16 code, u32 in, u32 out)
{
    (void)out;
    switch (code)
    {
    case 0x4181: // export 5: the domain name (unused here), the servers
    {
        g_dns.servers[0] = R32(in + 4);
        g_dns.servers[1] = R32(in + 8);
        in_addr a{g_dns.servers[0]}, b{g_dns.servers[1]};
        std::string first = inet_ntoa(a);
        Log("AVE-TCP: the DNS servers are %s and %s", first.c_str(), inet_ntoa(b));
        return 0;
    }
    case 0x4182: // export 6
        DnsReset();
        return 0;
    case 0x4183: // export 7
    {
        if (g_dns.ticket != nullptr)
        {
            return -1;
        }
        auto ticket = std::make_shared<Ticket>();
        ticket->host = reinterpret_cast<const char*>(IopMemory(R32(in)));
        std::vector<u32> servers;
        for (u32 s : g_dns.servers)
        {
            if (s != 0)
            {
                servers.push_back(s);
            }
        }
        g_dns.ticket = ticket;
        Log("AVE-TCP: looking up %s", ticket->host.c_str());
        std::thread(Resolve, ticket, servers).detach();
        return 0;
    }
    case 0x4184: // export 8
        if (static_cast<s16>(R16(in)) != 0 || g_dns.ticket == nullptr)
        {
            return -1;
        }
        g_dns.ticket = nullptr;
        return 0;
    case 0x4185: // export 9
    {
        if (static_cast<s16>(R16(in)) != 0 || g_dns.ticket == nullptr)
        {
            return -1;
        }
        const Ticket& t = *g_dns.ticket;
        if (!t.done)
        {
            return Pending;
        }
        if (t.result < 0)
        {
            return t.result;
        }
        const u16 room = R16(in + 8);
        const u32 to = R32(in + 4);
        u16 n = 0;
        for (; n < room && n < t.addresses.size(); n++)
        {
            W32(to + n * 4, t.addresses[n]);
        }
        return static_cast<s16>(n);
    }
    default:
        Fatal("AVE-TCP: resolver function %04x isn't done", code);
    }
}

void DnsReset()
{
    g_dns = Resolver{};
}
}
