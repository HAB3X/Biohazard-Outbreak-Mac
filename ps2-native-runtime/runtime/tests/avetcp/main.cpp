// AVE-TCP's stand-in against a local TCP server: ps2_avetcp_test. The API's blocks are laid out in the IOP's memory as the
// games' wrapper builds them (Biohazard Outbreak's AVEWRAPE.IRX), and each result checked against what the module returns:
// an open and its status, the options (0x4108/0x4109: the call Biohazard Outbreak makes on reaching obsrv.org's game
// server), sending and receiving, a receive that waits for data, the peer's FIN before and during a receive, an abort.
// Exits non-zero on any failure
#include "../../src/iop/avetcp.h"
#include "../../src/runtime.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

using namespace Ps2;

namespace
{
int g_failures = 0;

void Check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
    {
        g_failures++;
    }
}

// A listener on 127.0.0.1; each accepted connection is handed to the test's function on a thread
class Server
{
public:
    Server()
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(fd_, 4) != 0)
        {
            std::perror("test server");
            std::exit(2);
        }
        socklen_t size = sizeof(a);
        getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &size);
        port_ = a.sin_port;
    }
    ~Server() { close(fd_); }
    u16 Port() const { return port_; } // network order
    // The next connection, served by `serve` on its own thread
    std::thread Serve(std::function<void(int)> serve)
    {
        return std::thread([this, serve] {
            const int c = accept(fd_, nullptr, nullptr);
            serve(c);
            close(c);
        });
    }

private:
    int fd_ = -1;
    u16 port_ = 0;
};

// The blocks: one parameter block and one out block in the IOP's memory, as the wrapper's stack has them
u32 g_in = 0, g_out = 0, g_data = 0;

s16 Api(u16 code) { return AveTcp::ApiCall(code, g_in, g_out); }

void Clear() { std::memset(IopMemory(g_in), 0, 0x40); }

// 0x4110 as the wrapper's function 7 builds it: the address, the port, the local port (0: any)
s16 Open(u16 port)
{
    Clear();
    AveTcp::W32(g_in, htonl(INADDR_LOOPBACK));
    AveTcp::W16(g_in + 4, port);
    return Api(0x4110);
}

// 0x4117: the state and the bytes waiting
s16 Status(s16 handle, u16& state, u16& waiting)
{
    Clear();
    AveTcp::W16(g_in, static_cast<u16>(handle));
    const s16 r = Api(0x4117);
    state = AveTcp::R16(g_out);
    waiting = AveTcp::R16(g_out + 6);
    return r;
}

