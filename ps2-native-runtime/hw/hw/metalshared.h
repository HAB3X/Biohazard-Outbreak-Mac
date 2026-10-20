#pragma once

// The Mac's GPU as Metal sees it, one for the program: the Metal renderer (metaldevice.mm) and the window's Metal presenter
// (the runtime's presentmetal.mm) share its device and its command queue, so the presenter's drawing of a picture the renderer
// copied for it runs after the copy (a queue's command buffers run in the order they're committed). Objective-C++ only.

#import <Metal/Metal.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace NativeGraphics
{
struct MetalGpu
{
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    // The GPU's busy time on the command buffers both committed (nanoseconds: the union of their GPU start to end times, as
    // command buffers of a queue can run at once), and where the union's last interval ends
    std::atomic<uint64_t> gpuNanoseconds{0};
    std::mutex busyLock;
    double busyUntil = 0;

    // A command buffer's GPU time added to gpuNanoseconds when it completes (before it's committed)
    void Measure(id<MTLCommandBuffer> commands);
};

// Made the first time it's asked for (any thread); null when there's no Apple GPU (the renderer reads its targets in its
// fragment shader, which Apple's GPUs alone do), logged once
MetalGpu* SharedMetalGpu();
}
