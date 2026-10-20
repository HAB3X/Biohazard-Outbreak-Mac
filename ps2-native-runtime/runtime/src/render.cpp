// The render thread: the GS's side of the hardware (the GIF's three paths, the GS's registers, local memory and drawing, the
// PCRTC's picture) runs on a thread of its own while the game's thread runs the game, VIF1 and VU1. What reaches the GIF (VU1's
// XGKICK packets, VIF1's DIRECT data, the GIF channel's chains) is copied into a queue the render thread plays back in the same
// order, so the GS gets exactly what it got before; everything else that touches the GS's side (the privileged registers, the
// vblank's picture) is queued as work in its place among the data. The GS's interrupts (SIGNAL, FINISH) come from the render
// thread when it gets there, as the PS2's GS raises them when it's done.
//
// The queue is handed over in chunks: when one fills, at the end of each DMA transfer, and whenever the game reads CSR (it may
// be waiting for a FINISH or SIGNAL in data not handed over yet). The game's thread stays at most two pictures ahead.
//
// A game can have VIF1 and VU1 on a thread of their own too (Ps2GameInfo::vu1Thread): a queue in front of the render thread's,
// in the same form, takes VIF1's data and VU1's starts by the EE besides everything the render thread's took; the VU1 thread
// plays it in order, running VIF1 and VU1 and putting what they give the GIF, with the rest, into the render thread's queue.
// So the GS gets the same data in the same order, and the EE goes on with the game meanwhile, as the PS2's EE does while VIF1
// and VU1 work. The EE's reads and writes of VIF1's registers and VU1's memory wait until the VU1 thread has done everything
// before them (Vu1Sync). A VIF1 code's interrupt bit can't be had this way (the DMA has already ended for the EE): it stops the
// program.
//
// From the Crash Twinsanity native port's graphics/renderthread.cpp (same author). $PS2_RENDER_THREAD=0 keeps the GS on the
// game's thread, $PS2_VU1_THREAD=0 VIF1 and VU1.
#include "runtime.h"

#include "ps2/runtime.h"

#include "../../hw/ee/gif.h"
#include "../../hw/ee/vif.h"
#include "../../hw/ee/vu.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace Ps2
{
extern const Ps2GameInfo* g_game;

namespace
{
enum RecordKind : u32
{
    RecordTransfer,
    RecordKick,
    RecordCall,
    // The VU1 thread's: VIF1's words, and a VU1 program started by the EE (CMSAR1)
    RecordVif1,
    RecordVu1Start,
};

struct Header
{
    u32 kind;
    u32 value;     // Transfer: the path; Kick: the kicking code's address; Call: the work's index; Vu1Start: the address
    u32 quadwords; // the data's room after the header
    u32 words;     // Vif1: its words (the room is that rounded up to quadwords)
};
static_assert(sizeof(Header) == 16);

struct Chunk
{
    u8* bytes = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    std::vector<std::function<void()>> calls;

    u8* Grow(size_t more)
    {
        if (size + more > capacity)
        {
            capacity = std::max(capacity * 2, size + more);
            bytes = static_cast<u8*>(std::realloc(bytes, capacity));
        }
        u8* at = bytes + size;
        size += more;
        return at;
    }
};

constexpr size_t ChunkBytes = 512 * 1024;
constexpr size_t KeptChunkBytes = 8 * 1024 * 1024;

// Records in chunks, from the threads queueing work (one at a time) to the one thread playing them
class Queue
{
public:
    u8* Record(u32 kind, u32 value, u32 quadwords, u32 words = 0)
    {
        size_t bytes = sizeof(Header) + static_cast<size_t>(quadwords) * 16;
        if (filling_ != nullptr && filling_->size != 0 && filling_->size + bytes > ChunkBytes)
        {
            Flush();
        }
        if (filling_ == nullptr)
        {
            filling_ = Take();
        }
        u8* at = filling_->Grow(bytes);
        Header header = {kind, value, quadwords, words};
        std::memcpy(at, &header, sizeof(header));
        return at + sizeof(Header);
    }

    // Work in its place (not handed over yet: Call hands it over at once)
    void Record(std::function<void()> work)
    {
        Record(RecordCall, 0, 0);
        Header* header = reinterpret_cast<Header*>(filling_->bytes + filling_->size - sizeof(Header));
        header->value = static_cast<u32>(filling_->calls.size());
        filling_->calls.push_back(std::move(work));
    }

    void Call(std::function<void()> work)
    {
        Record(std::move(work));
        Flush();
    }

    void Flush()
    {
        if (filling_ == nullptr || filling_->size == 0)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(lock_);
            ready_.push_back(filling_);
            handed_++;
        }
        filling_ = nullptr;
        work_.notify_one();
    }

    // Everything queued played
    void Sync()
    {
        Flush();
        std::unique_lock<std::mutex> lock(lock_);
        done_.wait(lock, [this] { return played_ == handed_; });
    }

    // The player's: the next chunk handed over (waiting for one), and a chunk played
    Chunk* Next()
    {
        std::unique_lock<std::mutex> lock(lock_);
        work_.wait(lock, [this] { return !ready_.empty(); });
        Chunk* chunk = ready_.front();
        ready_.pop_front();
        return chunk;
    }

    void Played(Chunk* chunk)
    {
        chunk->calls.clear();
        chunk->size = 0;
        if (chunk->capacity > KeptChunkBytes)
        {
            std::free(chunk->bytes);
            chunk->bytes = nullptr;
            chunk->capacity = 0;
        }
        {
            std::lock_guard<std::mutex> lock(lock_);
            spare_.push_back(chunk);
            played_++;
        }
        done_.notify_all();
    }

private:
    Chunk* Take()
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!spare_.empty())
        {
            Chunk* chunk = spare_.back();
            spare_.pop_back();
            return chunk;
        }
        Chunk* chunk = new Chunk();
        chunk->Grow(ChunkBytes);
        chunk->size = 0;
        return chunk;
    }

    std::mutex lock_;
    std::condition_variable work_;
    std::condition_variable done_;
    std::deque<Chunk*> ready_;
    std::vector<Chunk*> spare_;
    u64 handed_ = 0;
    u64 played_ = 0;
    Chunk* filling_ = nullptr;
};

