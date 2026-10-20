// The hardware renderer's Metal device (gpudevice.h, docs/metal.md): the GS's primitives drawn by Metal on Apple's GPUs, each
// pixel's GS rules made in the fragment shader on the target's own pixel (programmable blending), so nothing a batch draws needs
// a copy of its target or a pass of its own. Colour targets are RGBA8 (16 bit frames as their 5 bits shifted up and the alpha
// bit as 0x80, as the OpenGL device keeps them), depth targets R32Uint colour attachments holding the GS's values. GS memory's
// copy is a shared buffer of its words (Apple silicon's memory is the GPU's): the CPU's pages go into it by blits in command
// order, and it's read straight after the queue is idle. The shaders are metalshaders.h.
//
// One command buffer is filled until something has to wait for the GPU (a read back, a picture), and one render pass is kept
// open across batches while they draw into the same targets.

#include "gpudevice.h"

#include "gl.h"
#include "metalshaders.h"

#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/OpenGL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace NativeGraphics
{
namespace
{
// The shaders' structures (metalshaders.h), the same layout
// (sizes and alignment as Metal's: float2 by 8, int4 by 16)
struct alignas(8) VertexUniforms
{
    f32 viewSize[2];
    f32 pointSize;
    u32 perPrimitive;
    u32 zmax;
};

struct alignas(16) DrawUniforms
{
    f32 texSize[2];
    f32 texActual[2];
    f32 texSnap;
    s32 targetScale;
    f32 lodK;
    f32 lodL;
    f32 lodMax;
    u32 ta0;
    u32 ta1;
    u32 aem;
    u32 fogR;
    u32 fogG;
    u32 fogB;
    u32 frameKind;
    u32 fbmsk;
    u32 frameWrite;
    u32 depthKind;
    u32 ztst;
    u32 zWrite;
    u32 zmax;
    u32 alphaTest;
    u32 atst;
    u32 aref;
    u32 afail;
    u32 date;
    u32 datm;
    u32 blend;
    u32 blendA;
    u32 blendB;
    u32 blendC;
    u32 blendD;
    u32 blendFix;
    u32 pabe;
    u32 colclamp;
    u32 fba;
    u32 dither;
    u32 ditherScale;
    u32 levelCount;
    u32 magLinear;
    u32 mmin;
    u32 mxl;
    u32 textured;
    u32 tfx;
    u32 tcc;
    u32 fog;
    u32 uvMode;
    u32 texAlpha;
    u32 mipmap;
    u32 lodConst;
    u32 nativeTexel;
    u32 region;
    u32 gsSample;
    u32 nativeZtst;
    s32 dimx[16];
    alignas(16) s32 regionMode[4];
    s32 regionBounds[4];
};

struct LayoutUniforms
{
    s32 unitsPerBlock;
    s32 unitShift;
    s32 unitBits;
    s32 pageWidth;
    s32 pageHeight;
    s32 pagesAcross;
    s32 pagesDown;
    s32 basePointer;
    s32 xorZ;
};

struct alignas(16) ConversionUniforms
{
    u32 storage;
    u32 targetScale;
    u32 firstWord;
    u32 words;
    u32 pageMask[16];
    s32 rect[4];
};

struct alignas(16) MergeUniforms
{
    u32 firstWord;
    u32 words;
    u32 rectCount;
    u32 unitMask;
    u32 pageMask[16];
    s32 rects[8][4];
};

struct alignas(16) DecodeUniforms
{
    s32 psm;
    s32 cpsm;
    s32 csa;
    u32 ta0;
    u32 ta1;
    u32 aem;
    alignas(16) s32 rect[4];
};

struct alignas(16) CircuitUniform
{
    u32 enabled;
    u32 pad[3];
    f32 place[4];
    f32 corner[2];
    f32 pad2[2];
};

struct alignas(16) CircuitsUniforms
{
    CircuitUniform circuit[2];
    f32 alp;
    u32 ownAlpha;
    u32 pad[2];
    f32 background[4];
};

static_assert(sizeof(VertexUniforms) == 24 && sizeof(DrawUniforms) == 320 && sizeof(ConversionUniforms) == 96 &&
              sizeof(MergeUniforms) == 208 && sizeof(DecodeUniforms) == 48 && sizeof(CircuitsUniforms) == 128);

constexpr u32 MetalMaxTargetSize = 16384;
constexpr size_t ArenaChunk = 8 * 1024 * 1024;

// The runs of pages in a set (first, last)
std::vector<std::pair<u32, u32>> PageRuns(const Gs::PageSet& pages)
{
    std::vector<std::pair<u32, u32>> runs;
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!pages.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && pages.test(end + 1))
        {
            end++;
        }

        runs.emplace_back(page, end);
        page = end;
    }

    return runs;
}

void PageMask(const Gs::PageSet& pages, u32* mask)
{
    std::memset(mask, 0, 16 * sizeof(u32));
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (pages.test(page))
        {
            mask[page >> 5] |= 1u << (page & 31);
        }
    }
}

LayoutUniforms LayoutFor(u32 psm, u32 bp, u32 bw, u32 pagesDown, u32* family)
{
    bool xorZ = false;
    LayoutOf(psm, family, &xorZ);
    const FamilyInfo& info = Families[*family];
    LayoutUniforms l;
    l.unitsPerBlock = static_cast<s32>(info.unitsPerBlock);
    l.unitShift = static_cast<s32>(info.unitShift);
    l.unitBits = static_cast<s32>(info.bits);
    l.pageWidth = static_cast<s32>(info.pageWidth);
    l.pageHeight = static_cast<s32>(info.pageHeight);
    l.pagesAcross = static_cast<s32>(std::max<u32>(info.halfWidth ? bw >> 1 : bw, 1));
    l.pagesDown = static_cast<s32>(pagesDown);
    l.basePointer = static_cast<s32>(bp);
    l.xorZ = xorZ ? 1 : 0;
    return l;
}

class MetalDevice : public GpuDevice
{
public:
    MetalDevice(HardwareStats& stats, const Gs::Gs& gs) : stats_(stats), gs_(gs) {}
    ~MetalDevice() override;

    bool Init() override;
    void MakeCurrent() override {}
    bool ReadsDestination() const override { return true; }
    u32 MaxTargetSize() const override { return MetalMaxTargetSize; }

    bool MakeTarget(Target& target) override;
    void FreeTarget(Target& target) override;
    // A depth target holds the GS's values themselves: zeros are zeros whichever way HwGs would keep them
    void ClearDepth(Target&) override {}
    // Nothing is kept aside: every draw is in its target
    bool Pending(const Target&) override { return false; }
    void Resolve(Target&) override {}
    TextureId CopyTarget(Target& target) override;
    bool ReadTargetPixel(Target& target, u32 x, u32 y, u32* value) override;
    void ReadTargetScaled(Target& target, std::vector<u32>& pixels) override;
    void DropCopies() override;

