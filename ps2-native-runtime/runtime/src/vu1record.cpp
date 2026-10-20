// VIF1's and VU1's side recorded and played back (--control's vu1record, --vu1-replay): with VIF1 and VU1 on a thread of their
// own (render.cpp), a stretch of what the VU1 thread was given (VIF1's data, the EE's VU1 starts, in order) with VU1's and VIF1's
// state at its start, played back later through VIF1 and VU1 alone. What they hand the GIF (VIF1's DIRECT data, VU1's XGKICK
// packets) is hashed instead of drawn: two builds give the same hash when VIF1 and VU1 made the same data, and their time is
// measured without the game's or the GS's (the 3D's VU1 programs vary from frame to frame with what's on screen, so a live
// scene's time doesn't).
//
// The file: "PS2VU1RC", a version, the state (Vu::Serialize, Vif::Serialize) with its size, then records of a 12 byte header
// (kind: 1 VIF1's data, 2 a VU1 start; value: the start's address; words) and their words.
#include "runtime.h"

#include "../../hw/ee/gif.h"
#include "../../hw/ee/vif.h"
#include "../../hw/ee/vu.h"
#include "../../hw/state.h"

#define XXH_INLINE_ALL
#include "../../hw/hw/xxhash.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace Ps2
{
namespace
{
constexpr char Magic[8] = {'P', 'S', '2', 'V', 'U', '1', 'R', 'C'};
constexpr u32 Version = 1;

struct Header
{
    u32 kind;
    u32 value;
    u32 words;
};
static_assert(sizeof(Header) == 12);

// The recording asked for (by --control, on its thread) and the one being made (on the VU1 thread)
std::mutex g_requestLock;
std::string g_requestFile;
double g_requestSeconds = 0;
FILE* g_file = nullptr;
std::string g_fileName;
std::chrono::steady_clock::time_point g_until;

void Stop(const char* why)
{
    fclose(g_file);
    g_file = nullptr;
    Log("vu1 record: %s %s", g_fileName.c_str(), why);
}

// What VIF1 and VU1 hand the GIF, hashed in order (XXH3: each record's path, or a kick's mark, then its quadwords)
class HashSink final : public Ee::Gif::Forward
{
public:
    HashSink() { XXH3_64bits_reset(&state_); }

    void Transfer(u32 path, const u8* data, u32 quadwords) override
    {
        XXH3_64bits_update(&state_, &path, sizeof(path));
        XXH3_64bits_update(&state_, data, static_cast<size_t>(quadwords) * 16);
        transfers++;
    }

    void Kick(const u8* vuMemory, u32 address, u32 quadwords, u32 source) override
    {
        (void)source;
        constexpr u32 Ring = 0x400;
        const u32 mark = 0xFFFFFFFFu;
        u32 at = address & (Ring - 1);
        u32 first = std::min(quadwords, Ring - at);
        XXH3_64bits_update(&state_, &mark, sizeof(mark));
        XXH3_64bits_update(&state_, vuMemory + at * 16, static_cast<size_t>(first) * 16);
        XXH3_64bits_update(&state_, vuMemory, static_cast<size_t>(quadwords - first) * 16);
        kicks++;
    }

    u64 Hash() { return XXH3_64bits_digest(&state_); }

    u64 transfers = 0;
    u64 kicks = 0;

private:
    XXH3_state_t state_;
};

double ThreadCpuSeconds()
{
    timespec t = {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}
}

void Vu1LiveRecord(const std::string& file, double seconds)
{
    if (!Vu1Threaded())
    {
        Log("vu1 record: only with VIF1 and VU1 on a thread of their own (Ps2GameInfo::vu1Thread)");
        return;
    }
    std::lock_guard<std::mutex> lock(g_requestLock);
    g_requestFile = file;
    g_requestSeconds = seconds;
}

void Vu1RecordChunk(Ee::Vu& vu1, Ee::Vif& vif1)
{
    if (g_file != nullptr && std::chrono::steady_clock::now() >= g_until)
    {
        Stop("written");
    }
    std::lock_guard<std::mutex> lock(g_requestLock);
    if (g_requestFile.empty() || g_file != nullptr)
    {
        return;
    }
    g_fileName = g_requestFile;
    g_requestFile.clear();
    g_file = fopen(g_fileName.c_str(), "wb");
    if (g_file == nullptr)
    {
        Log("vu1 record: can't write %s", g_fileName.c_str());
        return;
    }
    std::vector<u8> state;
    NativeGraphics::StateWriter writer(state);
    NativeGraphics::Saver saver{writer};
    vu1.Serialize(saver);
    vif1.Serialize(saver);
    u64 size = state.size();
    fwrite(Magic, sizeof(Magic), 1, g_file);
    fwrite(&Version, sizeof(Version), 1, g_file);
    fwrite(&size, sizeof(size), 1, g_file);
    fwrite(state.data(), 1, state.size(), g_file);
    g_until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                      std::chrono::duration<double>(g_requestSeconds));
    Log("vu1 record: %s for %.1f s", g_fileName.c_str(), g_requestSeconds);
}

void Vu1RecordData(u32 kind, u32 value, const u32* words, u32 count)
{
    if (g_file == nullptr)
    {
        return;
    }
    Header header = {kind, value, count};
    if (fwrite(&header, sizeof(header), 1, g_file) != 1 || (count != 0 && fwrite(words, 4, count, g_file) != count))
    {
        Stop("couldn't be written: the recording stops");
    }
}

bool Vu1Replay(Ee::Vu& vu1, Ee::Vif& vif1, Ee::Gif& gif, const std::vector<u8>& file, u32 pass)
{
    if (file.size() < sizeof(Magic) + 12 || std::memcmp(file.data(), Magic, sizeof(Magic)) != 0)
    {
        Log("vu1 replay: %s isn't a recording", g_options.vu1ReplayFile.c_str());
        return false;
    }
    u32 version = 0;
    u64 size = 0;
    std::memcpy(&version, file.data() + 8, 4);
    std::memcpy(&size, file.data() + 12, 8);
    if (version != Version || size > file.size() - 20)
    {
        Log("vu1 replay: %s is version %u (this build reads %u) or cut short", g_options.vu1ReplayFile.c_str(), version, Version);
        return false;
    }
    NativeGraphics::StateReader reader(file.data() + 20, size);
    NativeGraphics::Loader loader{reader};
    vu1.Serialize(loader);
    vif1.Serialize(loader);
    if (reader.failed())
    {
        Log("vu1 replay: %s's state is cut short", g_options.vu1ReplayFile.c_str());
        return false;
    }
    HashSink sink;
    gif.SetForward(&sink);
    u64 records = 0;
    auto start = std::chrono::steady_clock::now();
    const double cpuStart = ThreadCpuSeconds();
    for (size_t at = 20 + size; at + sizeof(Header) <= file.size();)
    {
        Header header;
        std::memcpy(&header, file.data() + at, sizeof(header));
        at += sizeof(header);
        if (at + static_cast<size_t>(header.words) * 4 > file.size())
        {
            Log("vu1 replay: %s is cut short", g_options.vu1ReplayFile.c_str());
            break;
        }
        std::vector<u32> words(header.words);
        std::memcpy(words.data(), file.data() + at, words.size() * 4);
        at += words.size() * 4;
        if (header.kind == 1)
        {
            vif1.Process(words.data(), header.words);
        }
        else if (header.kind == 2)
        {
            vu1.Run(header.value);
        }
        else
        {
            Log("vu1 replay: an unknown record (%u) in %s", header.kind, g_options.vu1ReplayFile.c_str());
            break;
        }
        records++;
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    gif.SetForward(nullptr);
    Log("vu1 replay: pass %u: %llu records in %.3f s, %.3f s of the thread's CPU; GIF data hash %016llx (%llu kicks, %llu "
        "transfers)",
        pass + 1, static_cast<unsigned long long>(records), seconds, ThreadCpuSeconds() - cpuStart,
        static_cast<unsigned long long>(sink.Hash()), static_cast<unsigned long long>(sink.kicks),
        static_cast<unsigned long long>(sink.transfers));
    return true;
}
}