// Status until the state or bytes waiting say so (a second at most)
bool StatusUntil(s16 handle, const std::function<bool(u16, u16)>& done)
{
    for (int n = 0; n < 100; n++)
    {
        u16 state, waiting;
        Status(handle, state, waiting);
        if (done(state, waiting))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// 0x4108/0x4109 as the wrapper's functions 3 and 4 build them: the handle, the option, the size, the value's address
s16 Option(u16 code, s16 handle, u16 option, u16 size, u32 value)
{
    Clear();
    AveTcp::W32(g_data, value);
    AveTcp::W16(g_in, static_cast<u16>(handle));
    AveTcp::W16(g_in + 2, option);
    AveTcp::W16(g_in + 4, size);
    AveTcp::W32(g_in + 8, g_data);
    return Api(code);
}
u32 Value() { return AveTcp::R32(g_data); }

// 0x4118 with one piece
s16 Send(s16 handle, const std::string& text)
{
    Clear();
    std::memcpy(IopMemory(g_data), text.data(), text.size());
    AveTcp::W16(g_in, static_cast<u16>(handle));
    *IopMemory(g_in + 9) = 1;
    AveTcp::W16(g_in + 0xC, static_cast<u16>(text.size()));
    AveTcp::W32(g_in + 0x10, g_data);
    return Api(0x4118);
}

// 0x4119 with no ASR (the wrapper's function 19): what came, as text
s16 Receive(s16 handle, u16 length, std::string& text)
{
    Clear();
    AveTcp::W16(g_in, static_cast<u16>(handle));
    AveTcp::W16(g_in + 2, length);
    AveTcp::W32(g_in + 4, g_data);
    const s16 r = Api(0x4119);
    text = r > 0 ? std::string(reinterpret_cast<const char*>(IopMemory(g_data)), static_cast<size_t>(r)) : "";
    return r;
}

s16 HandleCall(u16 code, s16 handle)
{
    Clear();
    AveTcp::W16(g_in, static_cast<u16>(handle));
    return Api(code);
}

void OptionsTest(Server& server)
{
    std::thread peer = server.Serve([](int c) {
        char b;
        recv(c, &b, 1, 0);
    });
    const s16 h = Open(server.Port());
    Check(h >= 0, "open: a handle");
    Check(StatusUntil(h, [](u16 state, u16) { return state == 4; }), "open: established");
    // Biohazard Outbreak's call at the game server: option 0x2008 got, size 4, on handle 0
    Check(Option(0x4109, h, 0x2008, 4, 0x7FFF) == 4 && Value() == 0, "get 0x2008: 4, off by default");
    Check(Option(0x4108, h, 0x2008, 4, 1) == 0, "set 0x2008 on: 0");
    Check(Option(0x4109, h, 0x2008, 4, 0) == 4 && Value() == 1, "get 0x2008: on");
    Check(Option(0x4108, h, 0x2008, 4, 2) == AveTcp::ErrorParameter, "set 0x2008 to 2: -13");
    Check(Option(0x4108, h, 0x2008, 8, 1) == AveTcp::ErrorParameter, "set 0x2008 with size 8: -13");
    Check(Option(0x4109, h, 0x2008, 3, 0) == AveTcp::ErrorParameter, "get with size 3: -13");
    Check(Option(0x4109, h, 0x2001, 4, 0) == 4 && Value() == 1, "get 0x2001: 1 by default");
    Check(Option(0x4108, h, 0x2001, 4, 0) == 0 && Option(0x4109, h, 0x2001, 4, 9) == 4 && Value() == 0, "set 0x2001 off");
    Check(Option(0x4109, h, 8, 4, 9) == 4 && Value() == 0, "get 8: no keep-alive by default");
    Check(Option(0x4108, h, 8, 8, 30) == 0 && Option(0x4109, h, 8, 4, 0) == 4 && Value() == 30, "set 8 to 30 s (size 8)");
    Check(Option(0x4108, h, 8, 2, 30) == AveTcp::ErrorParameter, "set 8 with size 2: -13");
    Check(Option(0x4108, h, 0x1234, 4, 0) == AveTcp::ErrorParameter, "an unknown option: -13");
    Check(Option(0x4109, 9, 0x2008, 4, 0) == AveTcp::ErrorHandle, "a handle not open: -4");
    Check(Send(h, "x") == 1, "send one byte");
    peer.join();
    Check(HandleCall(0x4113, h) == 0, "close");
}

void DataTest(Server& server)
{
    std::thread peer = server.Serve([](int c) {
        char b[16];
        const ssize_t n = recv(c, b, sizeof(b), 0);
        send(c, b, static_cast<size_t>(n), 0);
        // A reply that comes later: the receive waits for it
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        send(c, "later", 5, 0);
        recv(c, b, 1, 0);
        // FIN: a receive after it gets -10
    });
    const s16 h = Open(server.Port());
    Check(StatusUntil(h, [](u16 state, u16) { return state == 4; }), "established");
    Check(Send(h, "hello") == 5, "send 5 bytes");
    Check(StatusUntil(h, [](u16, u16 waiting) { return waiting == 5; }), "status: 5 bytes waiting");
    std::string text;
    Check(Receive(h, 64, text) == 5 && text == "hello", "receive the echo");
    const auto start = std::chrono::steady_clock::now();
    Check(Receive(h, 64, text) == 5 && text == "later", "a receive with nothing there waits for data");
    Check(std::chrono::steady_clock::now() - start > std::chrono::milliseconds(200), "...it waited");
    Check(Send(h, "!") == 1, "send");
    Check(StatusUntil(h, [](u16 state, u16) { return state == 5; }), "the peer's FIN: CLOSE_WAIT");
    Check(Receive(h, 64, text) == AveTcp::ErrorReceiveDone, "a receive after the FIN: -10");
    peer.join();
    Check(HandleCall(0x4115, h) == 0, "abort");
    Check(HandleCall(0x4117, h) == AveTcp::ErrorHandle, "the handle is gone");
}

void ClosedWhileWaiting(Server& server)
{
    std::thread peer = server.Serve([](int) { std::this_thread::sleep_for(std::chrono::milliseconds(300)); });
    const s16 h = Open(server.Port());
    Check(StatusUntil(h, [](u16 state, u16) { return state == 4; }), "established");
    std::string text;
    Check(Receive(h, 64, text) == AveTcp::ErrorNoData, "the peer closes while a receive waits: -8");
    peer.join();
    Check(HandleCall(0x4113, h) == 0, "close");
}
}

int main()
{
    IopInit();
    g_in = IopAlloc(0x40);
    g_out = IopAlloc(0x40);
    g_data = IopAlloc(0x400);
    const u32 parameters = IopAlloc(0x40);
    std::memset(IopMemory(parameters), 0, 0x40);
    AveTcp::Init(parameters);
    Server server;
    OptionsTest(server);
    DataTest(server);
    ClosedWhileWaiting(server);
    AveTcp::Dispose();
    std::printf(g_failures == 0 ? "all passed\n" : "%d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
