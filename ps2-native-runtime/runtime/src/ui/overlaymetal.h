// The overlay's Metal device (overlaymetal.mm), for the Metal presenter (presentmetal.mm). Objective-C++ only
#pragma once

#include "overlaydevice.h"

#import <Metal/Metal.h>

namespace Ps2::Ui
{
class MetalOverlay : public OverlayDevice
{
public:
    // The render pass the next lists are drawn into, and the presenter's frame (its buffers are used in turn by frame, so at
    // most three frames may be in flight)
    virtual void Begin(id<MTLRenderCommandEncoder> encoder, u32 frame) = 0;
};

// Its shader and pipeline for targets of a pixel format made (null: they couldn't be, logged)
std::unique_ptr<MetalOverlay> MakeMetalOverlay(id<MTLDevice> device, MTLPixelFormat format);
}