// Each record of a chunk: its header and data
template <typename Visit>
void ForEachRecord(const Chunk& chunk, Visit visit)
{
    const u8* at = chunk.bytes;
    const u8* end = chunk.bytes + chunk.size;
    while (at < end)
    {
        Header header;
        std::memcpy(&header, at, sizeof(header));
        at += sizeof(Header);
        visit(header, at);
        at += static_cast<size_t>(header.quadwords) * 16;
    }
}

thread_local bool t_onVu1Thread = false;

class RenderThread final : public Ee::Gif::Forward
{
public:
    void Start(bool vu1Thread)
    {
        if (vu1Thread)
        {
            vu1_ = std::make_unique<Queue>();
            vu1Thread_ = std::thread([this] { RunVu1(); });
        }
        thread_ = std::thread([this] { Run(); });
        Hw().gif->SetForward(this);
    }

    bool Vu1Threaded() const { return vu1_ != nullptr; }

    void Transfer(u32 path, const u8* data, u32 quadwords) override
    {
        if (quadwords != 0)
        {
            std::memcpy(In().Record(RecordTransfer, path, quadwords), data, static_cast<size_t>(quadwords) * 16);
        }
    }

    void Kick(const u8* vuMemory, u32 address, u32 quadwords, u32 source) override
    {
        // VU1's 16 KB ring from the address, wrapping at its end
        constexpr u32 Ring = 0x400;
        u8* to = In().Record(RecordKick, source, quadwords);
        u32 at = address & (Ring - 1);
        u32 first = std::min(quadwords, Ring - at);
        std::memcpy(to, vuMemory + at * 16, static_cast<size_t>(first) * 16);
        std::memcpy(to + first * 16, vuMemory, static_cast<size_t>(quadwords - first) * 16);
    }

    void Vif1(const u32* words, u32 count)
    {
        std::memcpy(vu1_->Record(RecordVif1, 0, (count + 3) / 4, count), words, static_cast<size_t>(count) * 4);
    }

    void Vu1Start(u32 address) { vu1_->Record(RecordVu1Start, address, 0); }

    void Call(std::function<void()> work) { In().Call(std::move(work)); }

    void Flush() { In().Flush(); }

    // Everything queued done: the VU1 thread's (which hands the render thread what it made of it), then the render thread's
    void Sync()
    {
        if (vu1_ != nullptr)
        {
            vu1_->Sync();
        }
        gs_.Sync();
    }

    void Vu1Sync() { vu1_->Sync(); }

    // A picture queued (its number)
    u64 QueueFrame(std::function<void()> show)
    {
        u64 frame;
        {
            // Read by a wait for the render thread on another thread (the timer thread queues while the game's waits)
            std::lock_guard<std::mutex> lock(framesLock_);
            frame = ++framesQueued_;
        }
        Call([this, frame, show = std::move(show)] {
            show();
            {
                std::lock_guard<std::mutex> lock(framesLock_);
                framesMade_ = frame;
            }
            framesDone_.notify_all();
        });
        return frame;
    }

    // The pictures queued and not made yet
    u64 Behind()
    {
        std::lock_guard<std::mutex> lock(framesLock_);
        return framesQueued_ - framesMade_;
    }

    void WaitBehind(u64 behind)
    {
        std::unique_lock<std::mutex> lock(framesLock_);
        framesDone_.wait(lock, [this, behind] { return framesMade_ + behind >= framesQueued_; });
    }

private:
    // Where work queued on this thread goes: the VU1 thread's queue, unless this is the VU1 thread (or there's none)
    Queue& In() { return vu1_ != nullptr && !t_onVu1Thread ? *vu1_ : gs_; }