    void InitMemory(const LayoutTables& tables) override;
    void UploadMemory(const Gs::PageSet& pages, const u8* memory) override;
    void ReadMemory(const Gs::PageSet& pages, u8* out) override;
    void MemoryFromTarget(Target& target, const Gs::PageSet& pages) override;
    void TargetFromMemory(Target& target, const Gs::PageSet& pages, const Rect& rect) override;
    void MergeWrites(const Gs::PageSet& pages, const u8* memory, const std::vector<MergeGroup>& groups) override;

    TextureId MakeTexture(const u32* widths, const u32* heights, u32 levels) override;
    void UploadTexture(TextureId texture, u32 level, const Rect& rect, const u32* texels, u32 stride) override;
    TextureId MakeImage(u32 width, u32 height, const u32* pixels) override;
    void FreeTexture(TextureId texture) override;
    void DecodeLevels(TextureId texture, const DecodeJob& job) override;
    TextureId MakeUpscaled(TextureId source, u32 width, u32 height, u32 scale) override;

    void DrawBatch(const BatchKey& key, const std::vector<HwVertex>& vertexes, const std::vector<BatchShape>& shapes) override;

    TextureId MergeCircuits(const CircuitPicture* circuits, u32 width, u32 height, f32 alp, bool ownAlpha, u32 background) override;
    void ReadPicture(const Picture& picture, std::vector<u32>& pixels) override;
    bool CopyPicture(const Picture& picture, u32 slot, void* drawnFence, ShownTexture* shown) override;
    void PictureDone() override;

private:
    // The command buffer being filled, and its one open encoder
    id<MTLCommandBuffer> Commands();
    void EndEncoder();
    id<MTLRenderCommandEncoder> RenderInto(const Target* colour, const Target* depth);
    id<MTLComputeCommandEncoder> Compute();
    id<MTLBlitCommandEncoder> Blit();
    // The command buffer committed (and the GPU waited for)
    void Submit(bool wait);
    // Bytes for the GPU to read in this command buffer (vertexes, the CPU's pages): a shared buffer and the offset in it
    id<MTLBuffer> Stage(const void* data, size_t bytes, size_t* offset);

    TextureId Keep(id<MTLTexture> texture);
    id<MTLTexture> TextureOf(TextureId id) const { return id != 0 && id <= textures_.size() ? textures_[id - 1] : nil; }
    id<MTLLibrary> Library(bool depth);
    id<MTLComputePipelineState> Kernel(const char* name);
    id<MTLRenderPipelineState> DrawPipeline(bool depth);
    id<MTLSamplerState> SamplerFor(const BatchKey& key);
    id<MTLBuffer> Bytes(const void* data, size_t bytes, size_t* offset) { return Stage(data, bytes, offset); }
    // A rectangle of a texture read back (RGBA bytes or words), the GPU waited for
    void ReadTexture(id<MTLTexture> texture, u32 x, u32 y, u32 width, u32 height, u32* out);

    HardwareStats& stats_;
    const Gs::Gs& gs_;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    // The shaders compiled with and without a depth attachment, and the draw's pipeline for each (made when the device starts:
    // every draw's features are the shader's branches, so nothing is compiled while a game plays)
    id<MTLLibrary> libraries_[2] = {};
    id<MTLRenderPipelineState> drawPipelines_[2] = {};
    id<MTLCommandBuffer> commands_ = nil;
    // When the command buffer was begun, and its draws: it's committed once it has filled for a while, whether or not anything
    // asks for the picture (a game run without a window or pictures would otherwise keep its work from the GPU)
    double commandsOpened_ = 0;
    u32 commandsDraws_ = 0;
    id<MTLRenderCommandEncoder> render_ = nil;
    const Target* renderColour_ = nullptr;
    const Target* renderDepth_ = nullptr;
    id<MTLComputeCommandEncoder> compute_ = nil;
    id<MTLBlitCommandEncoder> blit_ = nil;
    std::vector<id<MTLTexture>> textures_;
    std::vector<TextureId> freeTextures_;
    std::unordered_map<std::string, id<MTLComputePipelineState>> kernels_;
    std::unordered_map<u32, id<MTLSamplerState>> samplers_;
    id<MTLBuffer> memory_ = nil;
    id<MTLBuffer> forward_[5] = {};
    id<MTLBuffer> inverse_[5] = {};
    // The CLUT as the decodes of this command buffer read it (staged when its version changes)
    id<MTLBuffer> clutBuffer_ = nil;
    size_t clutOffset_ = 0;
    u32 clutVersion_ = ~0u;
    id<MTLCommandBuffer> clutCommands_ = nil;
    // 1x1 textures for draws without one (every binding the shader names is set)
    id<MTLTexture> blank_ = nil;
    id<MTLTexture> blankWords_ = nil;
    // The depth targets' copies the native depth test reads (by size)
    std::map<std::pair<u32, u32>, id<MTLTexture>> depthCopies_;
    // Staging: shared buffers of ArenaChunk filled in order; those of committed command buffers come back when they're done
    std::vector<id<MTLBuffer>> arenaUsed_;
    id<MTLBuffer> arena_ = nil;
    size_t arenaOffset_ = 0;
    std::mutex freeArenasLock_;
    std::vector<id<MTLBuffer>> freeArenas_;
    // The copies of targets a batch reads (by size), and the merged circuits' picture
    std::map<std::pair<u32, u32>, TextureId> copies_;
    TextureId merged_ = 0;
    // The window's hand-over (display.cpp's three slots): the picture copied by Metal into an IOSurface the render thread's
    // OpenGL sees too (a rectangle texture of it), then by OpenGL into the slot's texture the window's context draws
    struct HandOver
    {
        IOSurfaceRef surface = nullptr;
        id<MTLTexture> metal = nil;
        Hw::GLuint rectangle = 0;
        Hw::GLuint rectangleFbo = 0;
        Hw::GLuint texture = 0;
        Hw::GLuint fbo = 0;
        u32 width = 0;
        u32 height = 0;
    };
    HandOver handOver_[3];
    bool handedOver_ = false;
    bool MakeHandOver(HandOver& h, u32 width, u32 height);
};

MetalDevice::~MetalDevice()
{
    Submit(true);
    for (HandOver& h : handOver_)
    {
        if (h.surface != nullptr)
        {
            CFRelease(h.surface);
        }
    }
}

