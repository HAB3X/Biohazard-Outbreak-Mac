// The window drawn by Metal (presenter.h), for the Metal renderer: the window's CAMetalLayer shows each frame directly. The
// renderer copies the shown picture into its slot's texture and commits the copy to the queue both share (hw/hw/metalshared.h);
// the presenter's drawing of it, committed later, runs after it, so neither thread waits for the GPU. The picture is drawn at
// 4:3 letterboxed (plain, FXAA or the CRT filter: presentgl.cpp's GLSL programs in Metal's language, line for line; GLSL's mod
// is Mod here) with the PC menus over it (ui/overlaymetal.mm). At most three frames are in flight (the layer's drawables).
#include "presenter.h"

#include "ui/menus.h"
#include "ui/overlaymetal.h"

#include "../../hw/gs/gs.h"
#include "../../hw/hw/metalshared.h"

#include <SDL3/SDL_metal.h>

#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstring>

namespace Ps2
{
namespace
{
constexpr const char* PictureShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct PictureUniforms
{
    float4 uvRect;
    float2 texel;
    float2 pictureSize;
    float2 viewSize;
};

struct Varyings
{
    float4 position [[position]];
    float2 place;
};

// A quad over the viewport from the vertex number alone (a strip of 4), rows top first
vertex Varyings PictureVertex(uint id [[vertex_id]], constant PictureUniforms& u [[buffer(0)]])
{
    float2 corner = float2(float(id & 1), float(id >> 1));
    Varyings out;
    out.place = mix(u.uvRect.xy, u.uvRect.zw, float2(corner.x, 1.0 - corner.y));
    out.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

fragment float4 PicturePlain(Varyings in [[stage_in]], texture2d<float> picture [[texture(0)]], sampler smp [[sampler(0)]])
{
    return float4(picture.sample(smp, in.place).rgb, 1.0);
}

float Mod(float x, float y)
{
    return x - y * floor(x / y);
}

float Luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

// FXAA (a short version of Timothy Lottes' FXAA 3 console algorithm): edges found by luma contrast with their neighbours and
// blended along them, on the shown picture
fragment float4 PictureFxaa(Varyings in [[stage_in]], texture2d<float> picture [[texture(0)]], sampler smp [[sampler(0)]],
                            constant PictureUniforms& u [[buffer(0)]])
{
    float2 place = in.place;
    float2 texel = u.texel;
    float3 m = picture.sample(smp, place).rgb;
    float3 nw = picture.sample(smp, place + float2(-1.0, -1.0) * texel).rgb;
    float3 ne = picture.sample(smp, place + float2(1.0, -1.0) * texel).rgb;
    float3 sw = picture.sample(smp, place + float2(-1.0, 1.0) * texel).rgb;
    float3 se = picture.sample(smp, place + float2(1.0, 1.0) * texel).rgb;
    float lm = Luma(m);
    float lnw = Luma(nw);
    float lne = Luma(ne);
    float lsw = Luma(sw);
    float lse = Luma(se);
    float lowest = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float highest = max(lm, max(max(lnw, lne), max(lsw, lse)));
    float2 direction = float2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduce = max((lnw + lne + lsw + lse) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float smallest = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * smallest, float2(-8.0), float2(8.0)) * texel;
    float3 a = 0.5 * (picture.sample(smp, place + direction * (1.0 / 3.0 - 0.5)).rgb +
                      picture.sample(smp, place + direction * (2.0 / 3.0 - 0.5)).rgb);
    float3 b = a * 0.5 + 0.25 * (picture.sample(smp, place - direction * 0.5).rgb + picture.sample(smp, place + direction * 0.5).rgb);
    float lb = Luma(b);
    return float4((lb < lowest || lb > highest) ? a : b, 1.0);
}

// The CRT filter (presentgl.cpp says what each part is). pictureSize is the PS2's lines
fragment float4 PictureCrt(Varyings in [[stage_in]], texture2d<float> picture [[texture(0)]], sampler smp [[sampler(0)]],
                           constant PictureUniforms& u [[buffer(0)]])
{
    float2 place = in.place;
    float2 texel = 1.0 / u.pictureSize;
    float3 beam = picture.sample(smp, place).rgb * 0.5 + picture.sample(smp, place - float2(texel.x, 0.0)).rgb * 0.25 +
                  picture.sample(smp, place + float2(texel.x, 0.0)).rgb * 0.25;
    float row = fract(place.y * u.pictureSize.y);
    float brightness = max(max(beam.r, beam.g), beam.b);
    float depth = mix(0.35, 0.12, brightness);
    float line = 1.0 - depth * pow(abs(row - 0.5) * 2.0, 2.0);
    float column = Mod(floor(place.x * u.viewSize.x), 3.0);
    float3 grille = u.viewSize.x >= u.pictureSize.x * 2.0 ? (column == 0.0 ? float3(1.0, 0.94, 0.94)
                                                           : column == 1.0 ? float3(0.94, 1.0, 0.94) : float3(0.94, 0.94, 1.0))
                                                         : float3(1.0);
    float2 centred = place * 2.0 - 1.0;
    float vignette = 1.0 - 0.08 * dot(centred * centred, float2(0.5, 0.5));
    return float4(clamp(beam * line * grille * 1.12 * vignette, 0.0, 1.0), 1.0);
}
)";

struct alignas(16) PictureUniforms
{
    f32 uvRect[4];
    f32 texel[2];
    f32 pictureSize[2];
    f32 viewSize[2];
};

// The window's pixels (the layer's, and a picture's of the window)
constexpr MTLPixelFormat WindowFormat = MTLPixelFormatBGRA8Unorm;
constexpr u32 FramesInFlight = 3;

class MetalPresenter : public Presenter
{
public:
    explicit MetalPresenter(NativeGraphics::MetalGpu& gpu) : gpu_(gpu) {}
    ~MetalPresenter() override;
    NativeGraphics::GpuApi RendererApi() const override { return NativeGraphics::GpuApi::Metal; }
    SDL_WindowFlags PrepareWindow() override { return SDL_WINDOW_METAL; }
    bool Open(SDL_Window* window, bool vsync) override;
    // The Metal renderer needs nothing of the main thread's
    bool OpenRenderer() override { return true; }
    void RenderThreadStart() override {}
    void SetVsync(bool on) override;
    void UseShown(NativeGraphics::ShownTexture& shown) override;
    void UsePixels(const Gs::DisplayImage& image) override;
    void PictureSize(u32* width, u32* height) const override
    {
        *width = width_;
        *height = height_;
    }
    void* Present(const WindowFrame& frame, bool picture) override;
    // (no fences: the shared queue orders the GPU's work)
    void DropFence(void*) override {}
    bool Capture(const WindowFrame& frame, bool picture, std::vector<u32>& pixels) override;
    Ui::OverlayDevice* Overlay() override { return overlay_.get(); }

private:
    // A frame begun (waiting while three are in flight), and its command buffer committed (its end lets the next one begin)
    void BeginFrame();
    void Commit(id<MTLCommandBuffer> commands);
    // The frame encoded into a target of the frame's pixels
    void Encode(id<MTLCommandBuffer> commands, id<MTLTexture> target, const WindowFrame& frame, bool picture);

