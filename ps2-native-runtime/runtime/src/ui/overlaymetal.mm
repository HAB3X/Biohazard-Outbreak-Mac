// The overlay's Metal device (overlaydevice.h): its list of draws drawn into the Metal presenter's render pass (presentmetal.mm).
// The shader is overlaygl.cpp's GLSL program in Metal's language, line for line (GLSL's mod is x - y * floor(x / y): Mod here)
#include "overlaymetal.h"

#include "../runtime.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Ps2::Ui
{
namespace
{
constexpr const char* OverlayShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms
{
    float4 rect;
    float4 fillTop;
    float4 fillBottom;
    float4 borderColour;
    float4 tint;
    float2 viewport;
    float2 offset;
    float radius;
    float border;
    float blur;
    float scale;
    float time;
    int mode;
};

struct Vertex
{
    float2 position;
    float2 uv;
};

struct Varyings
{
    float4 position [[position]];
    float2 pixel;
    float2 texel;
};

vertex Varyings OverlayVertex(uint id [[vertex_id]], device const Vertex* vertexes [[buffer(0)]], constant Uniforms& u [[buffer(1)]])
{
    Vertex v = vertexes[id];
    Varyings out;
    out.pixel = v.position;
    out.texel = v.uv;
    out.position = float4(v.position.x / u.viewport.x * 2.0 - 1.0, 1.0 - v.position.y / u.viewport.y * 2.0, 0.0, 1.0);
    return out;
}

float Mod(float x, float y)
{
    return x - y * floor(x / y);
}

float RoundBox(float2 p, float2 halfSize, float r)
{
    float2 q = abs(p) - halfSize + float2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 Over(float4 under, float3 c, float a)
{
    return float4(mix(under.rgb, c, a), under.a);
}

// The distance (in the spiral's own units) to an arm r = a + b * theta, theta from 0 to its end, its phase
float ArmDistance(float r, float angle, float a, float b, float phase, float end)
{
    const float tau = 6.28318530718;
    float base = Mod(angle - phase, tau);
    float best = 1e9;
    for (int k = 0; k < 4; k++)
    {
        float theta = base + tau * float(k);
        if (theta <= end)
        {
            best = min(best, abs(r - (a + b * theta)));
        }
    }

    return best;
}

fragment float4 OverlayFragment(Varyings in [[stage_in]], constant Uniforms& u [[buffer(0)]], texture2d<float> atlas [[texture(0)]],
                                sampler smp [[sampler(0)]])
{
    float2 pixel = in.pixel;
    float2 texel = in.texel;
    if (u.mode == 0)
    {
        // A rounded box: its fill (a vertical gradient), its border, an edge softened by blur (a shadow)
        float2 centre = u.rect.xy + u.rect.zw * 0.5;
        float d = RoundBox(pixel - centre, u.rect.zw * 0.5, u.radius);
        float t = clamp((pixel.y - u.rect.y) / max(u.rect.w, 1.0), 0.0, 1.0);
        float4 fill = u.fillBottom.a < 0.0 ? u.fillTop : mix(u.fillTop, u.fillBottom, t);
        if (u.blur > 0.0)
        {
            float a = 1.0 - smoothstep(-u.blur, u.blur, d);
            return float4(fill.rgb, fill.a * a);
        }

        float coverage = clamp(0.5 - d, 0.0, 1.0);
        float4 c = fill;
        if (u.border > 0.0)
        {
            float edge = clamp(d + u.border + 0.5, 0.0, 1.0);
            c = mix(fill, u.borderColour, edge);
        }

        return float4(c.rgb, c.a * coverage);
    }
    else if (u.mode == 1)
    {
        return float4(u.tint.rgb, u.tint.a * atlas.sample(smp, texel).r);
    }
    else if (u.mode == 2)
    {
        // The options' backdrop in the reference space
        float2 q = (pixel - u.offset) / u.scale;
        float2 direction = normalize(float2(1.0, 1.0));
        float lengthAlong = 1280.0 * 0.70710678 + 720.0 * 0.70710678;
        float g = clamp(dot(q - float2(640.0, 360.0), direction) / lengthAlong + 0.5, 0.0, 1.0);
        float3 c0 = float3(14.0, 95.0, 184.0) / 255.0;
        float3 c1 = float3(10.0, 59.0, 122.0) / 255.0;
        float3 c2 = float3(7.0, 31.0, 69.0) / 255.0;
        float3 base = g < 0.55 ? mix(c0, c1, g / 0.55) : mix(c1, c2, (g - 0.55) / 0.45);
        float4 c = float4(base, 1.0);
        // The ring target (bands of 70)
        float ringDistance = length(q - float2(100.0, 460.0));
        if (ringDistance < 320.0)
        {
            bool light = Mod(floor(ringDistance / 70.0), 2.0) < 1.0;
            float3 band = light ? float3(232.0, 238.0, 246.0) / 255.0 : float3(22.0, 118.0, 210.0) / 255.0;
            float edge = clamp(320.0 - ringDistance, 0.0, 1.0);
            c = Over(c, band, 0.18 * edge);
        }

        // The spiral: the svg's 1000 units over 1300 reference pixels, its middle at (270, 350), turning a turn in 90 seconds
        float2 s = (q - float2(270.0, 350.0)) / 1.3;
        float r = length(s);
        float angle = atan2(s.y, s.x) - u.time * 6.28318530718 / 90.0;
        float pixelInSvg = 1.0 / (u.scale * 1.3);
        float end = 2.2 * 6.28318530718;
        float white = min(ArmDistance(r, angle, 10.0, 35.4475, 0.0, end), ArmDistance(r, angle, 10.0, 35.4475, 3.14159265, end));
        float whiteCover = clamp((35.0 - white) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, float3(1.0), 0.18 * 0.7 * whiteCover);
        float blue = min(ArmDistance(r, angle, 9.854, 35.4477, 0.4456, end), ArmDistance(r, angle, 9.854, 35.4477, 3.5872, end));
        float blueCover = clamp((11.0 - blue) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, float3(159.0, 182.0, 216.0) / 255.0, 0.25 * 0.7 * blueCover);
        return c;
    }
    else if (u.mode == 3)
    {
        float4 t = atlas.sample(smp, texel);
        return float4(t.rgb, t.a * u.tint.a);
    }
    else if (u.mode == 5)
    {
        // A PlayStation face button: a dark round button, its shape in its colour (radius: 0 cross, 1 circle, 2 square, 3 triangle)
        float2 p = (texel - float2(0.5)) * 2.0;
        float d = length(p);
        float button = clamp((0.96 - d) / fwidth(d), 0.0, 1.0);
        float3 c = mix(float3(0.07, 0.08, 0.12), float3(0.18, 0.20, 0.26), clamp(0.6 - p.y * 0.5, 0.0, 1.0));
        int shape = int(u.radius + 0.5);
        float stroke;
        float3 ink;
        if (shape == 0)
        {
            float2 q = abs(p);
            stroke = min(abs(q.x - q.y), 1e9) * 0.7071 - 0.09;
            stroke = max(stroke, max(q.x, q.y) - 0.48);
            ink = float3(0.49, 0.71, 0.95);
        }
        else if (shape == 1)
        {
            stroke = abs(d - 0.42) - 0.09;
            ink = float3(1.0, 0.40, 0.42);
        }
        else if (shape == 2)
        {
            float2 q = abs(p);
            stroke = abs(max(q.x, q.y) - 0.38) - 0.08;
            ink = float3(0.98, 0.55, 0.85);
        }
        else
        {
            float2 q = float2(abs(p.x), p.y + 0.08);
            float tri = max(q.x * 0.866 + q.y * 0.5, -q.y) - 0.27;
            stroke = abs(tri) - 0.08;
            ink = float3(0.30, 0.86, 0.70);
        }

        float inkCover = clamp(-stroke / fwidth(stroke), 0.0, 1.0);
        c = mix(c, ink, inkCover);
        return float4(c, button * u.tint.a);
    }

    // A fruit drawn when there's no art: an orange ball, its dark edge and a shine
    float2 p = (texel - float2(0.5)) * 2.0;
    float d = length(p);
    float edge = clamp((1.0 - d) * 24.0, 0.0, 1.0);
    float3 orange = mix(float3(1.0, 0.72, 0.25), float3(0.85, 0.32, 0.02), clamp(length(p - float2(-0.35, -0.35)) * 0.8, 0.0, 1.0));
    float3 c = mix(float3(0.23, 0.07, 0.0), orange, clamp((0.88 - d) * 24.0, 0.0, 1.0));
    float shine = clamp((0.22 - length(p - float2(-0.35, -0.4))) * 20.0, 0.0, 1.0);
    c = mix(c, float3(1.0), shine * 0.8);
    return float4(c, edge * u.tint.a);
}
)";

// The vertex buffers' ring: one a frame in flight (the presenter keeps at most this many)
constexpr u32 RingSize = 3;

class MetalOverlayDevice : public MetalOverlay
{
public:
    bool Init(id<MTLDevice> device, MTLPixelFormat format);
    OverlayTexture MakeTexture(s32 width, s32 height, bool oneChannel, const void* texels) override;
    void Draw(const OverlayDrawList& list) override;
    void Begin(id<MTLRenderCommandEncoder> encoder, u32 frame) override
    {
        encoder_ = encoder;
        frame_ = frame;
    }

private:
    id<MTLDevice> device_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLSamplerState> sampler_ = nil;
    id<MTLTexture> blank_ = nil;
    std::vector<id<MTLTexture>> textures_;
    id<MTLBuffer> vertexes_[RingSize] = {};
    id<MTLRenderCommandEncoder> encoder_ = nil;
    u32 frame_ = 0;
};

bool MetalOverlayDevice::Init(id<MTLDevice> device, MTLPixelFormat format)
{
    device_ = device;
    NSError* error = nil;
    id<MTLLibrary> library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:OverlayShaderSource] options:nil error:&error];
    if (library == nil)
    {
        Log("overlay: the Metal shader didn't compile: %s", [[error localizedDescription] UTF8String]);
        return false;
    }

    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"OverlayVertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"OverlayFragment"];
    MTLRenderPipelineColorAttachmentDescriptor* colour = descriptor.colorAttachments[0];
    colour.pixelFormat = format;
    colour.blendingEnabled = YES;
    colour.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    colour.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    colour.sourceAlphaBlendFactor = MTLBlendFactorOne;
    colour.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline_ = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (pipeline_ == nil)
    {
        Log("overlay: the Metal pipeline: %s", [[error localizedDescription] UTF8String]);
        return false;
    }

    MTLSamplerDescriptor* sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = MTLSamplerMinMagFilterLinear;
    sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_ = [device_ newSamplerStateWithDescriptor:sampler];
    // Bound for the draws without a texture (every argument the shader names is bound)
    MTLTextureDescriptor* blank = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1
                                                                                 mipmapped:NO];
    blank_ = [device_ newTextureWithDescriptor:blank];
    const u8 zero = 0;
    [blank_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&zero bytesPerRow:1];
    return true;
}