bool MetalDevice::Init()
{
    @autoreleasepool
    {
        device_ = MTLCreateSystemDefaultDevice();
        if (device_ == nil)
        {
            std::fprintf(stderr, "graphics: Metal renderer: no Metal device\n");
            return false;
        }

        // Programmable blending (reading the target in the fragment shader) is Apple's GPUs'
        if (![device_ supportsFamily:MTLGPUFamilyApple1])
        {
            std::fprintf(stderr, "graphics: Metal renderer: %s can't read its targets in a shader\n", [[device_ name] UTF8String]);
            return false;
        }

        queue_ = [device_ newCommandQueue];
        uint8_t white[4] = {0, 0, 0, 0};
        MTLTextureDescriptor* blank = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1
                                                                                         height:1 mipmapped:NO];
        blank_ = [device_ newTextureWithDescriptor:blank];
        [blank_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
        MTLTextureDescriptor* words = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:1 height:1
                                                                                     mipmapped:NO];
        blankWords_ = [device_ newTextureWithDescriptor:words];
        DrawPipeline(false);
        DrawPipeline(true);
        for (const char* name :
             {"MemoryFromTarget", "TargetFromMemory", "MergeWrites", "DecodeLevel", "Upscale", "MergeCircuits", "CopyPicture"})
        {
            Kernel(name);
        }
        std::fprintf(stderr, "graphics: Metal renderer on %s\n", [[device_ name] UTF8String]);
    }

    return true;
}

id<MTLLibrary> MetalDevice::Library(bool depth)
{
    __strong id<MTLLibrary>& library = libraries_[depth ? 1 : 0];
    if (library == nil)
    {
        NSError* error = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.preprocessorMacros = @{@"DEPTH" : @(depth ? 1 : 0)};
        library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:MetalShaderSource] options:options error:&error];
        if (library == nil)
        {
            std::fprintf(stderr, "graphics: Metal renderer: the shaders: %s\n", [[error localizedDescription] UTF8String]);
            std::abort();
        }
    }

    return library;
}

id<MTLComputePipelineState> MetalDevice::Kernel(const char* name)
{
    auto found = kernels_.find(name);
    if (found != kernels_.end())
    {
        return found->second;
    }

    NSError* error = nil;
    id<MTLFunction> function = [Library(false) newFunctionWithName:[NSString stringWithUTF8String:name]];
    id<MTLComputePipelineState> state = [device_ newComputePipelineStateWithFunction:function error:&error];
    if (state == nil)
    {
        std::fprintf(stderr, "graphics: Metal renderer: kernel %s: %s\n", name, [[error localizedDescription] UTF8String]);
        std::abort();
    }

    return kernels_[name] = state;
}

id<MTLRenderPipelineState> MetalDevice::DrawPipeline(bool depth)
{
    __strong id<MTLRenderPipelineState>& state = drawPipelines_[depth ? 1 : 0];
    if (state != nil)
    {
        return state;
    }

    id<MTLLibrary> library = Library(depth);
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"DrawVertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"DrawFragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    if (depth)
    {
        descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatR32Uint;
    }

    NSError* error = nil;
    state = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (state == nil)
    {
        std::fprintf(stderr, "graphics: Metal renderer: the draw's pipeline: %s\n", [[error localizedDescription] UTF8String]);
        std::abort();
    }

    return state;
}

id<MTLSamplerState> MetalDevice::SamplerFor(const BatchKey& key)
{
    const u32 id = static_cast<u32>(key.minFilter) | static_cast<u32>(key.magFilter) << 4 | key.clampS << 8 | key.clampT << 9 |
                   std::min<u32>(key.anisotropy, 16) << 10 | std::min<u32>(key.maxLevel, 15) << 16;
    auto found = samplers_.find(id);
    if (found != samplers_.end())
    {
        return found->second;
    }

    MTLSamplerDescriptor* descriptor = [MTLSamplerDescriptor new];
    const bool minLinear = key.minFilter == Filter::Linear || key.minFilter == Filter::LinearMipNearest ||
                           key.minFilter == Filter::LinearMipLinear;
    descriptor.minFilter = minLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    descriptor.magFilter = key.magFilter == Filter::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    descriptor.mipFilter = key.minFilter == Filter::NearestMipNearest || key.minFilter == Filter::LinearMipNearest ? MTLSamplerMipFilterNearest
                           : key.minFilter == Filter::NearestMipLinear || key.minFilter == Filter::LinearMipLinear  ? MTLSamplerMipFilterLinear
                                                                                                                    : MTLSamplerMipFilterNotMipmapped;
    descriptor.sAddressMode = key.clampS ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
    descriptor.tAddressMode = key.clampT ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
    descriptor.maxAnisotropy = std::max<u32>(1, std::min<u32>(key.anisotropy, 16));
    descriptor.lodMaxClamp = static_cast<float>(key.maxLevel);
    return samplers_[id] = [device_ newSamplerStateWithDescriptor:descriptor];
}

id<MTLCommandBuffer> MetalDevice::Commands()
{
    if (commands_ == nil)
    {
        commands_ = [queue_ commandBuffer];
        commandsOpened_ = CFAbsoluteTimeGetCurrent();
        commandsDraws_ = 0;
    }

    return commands_;
}

void MetalDevice::EndEncoder()
{
    if (render_ != nil)
    {
        [render_ endEncoding];
        render_ = nil;
        renderColour_ = nullptr;
        renderDepth_ = nullptr;
    }

    if (compute_ != nil)
    {
        [compute_ endEncoding];
        compute_ = nil;
    }

    if (blit_ != nil)
    {
        [blit_ endEncoding];
        blit_ = nil;
    }
}

id<MTLRenderCommandEncoder> MetalDevice::RenderInto(const Target* colour, const Target* depth)
{
    if (render_ != nil && renderColour_ == colour && renderDepth_ == depth)
    {
        return render_;
    }

    EndEncoder();
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = TextureOf(colour->texture);
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (depth != nullptr)
    {
        pass.colorAttachments[1].texture = TextureOf(depth->texture);
        pass.colorAttachments[1].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[1].storeAction = MTLStoreActionStore;
    }

    render_ = [Commands() renderCommandEncoderWithDescriptor:pass];
    renderColour_ = colour;
    renderDepth_ = depth;
    return render_;
}

id<MTLComputeCommandEncoder> MetalDevice::Compute()
{
    if (compute_ == nil)
    {
        EndEncoder();
        compute_ = [Commands() computeCommandEncoder];
    }

    return compute_;
}

id<MTLBlitCommandEncoder> MetalDevice::Blit()
{
    if (blit_ == nil)
    {
        EndEncoder();
        blit_ = [Commands() blitCommandEncoder];
    }

    return blit_;
}