    NativeGraphics::MetalGpu& gpu_;
    SDL_MetalView view_ = nullptr;
    CAMetalLayer* layer_ = nil;
    id<MTLRenderPipelineState> pipelines_[3] = {};
    id<MTLSamplerState> sampler_ = nil;
    std::unique_ptr<Ui::MetalOverlay> overlay_;
    dispatch_semaphore_t inFlight_ = nullptr;
    bool frameOpen_ = false;
    u32 frameIndex_ = 0;
    // The picture in use (a slot's copy, or one of the software GS's pictures' textures, used in turn) and its size
    id<MTLTexture> texture_ = nil;
    id<MTLTexture> pixels_[FramesInFlight] = {};
    u32 nextPixels_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
};

MetalPresenter::~MetalPresenter()
{
    // The frames in flight finished before the layer goes (and the semaphore back at its count: libdispatch traps on one let go
    // of below it). A frame begun but not committed holds one
    if (frameOpen_)
    {
        dispatch_semaphore_signal(inFlight_);
    }
    for (u32 i = 0; inFlight_ != nullptr && i < FramesInFlight; i++)
    {
        dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
    }
    for (u32 i = 0; inFlight_ != nullptr && i < FramesInFlight; i++)
    {
        dispatch_semaphore_signal(inFlight_);
    }
    layer_ = nil;
    if (view_ != nullptr)
    {
        SDL_Metal_DestroyView(view_);
    }
}

bool MetalPresenter::Open(SDL_Window* window, bool vsync)
{
    id<MTLDevice> device = gpu_.device;
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:PictureShaderSource] options:nil error:&error];
    if (library == nil)
    {
        Log("graphics: the window's Metal shaders: %s", [[error localizedDescription] UTF8String]);
        return false;
    }