OverlayTexture MetalOverlayDevice::MakeTexture(s32 width, s32 height, bool oneChannel, const void* texels)
{
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:oneChannel ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA8Unorm
                                     width:static_cast<NSUInteger>(width)
                                    height:static_cast<NSUInteger>(height)
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (texture == nil)
    {
        Log("overlay: no Metal texture of %dx%d", width, height);
        return 0;
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height)) mipmapLevel:0
                 withBytes:texels
               bytesPerRow:static_cast<NSUInteger>(width) * (oneChannel ? 1 : 4)];
    textures_.push_back(texture);
    return static_cast<OverlayTexture>(textures_.size());
}

void MetalOverlayDevice::Draw(const OverlayDrawList& list)
{
    if (list.draws.empty())
    {
        return;
    }

    const size_t bytes = list.vertexes.size() * sizeof(OverlayVertex);
    __strong id<MTLBuffer>& buffer = vertexes_[frame_ % RingSize];
    if (buffer == nil || buffer.length < bytes)
    {
        buffer = [device_ newBufferWithLength:std::max<size_t>(bytes * 2, 64 * 1024) options:MTLResourceStorageModeShared];
    }

    std::memcpy([buffer contents], list.vertexes.data(), bytes);
    id<MTLRenderCommandEncoder> encoder = encoder_;
    [encoder setViewport:MTLViewport{0, 0, static_cast<double>(list.width), static_cast<double>(list.height), 0, 1}];
    [encoder setRenderPipelineState:pipeline_];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    [encoder setFragmentSamplerState:sampler_ atIndex:0];
    const MTLScissorRect whole = {0, 0, static_cast<NSUInteger>(list.width), static_cast<NSUInteger>(list.height)};
    for (const OverlayDraw& draw : list.draws)
    {
        [encoder setVertexBytes:&draw.uniforms length:sizeof(draw.uniforms) atIndex:1];
        [encoder setFragmentBytes:&draw.uniforms length:sizeof(draw.uniforms) atIndex:0];
        id<MTLTexture> texture = draw.texture != 0 && draw.texture <= textures_.size() ? textures_[draw.texture - 1] : blank_;
        [encoder setFragmentTexture:texture atIndex:0];
        MTLScissorRect scissor = whole;
        if (draw.clipped)
        {
            // The clip's pixels as OpenGL's scissor takes them (whole pixels from the bottom left), inside the window (Metal's must
            // be)
            const s32 width = list.width, height = list.height;
            const s32 left = static_cast<s32>(draw.clip[0]);
            const s32 bottom = static_cast<s32>(static_cast<f32>(height) - draw.clip[1] - draw.clip[3]);
            const s32 right = left + static_cast<s32>(draw.clip[2]);
            const s32 top = height - (bottom + static_cast<s32>(draw.clip[3]));
            const s32 x0 = std::clamp(left, 0, width), x1 = std::clamp(right, x0, width);
            const s32 y0 = std::clamp(top, 0, height), y1 = std::clamp(height - bottom, y0, height);
            scissor = {static_cast<NSUInteger>(x0), static_cast<NSUInteger>(y0), static_cast<NSUInteger>(x1 - x0),
                       static_cast<NSUInteger>(y1 - y0)};
        }

        if (scissor.width == 0 || scissor.height == 0)
        {
            continue;
        }

        [encoder setScissorRect:scissor];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:draw.first vertexCount:draw.count];
    }

    [encoder setScissorRect:whole];
}
}

std::unique_ptr<MetalOverlay> MakeMetalOverlay(id<MTLDevice> device, MTLPixelFormat format)
{
    auto overlay = std::make_unique<MetalOverlayDevice>();
    if (!overlay->Init(device, format))
    {
        return nullptr;
    }

    return overlay;
}
}