void MetalDevice::Submit(bool wait)
{
    EndEncoder();
    if (commands_ == nil)
    {
        return;
    }

    if (arena_ != nil)
    {
        arenaUsed_.push_back(arena_);
        arena_ = nil;
        arenaOffset_ = 0;
    }

    std::vector<id<MTLBuffer>> used;
    used.swap(arenaUsed_);
    // (the staging buffers come back for reuse once the GPU is done with them)
    __block std::vector<id<MTLBuffer>> returning = std::move(used);
    std::mutex* lock = &freeArenasLock_;
    std::vector<id<MTLBuffer>>* free = &freeArenas_;
    [commands_ addCompletedHandler:^(id<MTLCommandBuffer>) {
        std::lock_guard<std::mutex> guard(*lock);
        for (id<MTLBuffer> buffer : returning)
        {
            free->push_back(buffer);
        }
    }];
    [commands_ commit];
    if (wait)
    {
        const double before = CFAbsoluteTimeGetCurrent();
        [commands_ waitUntilCompleted];
        const double waited = CFAbsoluteTimeGetCurrent() - before;
        if (waited > 0.1)
        {
            std::fprintf(stderr, "graphics: Metal renderer: waited %.3f s for the GPU (GPU time %.3f s)\n", waited,
                         commands_.GPUEndTime - commands_.GPUStartTime);
        }
        if (commands_.status == MTLCommandBufferStatusError)
        {
            std::fprintf(stderr, "graphics: Metal renderer: the GPU's work failed: %s\n",
                         [[commands_.error localizedDescription] UTF8String]);
        }
    }

    commands_ = nil;
    clutCommands_ = nil;
}

id<MTLBuffer> MetalDevice::Stage(const void* data, size_t bytes, size_t* offset)
{
    const size_t aligned = (bytes + 255) & ~static_cast<size_t>(255);
    if (aligned > ArenaChunk)
    {
        // Bigger than a chunk: a buffer of its own, let go of with the command buffer
        id<MTLBuffer> own = [device_ newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
        arenaUsed_.push_back(own);
        *offset = 0;
        return own;
    }

    if (arena_ == nil || arenaOffset_ + aligned > ArenaChunk)
    {
        if (arena_ != nil)
        {
            arenaUsed_.push_back(arena_);
        }

        arena_ = nil;
        {
            std::lock_guard<std::mutex> guard(freeArenasLock_);
            if (!freeArenas_.empty())
            {
                arena_ = freeArenas_.back();
                freeArenas_.pop_back();
            }
        }

        if (arena_ == nil)
        {
            arena_ = [device_ newBufferWithLength:ArenaChunk options:MTLResourceStorageModeShared];
        }

        arenaOffset_ = 0;
    }

    std::memcpy(static_cast<u8*>([arena_ contents]) + arenaOffset_, data, bytes);
    *offset = arenaOffset_;
    arenaOffset_ += aligned;
    return arena_;
}

TextureId MetalDevice::Keep(id<MTLTexture> texture)
{
    if (!freeTextures_.empty())
    {
        TextureId id = freeTextures_.back();
        freeTextures_.pop_back();
        textures_[id - 1] = texture;
        return id;
    }

    textures_.push_back(texture);
    return static_cast<TextureId>(textures_.size());
}

void MetalDevice::FreeTexture(TextureId texture)
{
    if (texture == 0 || texture > textures_.size())
    {
        return;
    }

    textures_[texture - 1] = nil;
    freeTextures_.push_back(texture);
}

bool MetalDevice::MakeTarget(Target& t)
{
    const u32 w = t.width * t.scale;
    const u32 h = t.height * t.scale;
    if (w == 0 || h == 0 || w > MetalMaxTargetSize || h > MetalMaxTargetSize)
    {
        t.texture = 0;
        return false;
    }

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.depth ? MTLPixelFormatR32Uint
                                                                                                       : MTLPixelFormatRGBA8Unorm
                                                                                          width:w
                                                                                         height:h
                                                                                      mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (texture == nil)
    {
        t.texture = 0;
        return false;
    }

    t.texture = Keep(texture);
    return true;
}

void MetalDevice::FreeTarget(Target& target)
{
    if (renderColour_ == &target || renderDepth_ == &target)
    {
        EndEncoder();
    }

    FreeTexture(target.texture);
    target.texture = 0;
}

TextureId MetalDevice::CopyTarget(Target& target)
{
    id<MTLTexture> source = TextureOf(target.texture);
    const auto size = std::make_pair(static_cast<u32>(source.width), static_cast<u32>(source.height));
    TextureId& copy = copies_[size];
    if (copy == 0)
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:size.first
                                                                                             height:size.second
                                                                                          mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        copy = Keep([device_ newTextureWithDescriptor:descriptor]);
    }

    [Blit() copyFromTexture:source toTexture:TextureOf(copy)];
    stats_.copies++;
    return copy;
}

void MetalDevice::DropCopies()
{
    for (auto& copy : copies_)
    {
        FreeTexture(copy.second);
    }

    copies_.clear();
    depthCopies_.clear();
}

void MetalDevice::ReadTexture(id<MTLTexture> texture, u32 x, u32 y, u32 width, u32 height, u32* out)
{
    const size_t row = static_cast<size_t>(width) * 4;
    id<MTLBuffer> buffer = [device_ newBufferWithLength:row * height options:MTLResourceStorageModeShared];
    [Blit() copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(width, height, 1)
                   toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row * height];
    Submit(true);
    std::memcpy(out, [buffer contents], row * height);
}

bool MetalDevice::ReadTargetPixel(Target& target, u32 x, u32 y, u32* value)
{
    ReadTexture(TextureOf(target.texture), x * target.scale, y * target.scale, 1, 1, value);
    return true;
}

void MetalDevice::ReadTargetScaled(Target& target, std::vector<u32>& pixels)
{
    id<MTLTexture> texture = TextureOf(target.texture);
    pixels.resize(static_cast<size_t>(texture.width) * texture.height);
    ReadTexture(texture, 0, 0, static_cast<u32>(texture.width), static_cast<u32>(texture.height), pixels.data());
}

void MetalDevice::InitMemory(const LayoutTables& tables)
{
    memory_ = [device_ newBufferWithLength:Gs::LocalMemoryBytes options:MTLResourceStorageModeShared];
    for (u32 f = 0; f < 5; f++)
    {
        forward_[f] = [device_ newBufferWithBytes:tables.forward[f].data() length:tables.forward[f].size() * 4
                                          options:MTLResourceStorageModeShared];
        inverse_[f] = [device_ newBufferWithBytes:tables.inverse[f].data() length:tables.inverse[f].size() * 4
                                          options:MTLResourceStorageModeShared];
    }
}