    const char* fragments[3] = {"PicturePlain", "PictureFxaa", "PictureCrt"};
    for (u32 i = 0; i < 3; i++)
    {
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"PictureVertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:[NSString stringWithUTF8String:fragments[i]]];
        descriptor.colorAttachments[0].pixelFormat = WindowFormat;
        pipelines_[i] = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (pipelines_[i] == nil)
        {
            Log("graphics: the window's Metal pipeline %s: %s", fragments[i], [[error localizedDescription] UTF8String]);
            return false;
        }
    }

    MTLSamplerDescriptor* sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = MTLSamplerMinMagFilterLinear;
    sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_ = [device newSamplerStateWithDescriptor:sampler];
    overlay_ = Ui::MakeMetalOverlay(device, WindowFormat);
    inFlight_ = dispatch_semaphore_create(FramesInFlight);
    if (window == nullptr)
    {
        return true;
    }

    view_ = SDL_Metal_CreateView(window);
    if (view_ == nullptr)
    {
        Log("graphics: no Metal view for the window: %s", SDL_GetError());
        return false;
    }

    layer_ = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view_);
    layer_.device = device;
    layer_.pixelFormat = WindowFormat;
    layer_.framebufferOnly = YES;
    layer_.maximumDrawableCount = FramesInFlight;
    layer_.displaySyncEnabled = vsync ? YES : NO;
    Log("graphics: the window drawn by Metal");
    return true;
}

void MetalPresenter::SetVsync(bool on)
{
    if (layer_ != nil)
    {
        layer_.displaySyncEnabled = on ? YES : NO;
    }
}

void MetalPresenter::BeginFrame()
{
    if (!frameOpen_)
    {
        dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
        frameOpen_ = true;
        frameIndex_++;
    }
}

void MetalPresenter::Commit(id<MTLCommandBuffer> commands)
{
    gpu_.Measure(commands);
    dispatch_semaphore_t inFlight = inFlight_;
    [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
        dispatch_semaphore_signal(inFlight);
    }];
    [commands commit];
    frameOpen_ = false;
}

void MetalPresenter::UseShown(NativeGraphics::ShownTexture& shown)
{
    texture_ = (__bridge id<MTLTexture>)shown.metal;
    width_ = shown.width;
    height_ = shown.height;
}

void MetalPresenter::UsePixels(const Gs::DisplayImage& image)
{
    // The texture used three pictures ago is free once this frame may begin (at most three in flight)
    BeginFrame();
    __strong id<MTLTexture>& texture = pixels_[nextPixels_];
    nextPixels_ = (nextPixels_ + 1) % FramesInFlight;
    if (texture == nil || texture.width != image.width || texture.height != image.height)
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:image.width
                                                                                             height:image.height
                                                                                          mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        texture = [gpu_.device newTextureWithDescriptor:descriptor];
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height) mipmapLevel:0 withBytes:image.pixels.data()
               bytesPerRow:static_cast<NSUInteger>(image.width) * 4];
    texture_ = texture;
    width_ = image.width;
    height_ = image.height;
}