    void Run()
    {
        ThreadOnFrameDeadline();
        ThreadCounted(ThreadRole::Render);
        DisplayRenderThreadStart();
        Ee::Gif& gif = *Hw().gif;
        for (;;)
        {
            Chunk* chunk = gs_.Next();
            ForEachRecord(*chunk, [&](const Header& header, const u8* data) {
                switch (header.kind)
                {
                case RecordTransfer:
                    GsRecordData(header.value, data, header.quadwords, false);
                    gif.TransferNow(header.value, data, header.quadwords);
                    break;
                case RecordKick:
                    GsRecordData(header.value, data, header.quadwords, true);
                    gif.KickRow(data, header.quadwords, header.value);
                    break;
                case RecordCall:
                    chunk->calls[header.value]();
                    break;
                default:
                    Fatal("render thread: a record of kind %u", header.kind);
                }
            });
            gs_.Played(chunk);
        }
    }

    // VIF1 and VU1 run, their GIF data (through the GIF's forward, this) and everything else going on into the render thread's
    // queue, handed over at each chunk's end
    void RunVu1()
    {
        ThreadOnFrameDeadline();
        ThreadCounted(ThreadRole::Vu1);
        t_onVu1Thread = true;
        Ee::Vif& vif1 = *Hw().vif1;
        for (;;)
        {
            Chunk* chunk = vu1_->Next();
            Vu1RecordChunk(*Hw().vu1, vif1);
            ForEachRecord(*chunk, [&](const Header& header, const u8* data) {
                switch (header.kind)
                {
                case RecordVif1:
                {
                    Vu1RecordData(1, 0, reinterpret_cast<const u32*>(data), header.words);
                    u32 taken = vif1.Process(reinterpret_cast<const u32*>(data), header.words);
                    if (taken < header.words || vif1.stalled())
                    {
                        Fatal("VIF1 stopped at a code's interrupt bit on the VU1 thread: this game can't have VIF1 and VU1 on "
                              "a thread of their own (Ps2GameInfo::vu1Thread)");
                    }
                    break;
                }
                case RecordVu1Start:
                    Vu1RecordData(2, header.value, nullptr, 0);
                    Hw().vu1->Run(header.value);
                    break;
                case RecordTransfer:
                case RecordKick:
                    std::memcpy(gs_.Record(header.kind, header.value, header.quadwords), data,
                                static_cast<size_t>(header.quadwords) * 16);
                    break;
                case RecordCall:
                    gs_.Record(std::move(chunk->calls[header.value]));
                    break;
                default:
                    Fatal("VU1 thread: a record of kind %u", header.kind);
                }
            });
            gs_.Flush();
            vu1_->Played(chunk);
        }
    }

    std::thread thread_;
    std::thread vu1Thread_;
    Queue gs_;
    std::unique_ptr<Queue> vu1_;
    std::mutex framesLock_;
    std::condition_variable framesDone_;
    u64 framesQueued_ = 0;
    u64 framesMade_ = 0;
};

RenderThread* g_render = nullptr;
}

void RenderStart()
{
    // ($PS2_VU1_THREAD=0 keeps VIF1 and VU1 on the EE's thread)
    const char* vu1Setting = getenv("PS2_VU1_THREAD");
    const bool vu1Thread =
        g_game != nullptr && g_game->vu1Thread != 0 && (vu1Setting == nullptr || std::strcmp(vu1Setting, "0") != 0);
    const char* setting = getenv("PS2_RENDER_THREAD");
    if (setting != nullptr && std::strcmp(setting, "0") == 0)
    {
        Log("graphics: the GS on the game's thread ($PS2_RENDER_THREAD=0)%s", vu1Thread ? ", VIF1 and VU1 too" : "");
        DisplayNoRenderThread();
        return;
    }
    g_render = new RenderThread();
    g_render->Start(vu1Thread);
    if (vu1Thread)
    {
        Log("graphics: VIF1 and VU1 on a thread of their own");
    }
}

void RenderCall(std::function<void()> work)
{
    if (g_render == nullptr)
    {
        work();
        return;
    }
    g_render->Call(std::move(work));
}

void RenderFlush()
{
    if (g_render != nullptr)
    {
        g_render->Flush();
    }
}

void RenderSync()
{
    if (g_render != nullptr)
    {
        g_render->Sync();
    }
}

bool Vu1Threaded() { return g_render != nullptr && g_render->Vu1Threaded(); }

void Vu1Data(const u32* words, u32 count) { g_render->Vif1(words, count); }

void Vu1Start(u32 address) { g_render->Vu1Start(address); }

void Vu1Sync()
{
    if (Vu1Threaded())
    {
        g_render->Vu1Sync();
    }
}

// The pictures the game's thread may be ahead of the render thread
constexpr u64 PicturesAhead = 2;

bool RenderFrame(std::function<void()> show, bool always)
{
    if (g_render == nullptr)
    {
        show();
        return true;
    }
    if (!always && g_render->Behind() >= PicturesAhead)
    {
        return false;
    }
    g_render->QueueFrame(std::move(show));
    return true;
}

void RenderHold()
{
    if (g_render != nullptr)
    {
        g_render->WaitBehind(PicturesAhead);
    }
}
}