void MetalDevice::UploadMemory(const Gs::PageSet& pages, const u8* memory)
{
    for (const auto& run : PageRuns(pages))
    {
        const size_t bytes = static_cast<size_t>(run.second - run.first + 1) * 8192;
        size_t offset = 0;
        id<MTLBuffer> staged = Stage(memory + static_cast<size_t>(run.first) * 8192, bytes, &offset);
        [Blit() copyFromBuffer:staged sourceOffset:offset toBuffer:memory_ destinationOffset:static_cast<size_t>(run.first) * 8192
                          size:bytes];
    }
}

void MetalDevice::ReadMemory(const Gs::PageSet& pages, u8* out)
{
    // Memory's copy is shared: once the GPU is done, its words are there to read
    Submit(true);
    const u8* words = static_cast<const u8*>([memory_ contents]);
    for (const auto& run : PageRuns(pages))
    {
        const size_t at = static_cast<size_t>(run.first) * 8192;
        std::memcpy(out + at, words + at, static_cast<size_t>(run.second - run.first + 1) * 8192);
    }
}

void MetalDevice::MemoryFromTarget(Target& target, const Gs::PageSet& pages)
{
    u32 family = 0;
    LayoutUniforms layout = LayoutFor(target.psm, target.bp, target.bw, target.height / 32, &family);
    layout.pagesDown = static_cast<s32>(target.height / Families[family].pageHeight);
    id<MTLComputeCommandEncoder> encoder = Compute();
    id<MTLComputePipelineState> kernel = Kernel("MemoryFromTarget");
    [encoder setComputePipelineState:kernel];
    [encoder setBuffer:memory_ offset:0 atIndex:0];
    [encoder setBytes:&layout length:sizeof(layout) atIndex:1];
    [encoder setBuffer:inverse_[family] offset:0 atIndex:2];
    id<MTLTexture> texture = TextureOf(target.texture);
    [encoder setTexture:target.depth ? blank_ : texture atIndex:0];
    [encoder setTexture:target.depth ? texture : nil atIndex:1];
    for (const auto& run : PageRuns(pages))
    {
        ConversionUniforms c = {};
        c.storage = target.depth ? 2 : (Is32Class(target.cls) ? 0 : 1);
        c.targetScale = target.scale;
        c.firstWord = run.first * 2048;
        c.words = (run.second - run.first + 1) * 2048;
        [encoder setBytes:&c length:sizeof(c) atIndex:3];
        [encoder dispatchThreads:MTLSizeMake(c.words, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
    }
}

void MetalDevice::TargetFromMemory(Target& target, const Gs::PageSet& pages, const Rect& all)
{
    if (all.right < all.left)
    {
        return;
    }

    u32 family = 0;
    LayoutUniforms layout = LayoutFor(target.psm, target.bp, target.bw, 1, &family);
    layout.pagesDown = static_cast<s32>(target.height / Families[family].pageHeight);
    const u32 n = target.scale;
    ConversionUniforms c = {};
    c.storage = target.depth ? 2 : (Is32Class(target.cls) ? 0 : 1);
    c.targetScale = n;
    PageMask(pages, c.pageMask);
    c.rect[0] = all.left * static_cast<s32>(n);
    c.rect[1] = all.top * static_cast<s32>(n);
    c.rect[2] = (all.right + 1) * static_cast<s32>(n);
    c.rect[3] = (all.bottom + 1) * static_cast<s32>(n);
    id<MTLComputeCommandEncoder> encoder = Compute();
    [encoder setComputePipelineState:Kernel("TargetFromMemory")];
    [encoder setBuffer:memory_ offset:0 atIndex:0];
    [encoder setBytes:&layout length:sizeof(layout) atIndex:1];
    [encoder setBuffer:forward_[family] offset:0 atIndex:2];
    [encoder setBytes:&c length:sizeof(c) atIndex:3];
    id<MTLTexture> texture = TextureOf(target.texture);
    [encoder setTexture:target.depth ? nil : texture atIndex:0];
    [encoder setTexture:target.depth ? texture : nil atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(c.rect[2] - c.rect[0]), static_cast<NSUInteger>(c.rect[3] - c.rect[1]), 1)
       threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
}

void MetalDevice::MergeWrites(const Gs::PageSet& pages, const u8* memory, const std::vector<MergeGroup>& groups)
{
    // Local memory's pages (fresh) staged once, each group merged from memory's copy as the one before left it
    std::vector<std::pair<std::pair<u32, u32>, std::pair<id<MTLBuffer>, size_t>>> staged;
    for (const auto& run : PageRuns(pages))
    {
        size_t offset = 0;
        id<MTLBuffer> buffer = Stage(memory + static_cast<size_t>(run.first) * 8192,
                                     static_cast<size_t>(run.second - run.first + 1) * 8192, &offset);
        staged.push_back({run, {buffer, offset}});
    }

    for (const MergeGroup& group : groups)
    {
        u32 family = 0;
        LayoutUniforms layout = LayoutFor(group.psm, group.bp, group.bw, group.pagesDown, &family);
        id<MTLComputeCommandEncoder> encoder = Compute();
        [encoder setComputePipelineState:Kernel("MergeWrites")];
        [encoder setBuffer:memory_ offset:0 atIndex:0];
        [encoder setBytes:&layout length:sizeof(layout) atIndex:1];
        [encoder setBuffer:inverse_[family] offset:0 atIndex:2];
        for (const auto& run : staged)
        {
            MergeUniforms m = {};
            m.firstWord = run.first.first * 2048;
            m.words = (run.first.second - run.first.first + 1) * 2048;
            m.rectCount = static_cast<u32>(group.rects.size());
            m.unitMask = group.unitMask;
            PageMask(group.pages, m.pageMask);
            for (size_t r = 0; r < group.rects.size(); r++)
            {
                m.rects[r][0] = group.rects[r].left;
                m.rects[r][1] = group.rects[r].top;
                m.rects[r][2] = group.rects[r].right;
                m.rects[r][3] = group.rects[r].bottom;
            }

            [encoder setBytes:&m length:sizeof(m) atIndex:3];
            [encoder setBuffer:run.second.first offset:run.second.second atIndex:4];
            [encoder dispatchThreads:MTLSizeMake(m.words, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        }

        // (each group reads what the one before wrote)
        [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
    }
}

TextureId MetalDevice::MakeTexture(const u32* widths, const u32* heights, u32 levels)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:widths[0]
                                                                                         height:heights[0]
                                                                                      mipmapped:levels > 1];
    descriptor.mipmapLevelCount = levels;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    return Keep([device_ newTextureWithDescriptor:descriptor]);
}

void MetalDevice::UploadTexture(TextureId texture, u32 level, const Rect& rect, const u32* texels, u32 stride)
{
    const u32 width = static_cast<u32>(rect.right - rect.left + 1);
    const u32 height = static_cast<u32>(rect.bottom - rect.top + 1);
    // The rectangle's rows packed into staging, then copied into the level in command order
    std::vector<u32> rows(static_cast<size_t>(width) * height);
    for (u32 y = 0; y < height; y++)
    {
        std::memcpy(&rows[static_cast<size_t>(y) * width], texels + static_cast<size_t>(y) * stride, static_cast<size_t>(width) * 4);
    }

    size_t offset = 0;
    id<MTLBuffer> staged = Stage(rows.data(), rows.size() * 4, &offset);
    [Blit() copyFromBuffer:staged sourceOffset:offset sourceBytesPerRow:width * 4 sourceBytesPerImage:static_cast<size_t>(width) * 4 * height
                sourceSize:MTLSizeMake(width, height, 1) toTexture:TextureOf(texture) destinationSlice:0 destinationLevel:level
         destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(rect.left), static_cast<NSUInteger>(rect.top), 0)];
}

TextureId MetalDevice::MakeImage(u32 width, u32 height, const u32* pixels)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width
                                                                                         height:height mipmapped:YES];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    TextureId id = Keep([device_ newTextureWithDescriptor:descriptor]);
    UploadTexture(id, 0, Rect{0, 0, static_cast<s32>(width) - 1, static_cast<s32>(height) - 1}, pixels, width);
    [Blit() generateMipmapsForTexture:TextureOf(id)];
    return id;
}