void MetalPresenter::Encode(id<MTLCommandBuffer> commands, id<MTLTexture> target, const WindowFrame& frame, bool picture)
{
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    const SDL_FRect& dst = frame.place;
    if (picture && texture_ != nil && dst.w > 0 && dst.h > 0)
    {
        [encoder setViewport:MTLViewport{dst.x, dst.y, dst.w, dst.h, 0, 1}];
        [encoder setRenderPipelineState:pipelines_[static_cast<u32>(frame.filter)]];
        PictureUniforms u = {{0, 0, 1, 1},
                             {1.0f / static_cast<f32>(width_), 1.0f / static_cast<f32>(height_)},
                             {frame.pictureLines[0], frame.pictureLines[1]},
                             {dst.w, dst.h}};
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:0];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];
        [encoder setFragmentTexture:texture_ atIndex:0];
        [encoder setFragmentSamplerState:sampler_ atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }

    if (overlay_ != nullptr)
    {
        overlay_->Begin(encoder, frameIndex_);
        Ui::MenusDraw(*overlay_, frame.width, frame.height, frame.pixelScale, dst);
    }

    [encoder endEncoding];
}

void* MetalPresenter::Present(const WindowFrame& frame, bool picture)
{
    @autoreleasepool
    {
        BeginFrame();
        const CGSize size = CGSizeMake(frame.width, frame.height);
        if (!CGSizeEqualToSize(layer_.drawableSize, size))
        {
            layer_.drawableSize = size;
        }

        id<CAMetalDrawable> drawable = [layer_ nextDrawable];
        if (drawable == nil)
        {
            // (none came in time: this frame isn't shown)
            Log("graphics: the window had no Metal drawable for a frame");
            dispatch_semaphore_signal(inFlight_);
            frameOpen_ = false;
            return nullptr;
        }

        id<MTLCommandBuffer> commands = [gpu_.queue commandBuffer];
        Encode(commands, drawable.texture, frame, picture);
        [commands presentDrawable:drawable];
        Commit(commands);
    }
    return nullptr;
}

bool MetalPresenter::Capture(const WindowFrame& frame, bool picture, std::vector<u32>& pixels)
{
    @autoreleasepool
    {
        BeginFrame();
        const NSUInteger width = static_cast<NSUInteger>(frame.width), height = static_cast<NSUInteger>(frame.height);
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:WindowFormat width:width height:height
                                                                                         mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget;
        descriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> target = [gpu_.device newTextureWithDescriptor:descriptor];
        id<MTLBuffer> out = [gpu_.device newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commands = [gpu_.queue commandBuffer];
        Encode(commands, target, frame, picture);
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1)
                     toBuffer:out destinationOffset:0 destinationBytesPerRow:width * 4 destinationBytesPerImage:width * height * 4];
        [blit endEncoding];
        Commit(commands);
        [commands waitUntilCompleted];
        if (commands.status == MTLCommandBufferStatusError)
        {
            Log("graphics: the window's picture: %s", [[commands.error localizedDescription] UTF8String]);
            return false;
        }

        // BGRA to RGBA
        const u32* bgra = static_cast<const u32*>([out contents]);
        pixels.resize(width * height);
        for (size_t i = 0; i < pixels.size(); i++)
        {
            const u32 p = bgra[i];
            pixels[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
        }
    }
    return true;
}
}

std::unique_ptr<Presenter> MakeMetalPresenter()
{
    NativeGraphics::MetalGpu* gpu = NativeGraphics::SharedMetalGpu();
    if (gpu == nullptr)
    {
        return nullptr;
    }

    return std::make_unique<MetalPresenter>(*gpu);
}
}