void MetalDevice::DecodeLevels(TextureId texture, const DecodeJob& job)
{
    if (job.indexed && (clutVersion_ != job.clutVersion || clutCommands_ != commands_ || commands_ == nil))
    {
        clutBuffer_ = Stage(job.clut, 512 * 2, &clutOffset_);
        clutVersion_ = job.clutVersion;
        clutCommands_ = Commands();
    }

    id<MTLTexture> levels = TextureOf(texture);
    for (u32 i = 0; i < job.levelCount; i++)
    {
        const Rect& rect = job.rects[i];
        if (rect.right < rect.left)
        {
            continue;
        }

        const Gs::TextureLevel& lv = job.levels[i];
        u32 family = 0;
        LayoutUniforms layout = LayoutFor(lv.psm, lv.bp, lv.bw, 1, &family);
        DecodeUniforms d = {};
        d.psm = static_cast<s32>(lv.psm);
        d.cpsm = static_cast<s32>(job.cpsm);
        d.csa = static_cast<s32>(job.csa);
        d.ta0 = job.ta0;
        d.ta1 = job.ta1;
        d.aem = job.aem;
        d.rect[0] = rect.left;
        d.rect[1] = rect.top;
        d.rect[2] = rect.right;
        d.rect[3] = rect.bottom;
        id<MTLTexture> view = levels.mipmapLevelCount > 1
                                  ? [levels newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D
                                                                   levels:NSMakeRange(i, 1) slices:NSMakeRange(0, 1)]
                                  : levels;
        id<MTLComputeCommandEncoder> encoder = Compute();
        [encoder setComputePipelineState:Kernel("DecodeLevel")];
        [encoder setBuffer:memory_ offset:0 atIndex:0];
        [encoder setBytes:&layout length:sizeof(layout) atIndex:1];
        [encoder setBuffer:forward_[family] offset:0 atIndex:2];
        [encoder setBytes:&d length:sizeof(d) atIndex:3];
        if (job.indexed)
        {
            [encoder setBuffer:clutBuffer_ offset:clutOffset_ atIndex:4];
        }
        else
        {
            [encoder setBuffer:memory_ offset:0 atIndex:4];
        }

        [encoder setTexture:view atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(rect.right - rect.left + 1),
                                             static_cast<NSUInteger>(rect.bottom - rect.top + 1), 1)
           threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    }
}

TextureId MetalDevice::MakeUpscaled(TextureId source, u32 width, u32 height, u32 scale)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:width * scale
                                                                                         height:height * scale
                                                                                      mipmapped:YES];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> out = [device_ newTextureWithDescriptor:descriptor];
    id<MTLTexture> top = [out newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D
                                                     levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
    const f32 factor = static_cast<f32>(scale);
    id<MTLComputeCommandEncoder> encoder = Compute();
    [encoder setComputePipelineState:Kernel("Upscale")];
    [encoder setTexture:TextureOf(source) atIndex:0];
    [encoder setTexture:top atIndex:1];
    [encoder setBytes:&factor length:sizeof(factor) atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(width * scale, height * scale, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [Blit() generateMipmapsForTexture:out];
    return Keep(out);
}

void MetalDevice::DrawBatch(const BatchKey& k, const std::vector<HwVertex>& vertexes, const std::vector<BatchShape>&)
{
    // The depth as the batch begins, for the test at the GS's sample point
    id<MTLTexture> depthCopy = blankWords_;
    const bool nativeDepth = (k.program & BitNativeDepth) && k.depth != nullptr;
    if (nativeDepth)
    {
        id<MTLTexture> depth = TextureOf(k.depth->texture);
        __strong id<MTLTexture>& copy = depthCopies_[{static_cast<u32>(depth.width), static_cast<u32>(depth.height)}];
        if (copy == nil)
        {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                                                                                  width:depth.width
                                                                                                 height:depth.height
                                                                                              mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            copy = [device_ newTextureWithDescriptor:descriptor];
        }

        [Blit() copyFromTexture:depth toTexture:copy];
        depthCopy = copy;
    }

    id<MTLRenderCommandEncoder> encoder = RenderInto(k.colour, k.depth);
    const u32 n = k.colour->scale;
    const u32 width = k.colour->width * n;
    const u32 height = k.colour->height * n;
    [encoder setRenderPipelineState:DrawPipeline(k.depth != nullptr)];

    VertexUniforms v = {};
    v.viewSize[0] = static_cast<f32>(width);
    v.viewSize[1] = static_cast<f32>(height);
    v.pointSize = static_cast<f32>(n);
    v.perPrimitive = k.primitive == Topology::Triangles ? 3 : k.primitive == Topology::Lines ? 2 : 1;
    v.zmax = k.exact.zmax;
    size_t offset = 0;
    id<MTLBuffer> buffer = Stage(vertexes.data(), vertexes.size() * sizeof(HwVertex), &offset);
    [encoder setVertexBuffer:buffer offset:offset atIndex:0];
    [encoder setVertexBytes:&v length:sizeof(v) atIndex:1];

    DrawUniforms u = {};
    u.texSize[0] = k.gsTexWidth;
    u.texSize[1] = k.gsTexHeight;
    u.texActual[0] = k.texWidth;
    u.texActual[1] = k.texHeight;
    u.texSnap = static_cast<f32>(k.texSnap);
    u.targetScale = static_cast<s32>(n);
    u.lodK = k.lodK;
    u.lodL = std::exp2(static_cast<f32>(k.lodL));
    u.lodMax = static_cast<f32>(k.mxl);
    u.ta0 = k.ta0;
    u.ta1 = k.ta1;
    u.aem = k.aem;
    u.fogR = k.fogColour & 0xFF;
    u.fogG = (k.fogColour >> 8) & 0xFF;
    u.fogB = (k.fogColour >> 16) & 0xFF;
    const BatchKey::Exact& e = k.exact;
    u.frameKind = e.frameKind;
    u.fbmsk = e.fbmsk;
    u.frameWrite = e.frameWrite;
    u.depthKind = k.depth != nullptr ? e.depthKind : 0;
    u.ztst = e.ztst;
    u.zWrite = k.depth != nullptr ? e.zWrite : 0;
    u.zmax = e.zmax;
    u.alphaTest = e.alphaTest;
    u.atst = e.atst;
    u.aref = e.aref;
    u.afail = e.afail;
    u.date = e.date;
    u.datm = e.datm;
    u.blend = e.blend;
    u.blendA = e.blendA;
    u.blendB = e.blendB;
    u.blendC = e.blendC;
    u.blendD = e.blendD;
    u.blendFix = e.blendFix;
    u.pabe = e.pabe;
    u.colclamp = e.colclamp;
    u.fba = e.fba;
    u.dither = (k.program & BitDither) ? 1 : 0;
    u.ditherScale = k.ditherScale != 0 ? k.ditherScale : 1;
    u.levelCount = k.exact.levelCount;
    u.magLinear = k.exact.magLinear;
    u.mmin = k.exact.mmin;
    u.mxl = k.exact.mxl;
    u.textured = (k.program & BitTextured) ? 1 : 0;
    u.tfx = (k.program >> BitTfxShift) & 3;
    u.tcc = (k.program & BitTcc) ? 1 : 0;
    u.fog = (k.program & BitFog) ? 1 : 0;
    u.uvMode = (k.program & BitUv) ? 1 : 0;
    u.texAlpha = (k.program >> BitTexAlphaShift) & 3;
    u.mipmap = (k.program & BitMipmap) ? 1 : 0;
    u.lodConst = (k.program & BitLodConstant) ? 1 : 0;
    u.nativeTexel = (k.program & BitNativeTexel) ? 1 : 0;
    u.region = (k.program & BitRegion) ? 1 : 0;
    u.gsSample = u.textured != 0 && k.exact.gsSample != 0 ? 1 : 0;
    u.nativeZtst = nativeDepth ? k.nativeDepthTest : 0;
    for (u32 i = 0; i < 16; i++)
    {
        s32 value = static_cast<s32>((k.dimx >> (i * 4)) & 7);
        u.dimx[i] = value & 4 ? value - 8 : value;
    }

    for (u32 i = 0; i < 4; i++)
    {
        u.regionMode[i] = static_cast<s32>(k.regionMode[i]);
        u.regionBounds[i] = static_cast<s32>(k.regionBounds[i]);
    }

    [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];
    id<MTLTexture> texture = k.texture != 0 ? TextureOf(k.texture) : blank_;
    [encoder setFragmentTexture:texture atIndex:0];
    [encoder setFragmentTexture:depthCopy atIndex:1];
    [encoder setFragmentSamplerState:SamplerFor(k) atIndex:0];

    // The scissor at the scale, inside the target
    const s32 left = std::clamp(k.scissor[0] * static_cast<s32>(n), 0, static_cast<s32>(width));
    const s32 top = std::clamp(k.scissor[1] * static_cast<s32>(n), 0, static_cast<s32>(height));
    const s32 right = std::clamp((k.scissor[2] + 1) * static_cast<s32>(n), 0, static_cast<s32>(width));
    const s32 bottom = std::clamp((k.scissor[3] + 1) * static_cast<s32>(n), 0, static_cast<s32>(height));
    if (right <= left || bottom <= top)
    {
        return;
    }

    MTLScissorRect scissor = {static_cast<NSUInteger>(left), static_cast<NSUInteger>(top), static_cast<NSUInteger>(right - left),
                              static_cast<NSUInteger>(bottom - top)};
    [encoder setScissorRect:scissor];
    const MTLPrimitiveType type = k.primitive == Topology::Triangles ? MTLPrimitiveTypeTriangle
                                  : k.primitive == Topology::Lines  ? MTLPrimitiveTypeLine
                                                                    : MTLPrimitiveTypePoint;
    [encoder drawPrimitives:type vertexStart:0 vertexCount:vertexes.size()];
    if (++commandsDraws_ >= 2048 || CFAbsoluteTimeGetCurrent() - commandsOpened_ > 0.008)
    {
        Submit(false);
    }
}

TextureId MetalDevice::MergeCircuits(const CircuitPicture* circuits, u32 width, u32 height, f32 alp, bool ownAlpha, u32 background)
{
    id<MTLTexture> out = TextureOf(merged_);
    if (out == nil || out.width != width || out.height != height)
    {
        FreeTexture(merged_);
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width
                                                                                             height:height mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        descriptor.storageMode = MTLStorageModePrivate;
        out = [device_ newTextureWithDescriptor:descriptor];
        merged_ = Keep(out);
    }

    CircuitsUniforms m = {};
    for (u32 i = 0; i < 2; i++)
    {
        m.circuit[i].enabled = circuits[i].enabled ? 1 : 0;
        std::memcpy(m.circuit[i].place, circuits[i].place, sizeof(m.circuit[i].place));
        std::memcpy(m.circuit[i].corner, circuits[i].corner, sizeof(m.circuit[i].corner));
    }

    m.alp = alp;
    m.ownAlpha = ownAlpha ? 1 : 0;
    m.background[0] = static_cast<f32>(background & 0xFF) / 255.0f;
    m.background[1] = static_cast<f32>((background >> 8) & 0xFF) / 255.0f;
    m.background[2] = static_cast<f32>((background >> 16) & 0xFF) / 255.0f;
    id<MTLComputeCommandEncoder> encoder = Compute();
    [encoder setComputePipelineState:Kernel("MergeCircuits")];
    [encoder setTexture:circuits[0].enabled ? TextureOf(circuits[0].target->texture) : TextureOf(circuits[1].target->texture) atIndex:0];
    [encoder setTexture:circuits[1].enabled ? TextureOf(circuits[1].target->texture) : TextureOf(circuits[0].target->texture) atIndex:1];
    [encoder setTexture:out atIndex:2];
    [encoder setBytes:&m length:sizeof(m) atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    return merged_;
}

void MetalDevice::ReadPicture(const Picture& picture, std::vector<u32>& pixels)
{
    pixels.resize(static_cast<size_t>(picture.width) * picture.height);
    id<MTLTexture> texture = picture.target != nullptr ? TextureOf(picture.target->texture) : TextureOf(merged_);
    ReadTexture(texture, static_cast<u32>(picture.left), static_cast<u32>(picture.top), picture.width, picture.height, pixels.data());
}

bool MetalDevice::MakeHandOver(HandOver& h, u32 width, u32 height)
{
    constexpr Hw::GLenum TextureRectangle = 0x84F5, Bgra = 0x80E1, UnsignedInt8888Rev = 0x8367;
    Hw::Gl& gl = Hw::GetGl();
    if (h.surface != nullptr)
    {
        CFRelease(h.surface);
        h.surface = nullptr;
        h.metal = nil;
    }

    const u32 format = 'BGRA';
    NSDictionary* properties = @{
        (NSString*)kIOSurfaceWidth : @(width),
        (NSString*)kIOSurfaceHeight : @(height),
        (NSString*)kIOSurfaceBytesPerElement : @4,
        (NSString*)kIOSurfacePixelFormat : @(format),
    };
    h.surface = IOSurfaceCreate((__bridge CFDictionaryRef)properties);
    if (h.surface == nullptr)
    {
        std::fprintf(stderr, "graphics: Metal renderer: no IOSurface for the window's picture\n");
        return false;
    }

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width
                                                                                         height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    h.metal = [device_ newTextureWithDescriptor:descriptor iosurface:h.surface plane:0];
    if (h.rectangle == 0)
    {
        gl.GenTextures(1, &h.rectangle);
        gl.GenFramebuffers(1, &h.rectangleFbo);
        gl.GenTextures(1, &h.texture);
        gl.GenFramebuffers(1, &h.fbo);
    }

    gl.BindTexture(TextureRectangle, h.rectangle);
    if (CGLTexImageIOSurface2D(CGLGetCurrentContext(), TextureRectangle, Hw::GL_RGBA8, static_cast<Hw::GLsizei>(width), static_cast<Hw::GLsizei>(height), Bgra,
                               UnsignedInt8888Rev, h.surface, 0) != kCGLNoError)
    {
        std::fprintf(stderr, "graphics: Metal renderer: OpenGL can't see the window's IOSurface\n");
        return false;
    }

    gl.BindFramebuffer(Hw::GL_FRAMEBUFFER, h.rectangleFbo);
    gl.FramebufferTexture2D(Hw::GL_FRAMEBUFFER, Hw::GL_COLOR_ATTACHMENT0, TextureRectangle, h.rectangle, 0);
    gl.BindTexture(Hw::GL_TEXTURE_2D, h.texture);
    gl.TexParameteri(Hw::GL_TEXTURE_2D, Hw::GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexParameteri(Hw::GL_TEXTURE_2D, Hw::GL_TEXTURE_WRAP_S, static_cast<Hw::GLint>(Hw::GL_CLAMP_TO_EDGE));
    gl.TexParameteri(Hw::GL_TEXTURE_2D, Hw::GL_TEXTURE_WRAP_T, static_cast<Hw::GLint>(Hw::GL_CLAMP_TO_EDGE));
    gl.TexImage2D(Hw::GL_TEXTURE_2D, 0, static_cast<Hw::GLint>(Hw::GL_RGBA8), static_cast<Hw::GLsizei>(width), static_cast<Hw::GLsizei>(height), 0, Hw::GL_RGBA,
                  Hw::GL_UNSIGNED_BYTE, nullptr);
    gl.BindFramebuffer(Hw::GL_FRAMEBUFFER, h.fbo);
    gl.FramebufferTexture2D(Hw::GL_FRAMEBUFFER, Hw::GL_COLOR_ATTACHMENT0, Hw::GL_TEXTURE_2D, h.texture, 0);
    gl.BindFramebuffer(Hw::GL_FRAMEBUFFER, 0);
    h.width = width;
    h.height = height;
    return true;
}

bool MetalDevice::CopyPicture(const Picture& picture, u32 slot, void* drawnFence, ShownTexture* shown)
{
    // The render thread's OpenGL context is current (display.cpp keeps it so with Metal): without it, no hand-over (the
    // window reads the picture back instead)
    if (CGLGetCurrentContext() == nullptr || !Hw::LoadGl())
    {
        return false;
    }

    HandOver& h = handOver_[slot % 3];
    if ((h.surface == nullptr || h.width != picture.width || h.height != picture.height) && !MakeHandOver(h, picture.width, picture.height))
    {
        return false;
    }

    id<MTLTexture> source = picture.target != nullptr ? TextureOf(picture.target->texture) : TextureOf(merged_);
    const u32 origin[2] = {static_cast<u32>(picture.left), static_cast<u32>(picture.top)};
    id<MTLComputeCommandEncoder> encoder = Compute();
    [encoder setComputePipelineState:Kernel("CopyPicture")];
    [encoder setTexture:source atIndex:0];
    [encoder setTexture:h.metal atIndex:1];
    [encoder setBytes:origin length:sizeof(origin) atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(picture.width, picture.height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    // (Metal's writes done before OpenGL reads the surface: the IOSurface has no synchronisation between the two)
    Submit(true);

    Hw::Gl& gl = Hw::GetGl();
    if (drawnFence != nullptr)
    {
        Hw::GLsync drawn = static_cast<Hw::GLsync>(drawnFence);
        gl.WaitSync(drawn, 0, Hw::GL_TIMEOUT_IGNORED);
        gl.DeleteSync(drawn);
    }

    gl.BindFramebuffer(Hw::GL_READ_FRAMEBUFFER, h.rectangleFbo);
    gl.BindFramebuffer(Hw::GL_DRAW_FRAMEBUFFER, h.fbo);
    gl.Disable(Hw::GL_SCISSOR_TEST);
    gl.BlitFramebuffer(0, 0, static_cast<Hw::GLint>(h.width), static_cast<Hw::GLint>(h.height), 0, 0, static_cast<Hw::GLint>(h.width),
                       static_cast<Hw::GLint>(h.height), Hw::GL_COLOR_BUFFER_BIT, Hw::GL_NEAREST);
    gl.BindFramebuffer(Hw::GL_FRAMEBUFFER, 0);
    shown->texture = h.texture;
    shown->width = h.width;
    shown->height = h.height;
    shown->fence = gl.FenceSync(Hw::GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.Flush();
    if (!handedOver_)
    {
        handedOver_ = true;
        std::fprintf(stderr, "graphics: Metal renderer: the picture goes to the window through an IOSurface\n");
    }

    return true;
}

void MetalDevice::PictureDone()
{
    // The vblank's work to the GPU (nothing waits for it here)
    Submit(false);
}
}

std::unique_ptr<GpuDevice> MakeMetalDevice(HardwareStats& stats, const Gs::Gs& gs)
{
    return std::make_unique<MetalDevice>(stats, gs);
}
}
