// The hardware renderer's OpenGL device (gpudevice.h): OpenGL 3.3 core, which macOS runs over Metal. Moved out of hwgs.cpp
// unchanged; its notes on how the GS's rules are carried over to OpenGL are hwgs.cpp's top comment.
//
// What OpenGL's fixed functions can't make is made in the shader from a copy of the target ("shader blend"), the destination
// alpha test by a stencil made in a pass of its own, a failed alpha test's AFAIL by a second pass, wrapping sums into 16 bit
// frames in a float buffer resolved later; depth is a float of the GS's value over 2^32 (a 32 bit buffer near the top kept
// reversed). GS memory's copy is a 1024x1024 R32UI texture of its words.

#include "gpudevice.h"

#include "gl.h"

#include "../png.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

namespace NativeGraphics
{
// The presenter's window and context (display.cpp), shared when there is one; the render thread's own (on it)
bool PresenterContext(SDL_Window** window, SDL_GLContext* context);
bool RenderContext(SDL_Window** window, SDL_GLContext* context);

namespace
{
using namespace Hw;

constexpr const char* VertexSource = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in uint aZ;
layout(location = 2) in vec4 aColour;
layout(location = 3) in vec3 aStq;
layout(location = 4) in float aFog;
uniform vec2 viewSize;
uniform vec2 viewOrigin;
uniform float zMax;
uniform uint zMaxU;
uniform int depthReverse;
uniform float pointSize;
noperspective out vec4 vColour;
noperspective out vec3 vStq;
noperspective out float vFog;
noperspective out float vDepth;
void main()
{
    gl_Position = vec4((aPos + viewOrigin) / viewSize * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = pointSize;
    vColour = aColour;
    vStq = aStq;
    vFog = aFog;
    // Depth as a float of the GS's value over 2^32 (exact below 2^24), or for a reversed target 2^32 - 1 less it (exact near
    // the top)
    vDepth = depthReverse != 0 ? float(0xFFFFFFFFu - min(aZ, zMaxU)) * (1.0 / 4294967296.0) : min(float(aZ), zMax) * (1.0 / 4294967296.0);
}
)";

constexpr const char* FragmentSource = R"(
noperspective in vec4 vColour;
noperspective in vec3 vStq;
noperspective in float vFog;
noperspective in float vDepth;
layout(location = 0, index = 0) out vec4 outColour;
layout(location = 0, index = 1) out vec4 outFactor;
uniform sampler2D tex;
uniform vec2 texSize;
uniform vec2 texActual;
uniform float texSnap;
uniform float aref;
uniform vec3 fogColour;
uniform float ta0;
uniform float ta1;
uniform int aem;
uniform float lodK;
uniform float lodL;
uniform float lodMax;
uniform int dimx[16];
uniform float scale;
uniform sampler2D destination;
uniform int blendOn;
uniform int blendA;
uniform int blendB;
uniform int blendC;
uniform int blendD;
uniform float blendFix;
uniform int pabe;
uniform int colclamp;
uniform int datm;
uniform uvec4 frameMask;
uniform int depthReverse;
uniform sampler2D depthCopy;
uniform int nativeZtst;
uniform int targetScale;
uniform ivec4 regionMode;
uniform ivec4 regionBounds;
vec3 Pick(int select, vec3 source, vec3 destination)
{
    return select == 0 ? source : select == 1 ? destination : vec3(0.0);
}
#if REGION
// A texel coordinate wrapped by CLAMP's mode as the software GS wraps it (its region modes kept inside the level)
int RegionWrap(int c, int mode, int size, int low, int high)
{
    return mode == 0 ? c & (size - 1) : mode == 1 ? clamp(c, 0, size - 1)
                                      : mode == 2 ? min(c < low ? low : high < c ? high : c, size - 1)
                                                  : min((c & low) | high, size - 1);
}
// A sample of the texture (n samples a texel across and down): its texel wrapped, a clamped texel's samples clamped to its
// edge's, as the software GS reads a shadow
vec4 RegionTexel(ivec2 at)
{
    int n = regionMode.w;
    ivec2 texel = ivec2(floor(vec2(at) / float(n)));
    ivec2 within = at - texel * n;
    ivec2 size = ivec2(texSize);
    ivec2 wrapped = ivec2(RegionWrap(texel.x, regionMode.x, size.x, regionBounds.x, regionBounds.y),
                          RegionWrap(texel.y, regionMode.y, size.y, regionBounds.z, regionBounds.w));
    if ((regionMode.x == 1 || regionMode.x == 2) && wrapped.x != texel.x)
    {
        within.x = texel.x < wrapped.x ? 0 : n - 1;
    }
    if ((regionMode.y == 1 || regionMode.y == 2) && wrapped.y != texel.y)
    {
        within.y = texel.y < wrapped.y ? 0 : n - 1;
    }
    return floor(texelFetch(tex, wrapped * n + within, 0) * 255.0 + 0.5);
}
// The GS's bilinear step: a + (b - a) * f >> 4
vec4 Lerp4(vec4 a, vec4 b, float f)
{
    return a + floor((b - a) * f / 16.0);
}
// The texture read as the GS reads it with a region clamp or repeat: the sample at the coordinates, or the four round them
// (half a sample off) weighted by 4 bit fractions, each wrapped on its own
vec4 RegionSample(vec2 uv)
{
    vec2 at = uv * float(regionMode.w);
    if (regionMode.z == 0)
    {
        return RegionTexel(ivec2(floor(at)));
    }
    ivec2 sixteenths = ivec2(floor((at - 0.5) * 16.0));
    ivec2 i = sixteenths >> 4;
    vec2 f = vec2(sixteenths & 15);
    vec4 top = Lerp4(RegionTexel(i), RegionTexel(i + ivec2(1, 0)), f.x);
    vec4 bottom = Lerp4(RegionTexel(i + ivec2(0, 1)), RegionTexel(i + ivec2(1, 1)), f.x);
    return Lerp4(top, bottom, f.y);
}
#endif
// The depth at the GS's sample point: the pixel's centre is 1/256 of a pixel past it (right and down, see the top), which on
// a steep surface is a unit or two of depth; the GS truncates it (exactly, while the value is below 2^24)
float GsDepth()
{
    float d = vDepth - (dFdx(vDepth) + dFdy(vDepth)) * SAMPLE_SHIFT;
    float z = d * 4294967296.0;
    if (z < 16777216.0)
    {
        // (reversed, the truncation is upwards)
        z = depthReverse != 0 ? ceil(max(z, 0.0) - 1.0 / 64.0) : floor(max(z, 0.0) + 1.0 / 64.0);
        d = z * (1.0 / 4294967296.0);
    }
    return d;
}
void main()
{
    // (before any discard: the derivatives need the whole quad)
    float depth = GsDepth();
#if NATIVEDEPTH
    // The depth test where the GS makes it: at the block's first sample, against the depth there
    {
        ivec2 block = ivec2(floor(gl_FragCoord.xy / float(targetScale))) * targetScale;
        vec2 back = gl_FragCoord.xy - (vec2(block) + 0.5);
        float here = depth - dFdx(depth) * back.x - dFdy(depth) * back.y;
        float there = texelFetch(depthCopy, block, 0).r;
        bool pass = depthReverse != 0 ? (nativeZtst == 2 ? here <= there : here < there) : (nativeZtst == 2 ? here >= there : here > there);
        if (!pass)
        {
            discard;
        }
    }
#endif
    vec4 c = vColour;
    vec3 rgb;
    float a;
#if TEXTURED
#if UVMODE
    vec2 uv = vStq.xy;
#else
    vec2 uv = vStq.xy / vStq.z * texSize;
#endif
#if NATIVEDEPTH || NATIVETEXEL
    // (and the texture read where the GS reads it: the block's first sample's coordinates)
    {
        ivec2 block = ivec2(floor(gl_FragCoord.xy / float(targetScale))) * targetScale;
        vec2 back = gl_FragCoord.xy - (vec2(block) + 0.5);
        uv = uv - dFdx(uv) * back.x - dFdy(uv) * back.y;
    }
#endif
    // The GS's texel coordinates are exact (S/Q times the size, on its sixteenth of a texel grid); the GPU's interpolation can
    // land a hair under a texel's edge where the GS lands on it, and read the texel before it. A 256th of a texel, well inside
    // the GS's grid, puts it back on the GS's side (an effect reading a buffer as 8 bit texels one to a pixel: every
    // misread texel is another pixel's byte)
    uv += 1.0 / 256.0;
    float lod = 0.0;
#if MIPMAP
#if LODCONST
    lod = lodK;
#else
    lod = log2(1.0 / abs(vStq.z)) * lodL + lodK;
#endif
    lod = min(lod, lodMax);
#endif
    // A target at a higher scale than the frame, unfiltered: the texel's first sample, the one at the GS's sample point (any
    // other sits up to a pixel's fraction away, across an edge the PS2's resolution doesn't have)
#if REGION
    vec4 t = RegionSample(uv);
#else
    if (texSnap > 0.0)
    {
        uv = floor(uv) + 0.5 / texSnap;
    }
    vec4 t = floor(textureLod(tex, uv / texActual, lod) * 255.0 + 0.5);
#endif
#if TEXALPHA == 1
    t.a = (aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : ta0;
#elif TEXALPHA == 2
    // A 16 bit target's channels are 5 bits (GL's blending into it under a stencil DATE may have left the low bits)
    t.rgb = floor(t.rgb / 8.0) * 8.0;
    t.a = t.a >= 128.0 ? ta1 : ((aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : ta0);
#endif
    float va = floor(c.a);
#if TFX == 0
    rgb = min(floor(t.rgb * c.rgb / 128.0), 255.0);
#if TCC
    a = min(floor(t.a * c.a / 128.0), 255.0);
#else
    a = va;
#endif
#elif TFX == 1
    rgb = t.rgb;
#if TCC
    a = t.a;
#else
    a = va;
#endif
#elif TFX == 2
    rgb = min(floor(t.rgb * c.rgb / 128.0) + va, 255.0);
#if TCC
    a = min(t.a + va, 255.0);
#else
    a = va;
#endif
#else
    rgb = min(floor(t.rgb * c.rgb / 128.0) + va, 255.0);
#if TCC
    a = t.a;
#else
    a = va;
#endif
#endif
#else
    rgb = floor(c.rgb);
    a = floor(c.a);
#endif
#if ALPHATEST
    bool pass;
#if ATST == 0
    pass = false;
#elif ATST == 1
    pass = true;
#elif ATST == 2
    pass = a < aref;
#elif ATST == 3
    pass = a <= aref;
#elif ATST == 4
    pass = a == aref;
#elif ATST == 5
    pass = a >= aref;
#elif ATST == 6
    pass = a > aref;
#else
    pass = a != aref;
#endif
#if FAILPASS
    if (pass)
    {
        discard;
    }
#else
    if (!pass)
    {
        discard;
    }
#endif
#endif
#if FOG
    float f = floor(vFog);
    rgb = floor((f * rgb + (255.0 - f) * fogColour) / 256.0);
#endif
#if WRAPADD
    // A wrapping additive blend (COLCLAMP off, D the destination): this primitive's part of the sum, as it adds to the 16 bit
    // frame's 5 bit channels (the destination's low bits are none, so the part's truncation is the sum's), summed by GL into a
    // float buffer and wrapped when it's resolved
    {
        float factor = blendC == 0 ? a : blendFix;
        vec3 part = floor((Pick(blendA, rgb, vec3(0.0)) - Pick(blendB, rgb, vec3(0.0))) * factor / 128.0);
#if DITHER
        ivec2 p = ivec2(floor(gl_FragCoord.xy / scale));
        part = part + float(dimx[(p.y & 3) * 4 + (p.x & 3)]);
#endif
        part = floor(part / 8.0) * 8.0;
        // As its signed equivalent modulo 256 (-8, not 248), in 256ths: a half float holds these and their sums exactly (in
        // 255ths, as large parts, the sums of the many layers of a shadow volume pass rounded off by a unit or two)
        part = mod(part, 256.0);
        part = mix(part, part - 256.0, greaterThanEqual(part, vec3(128.0)));
        outColour = vec4(part / 256.0, 1.0);
        outFactor = vec4(0.0);
#if DEPTH
        gl_FragDepth = depth;
#endif
        return;
    }
#endif
#if SHADERBLEND
    // The destination (a copy of the target from before the batch) and the GS's own blend, mask and tests on it
    vec4 d = floor(texelFetch(destination, ivec2(gl_FragCoord.xy), 0) * 255.0 + 0.5);
#if FRAME16
    d.rgb = floor(d.rgb / 8.0) * 8.0;
    d.a = d.a >= 128.0 ? 128.0 : 0.0;
#endif
    float da = d.a;
#if FRAME24
    da = 128.0;
#endif
#if DATE
    if ((da >= 128.0 ? 1 : 0) != datm)
    {
        discard;
    }
#endif
    if (blendOn != 0 && !(pabe != 0 && a < 128.0))
    {
        float factor = blendC == 0 ? a : blendC == 1 ? da : blendFix;
        rgb = floor((Pick(blendA, rgb, d.rgb) - Pick(blendB, rgb, d.rgb)) * factor / 128.0) + Pick(blendD, rgb, d.rgb);
    }
#endif
#if FRAME16
#if DITHER
    ivec2 p = ivec2(floor(gl_FragCoord.xy / scale));
    rgb = rgb + float(dimx[(p.y & 3) * 4 + (p.x & 3)]);
#endif
#endif
    rgb = colclamp != 0 ? clamp(rgb, 0.0, 255.0) : mod(rgb, 256.0);
#if FRAME16
    rgb = floor(rgb / 8.0) * 8.0;
#endif
    float written = a;
#if FBA
    written = written < 128.0 ? written + 128.0 : written;
#endif
#if FRAME16
    written = written >= 128.0 ? 128.0 : 0.0;
#endif
#if SHADERBLEND
    uvec4 merged = (uvec4(vec4(rgb, written)) & ~frameMask) | (uvec4(d) & frameMask);
    outColour = vec4(merged) / 255.0;
#else
    outColour = vec4(rgb / 255.0, written / 255.0);
#endif
    outFactor = vec4(0.0, 0.0, 0.0, min(a / 128.0, 1.0));
#if DEPTH
    gl_FragDepth = depth;
#endif
}
)";

// Uploads of depth: a staging texture's values (depth over 2^32) written over a rectangle
constexpr const char* DepthVertexSource = R"(#version 330 core
uniform vec4 rect;
uniform vec2 viewSize;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 at = mix(rect.xy, rect.zw, corner);
    gl_Position = vec4(at / viewSize * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* DepthFragmentSource = R"(#version 330 core
uniform sampler2D staging;
uniform float scale;
out vec4 unused;
void main()
{
    gl_FragDepth = texelFetch(staging, ivec2(floor(gl_FragCoord.xy / scale)), 0).r;
    unused = vec4(0.0);
}
)";

// The PCRTC's two circuits merged (circuit 1 over circuit 2 by ALP or its own alpha, over the background colour), at the scale
constexpr const char* MergeVertexSource = R"(#version 330 core
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* MergeFragmentSource = R"(#version 330 core
uniform sampler2D circuit1;
uniform sampler2D circuit2;
// Each circuit: enabled, its place in the picture (pixels at the scale), its size, its corner in its buffer, its buffer's size
uniform int enabled1;
uniform int enabled2;
uniform vec4 place1;
uniform vec4 place2;
uniform vec2 corner1;
uniform vec2 corner2;
uniform vec2 size1;
uniform vec2 size2;
uniform float alp;
uniform int ownAlpha;
uniform vec3 background;
out vec4 colour;
bool Read(sampler2D buffer, vec4 place, vec2 corner, vec2 size, vec2 at, out vec4 c)
{
    vec2 local = at - place.xy;
    if (local.x < 0.0 || local.y < 0.0 || local.x >= place.z || local.y >= place.w)
    {
        return false;
    }

    c = texelFetch(buffer, ivec2(corner + local), 0);
    return true;
}
void main()
{
    vec2 at = floor(gl_FragCoord.xy);
    vec4 c1;
    vec4 c2;
    bool has1 = enabled1 != 0 && Read(circuit1, place1, corner1, size1, at, c1);
    bool has2 = enabled2 != 0 && Read(circuit2, place2, corner2, size2, at, c2);
    vec3 under = has2 ? c2.rgb : background;
    vec3 result = under;
    if (has1)
    {
        float a = ownAlpha != 0 ? min(c1.a * 2.0, 1.0) : alp;
        result = c1.rgb * a + under * (1.0 - a);
    }

    colour = vec4(result, 1.0);
}
)";

// The destination alpha test as a stencil: pixels whose destination alpha's top bit is DATM get 1
constexpr const char* DateFragmentSource = R"(#version 330 core
uniform sampler2D destination;
uniform int datm;
out vec4 unused;
void main()
{
    float a = floor(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).a * 255.0 + 0.5);
    if ((a >= 128.0 ? 1 : 0) != datm)
    {
        discard;
    }

    unused = vec4(0.0);
}
)";

// A wrapping sum resolved: the target's colours plus the float buffer's sum, modulo 256
constexpr const char* ResolveFragmentSource = R"(#version 330 core
uniform sampler2D before;
uniform sampler2D sum;
// The alpha written where the sums' batches drew (their count), as a 16 bit frame's target stores its bit
uniform float alphaBit;
out vec4 colour;
void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 b = texelFetch(before, at, 0);
    vec4 s = texelFetch(sum, at, 0);
    vec3 c = floor(b.rgb * 255.0 + 0.5) + floor(s.rgb * 256.0 + 0.5);
    colour = vec4(mod(c, 256.0) / 255.0, s.a > 0.0 ? alphaBit : b.a);
}
)";

// GS local memory on the GPU (a 1024x1024 R32UI texture of its words) and the conversions between it and the targets, by the
// GS's swizzles: a layout's table of where each pixel of a page is (its block in the page, its unit in the block) and the
// inverse (a page's units' pixels), made from gsmemory.cpp's own functions
constexpr const char* MemCommonSource = R"(
uniform usampler2D mem;
uniform usampler2D fwd;
uniform usampler2D inv;
uniform int unitsPerBlock;
uniform int unitShift;
uniform int unitBits;
uniform int pageWidth;
uniform int pageHeight;
uniform int pagesAcross;
uniform int pagesDown;
uniform int basePointer;
uniform int xorZ;
uint MemUnit(uint address)
{
    uint w = (address >> uint(unitShift)) & 0xFFFFFu;
    uint word = texelFetch(mem, ivec2(int(w & 1023u), int(w >> 10)), 0).r;
    uint sub = address & ((1u << uint(unitShift)) - 1u);
    uint mask = unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(unitBits)) - 1u);
    return (word >> (sub * uint(unitBits))) & mask;
}
uint UnitAddress(ivec2 p)
{
    uint page = uint((p.y / pageHeight) * pagesAcross + p.x / pageWidth);
    uint f = texelFetch(fwd, ivec2(p.x % pageWidth, p.y % pageHeight), 0).r;
    uint block = uint(basePointer) + page * 32u + (f >> 16);
    if (xorZ != 0)
    {
        block ^= 24u;
    }

    return (block & 16383u) * uint(unitsPerBlock) + (f & 0xFFFFu);
}
// The pixel a unit of memory is (false: none of the buffer's)
bool UnitPixel(uint address, out ivec2 p)
{
    uint block = address / uint(unitsPerBlock);
    uint inBlock = address % uint(unitsPerBlock);
    if (xorZ != 0)
    {
        block ^= 24u;
    }

    uint rel = (block - uint(basePointer)) & 16383u;
    uint page = rel >> 5;
    if (page >= uint(pagesAcross * pagesDown))
    {
        return false;
    }

    uint i = (rel & 31u) * uint(unitsPerBlock) + inBlock;
    uint v = texelFetch(inv, ivec2(int(i & 1023u), int(i >> 10)), 0).r;
    p = ivec2(int(page % uint(pagesAcross)) * pageWidth + int(v & 0xFFFFu), int(page / uint(pagesAcross)) * pageHeight + int(v >> 16));
    return true;
}
)";

constexpr const char* MemRectVertexSource = R"(#version 330 core
uniform vec4 rect;
uniform vec2 viewSize;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 at = mix(rect.xy, rect.zw, corner);
    gl_Position = vec4(at / viewSize * 2.0 - 1.0, 0.0, 1.0);
}
)";

// A target's pixels written into memory (the words of the pages drawn over; the target's sample point of each pixel)
constexpr const char* MemFromTargetSource = R"(
uniform sampler2D target;
uniform int targetScale;
// 0 colour 32, 1 colour 16, 2 depth
uniform int storage;
uniform int depthReverse;
out uint word;
uint Unit(uint address)
{
    ivec2 p;
    if (!UnitPixel(address, p))
    {
        discard;
    }

    vec4 c = texelFetch(target, p * targetScale, 0);
    if (storage == 2)
    {
        float z = floor(c.r * 4294967296.0 + 0.5);
        uint v = uint(min(z, 4294967040.0));
        v = depthReverse != 0 ? 0xFFFFFFFFu - v : v;
        return unitBits == 32 ? v : v & 0xFFFFu;
    }

    uvec4 b = uvec4(floor(c * 255.0 + 0.5));
    if (storage == 0)
    {
        return b.r | b.g << 8 | b.b << 16 | b.a << 24;
    }

    return (b.r >> 3) | (b.g >> 3) << 5 | (b.b >> 3) << 10 | (b.a >= 128u ? 0x8000u : 0u);
}
void main()
{
    uint w = uint(gl_FragCoord.y) * 1024u + uint(gl_FragCoord.x);
    uint units = 1u << uint(unitShift);
    uint value = 0u;
    for (uint k = 0u; k < units; k++)
    {
        value |= Unit((w << uint(unitShift)) | k) << (k * uint(unitBits));
    }

    word = value;
}
)";

// Memory's pixels written into a target (the pages asked for only: a page mask)
constexpr const char* TargetFromMemSource = R"(
uniform int targetScale;
uniform int storage;
uniform int depthReverse;
uniform uint pageMask[16];
out vec4 colour;
void main()
{
    ivec2 p = ivec2(floor(gl_FragCoord.xy)) / targetScale;
    uint address = UnitAddress(p);
    uint page = (address >> uint(unitShift)) >> 11;
    if ((pageMask[page >> 5] & (1u << (page & 31u))) == 0u)
    {
        discard;
    }

    uint v = MemUnit(address);
    if (storage == 2)
    {
        gl_FragDepth = float(depthReverse != 0 ? 0xFFFFFFFFu - v : v) * (1.0 / 4294967296.0);
        colour = vec4(0.0);
        return;
    }

    gl_FragDepth = 0.0;
    if (storage == 0)
    {
        colour = vec4(uvec4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24)) / 255.0;
    }
    else
    {
        colour = vec4(float((v & 31u) << 3), float(((v >> 5) & 31u) << 3), float(((v >> 10) & 31u) << 3), (v & 0x8000u) != 0u ? 128.0 : 0.0) / 255.0;
    }
}
)";

// The CPU's writes of rectangles of a buffer (one layout) put into memory's copy: each unit (pixel) of the pages' words in a
// rectangle the CPU's (fresh: local memory's pages), every other memory's copy's as it was (mem: a copy of it). The bits of a
// unit a write sets: unitMask (24 bit and the 8 and 4 bit formats in 32 bit words write part of each)
constexpr const char* MergeWritesSource = R"(
uniform usampler2D fresh;
uniform int rectCount;
uniform ivec4 rects[8];
uniform uint unitMask;
uniform uint pageMask[16];
out uint word;
void main()
{
    ivec2 at = ivec2(floor(gl_FragCoord.xy));
    uint w = uint(at.y) * 1024u + uint(at.x);
    uint page = w >> 11;
    if ((pageMask[page >> 5] & (1u << (page & 31u))) == 0u)
    {
        discard;
    }

    uint units = 1u << uint(unitShift);
    uint unitAll = unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(unitBits)) - 1u);
    uint take = 0u;
    for (uint k = 0u; k < units; k++)
    {
        ivec2 p;
        if (!UnitPixel((w << uint(unitShift)) | k, p))
        {
            continue;
        }

        for (int r = 0; r < rectCount; r++)
        {
            if (p.x >= rects[r].x && p.y >= rects[r].y && p.x <= rects[r].z && p.y <= rects[r].w)
            {
                take |= unitAll << (k * uint(unitBits));
                break;
            }
        }
    }

    take &= unitMask;
    word = (texelFetch(mem, at, 0).r & ~take) | (texelFetch(fresh, at, 0).r & take);
}
)";

// A texture level decoded from memory's copy as the software GS decodes it (DecodeTexel: TEXA, the CLUT), into a texture's
// level: RGBA, the PS2's alpha (0x80 = 1.0) in the bytes
constexpr const char* DecodeSource = R"(
uniform int psm;
uniform usampler2D clut;
uniform int cpsm;
uniform int csa;
uniform uint ta0;
uniform uint ta1;
uniform int aem;
out vec4 colour;
uint Expand16(uint c)
{
    uint alpha = (c & 0x8000u) != 0u ? ta1 : ((aem != 0 && (c & 0x7FFFu) == 0u) ? 0u : ta0);
    return (c & 31u) << 3 | ((c >> 5) & 31u) << 11 | ((c >> 10) & 31u) << 19 | alpha << 24;
}
uint ClutEntry(int i)
{
    return texelFetch(clut, ivec2(i & 511, 0), 0).r;
}
uint ClutColour(uint index)
{
    if (cpsm == 0 || cpsm == 1)
    {
        int slot = int((index + uint(csa & 15) * 16u) & 255u);
        uint c = ClutEntry(slot) | ClutEntry(slot + 256) << 16;
        if (cpsm == 1)
        {
            uint alpha = (aem != 0 && (c & 0xFFFFFFu) == 0u) ? 0u : ta0;
            c = (c & 0xFFFFFFu) | alpha << 24;
        }

        return c;
    }

    return Expand16(ClutEntry(int((index + uint(csa) * 16u) & 511u)));
}
void main()
{
    ivec2 p = ivec2(floor(gl_FragCoord.xy));
    uint v = MemUnit(UnitAddress(p));
    uint c;
    // PSMCT32 0x00, CT24 0x01, CT16 0x02, CT16S 0x0A, T8 0x13, T4 0x14, T8H 0x1B, T4HL 0x24, T4HH 0x2C, Z32 0x30, Z24 0x31,
    // Z16 0x32, Z16S 0x3A
    if (psm == 0x00 || psm == 0x30)
    {
        c = v;
    }
    else if (psm == 0x01 || psm == 0x31)
    {
        c = v & 0xFFFFFFu;
        uint alpha = (aem != 0 && c == 0u) ? 0u : ta0;
        c = c | alpha << 24;
    }
    else if (psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A)
    {
        c = Expand16(v);
    }
    else if (psm == 0x1B)
    {
        c = ClutColour(v >> 24);
    }
    else if (psm == 0x24)
    {
        c = ClutColour((v >> 24) & 15u);
    }
    else if (psm == 0x2C)
    {
        c = ClutColour(v >> 28);
    }
    else
    {
        c = ClutColour(v);
    }

    colour = vec4(uvec4(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u, c >> 24)) / 255.0;
}
)";

// Copies by drawing (a blit makes Apple's OpenGL submit its work and wait): a texture's texels at the same places
constexpr const char* CopyColourSource = R"(#version 330 core
uniform sampler2D source;
out vec4 colour;
void main()
{
    colour = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
}
)";

constexpr const char* CopyWordSource = R"(#version 330 core
uniform usampler2D source;
out uint word;
void main()
{
    word = texelFetch(source, ivec2(gl_FragCoord.xy), 0).r;
}
)";

// The texture upscale: a texture's texels read at S times the size with Catmull-Rom's cubic (sharper than the bilinear a
// magnified texture gets), the edges' texels repeated
constexpr const char* UpscaleSource = R"(#version 330 core
uniform sampler2D source;
uniform vec2 sourceSize;
uniform float scale;
out vec4 colour;
vec4 Fetch(ivec2 p)
{
    return texelFetch(source, clamp(p, ivec2(0), ivec2(sourceSize) - 1), 0);
}
vec4 Weights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}
void main()
{
    vec2 position = gl_FragCoord.xy / scale - 0.5;
    vec2 floored = floor(position);
    ivec2 base = ivec2(floored);
    vec4 wx = Weights(position.x - floored.x);
    vec4 wy = Weights(position.y - floored.y);
    vec4 sum = vec4(0.0);
    for (int y = 0; y < 4; y++)
    {
        vec4 row = Fetch(base + ivec2(-1, y - 1)) * wx.x + Fetch(base + ivec2(0, y - 1)) * wx.y +
                   Fetch(base + ivec2(1, y - 1)) * wx.z + Fetch(base + ivec2(2, y - 1)) * wx.w;
        sum += row * wy[y];
    }

    colour = clamp(sum, 0.0, 1.0);
}
)";

struct Program
{
    GLuint id = 0;
    GLint viewSize;
    GLint viewOrigin;
    GLint depthCopy;
    GLint nativeZtst;
    GLint targetScale;
    GLint zMax;
    GLint zMaxU;
    GLint depthReverse;
    GLint pointSize;
    GLint tex;
    GLint texSize;
    GLint texActual;
    GLint texSnap;
    GLint aref;
    GLint fogColour;
    GLint ta0;
    GLint ta1;
    GLint aem;
    GLint lodK;
    GLint lodL;
    GLint lodMax;
    GLint dimx;
    GLint scale;
    GLint destination;
    GLint blendOn;
    GLint blendA;
    GLint blendB;
    GLint blendC;
    GLint blendD;
    GLint blendFix;
    GLint pabe;
    GLint colclamp;
    GLint datm;
    GLint frameMask;
    GLint regionMode;
    GLint regionBounds;
};

GLuint CompileShader(GLenum kind, const std::string& source)
{
    Gl& gl = GetGl();
    GLuint shader = gl.CreateShader(kind);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: hardware shader: %s\n", log);
        std::abort();
    }

    return shader;
}

GLuint LinkProgram(GLuint vertex, GLuint fragment, bool dualSource)
{
    Gl& gl = GetGl();
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    if (dualSource)
    {
        gl.BindFragDataLocationIndexed(program, 0, 0, "outColour");
        gl.BindFragDataLocationIndexed(program, 0, 1, "outFactor");
    }

    gl.LinkProgram(program);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048];
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: hardware program: %s\n", log);
        std::abort();
    }

    return program;
}

// The biggest texture (a side in pixels) a target is made as: GL_MAX_TEXTURE_SIZE on Apple silicon's GL, the least of the GPUs
// it runs on
constexpr u32 GlMaxTargetSize = 16384;
// The draws' viewport (see DrawBatch)
constexpr GLint BigViewSize = 16384;
constexpr GLint BigViewOrigin = 4096;

GLenum GlFilter(Filter filter)
{
    static constexpr GLenum Filters[] = {GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                                         GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR};
    return Filters[static_cast<u32>(filter)];
}

GLenum GlBlendOp(BlendOp op)
{
    static constexpr GLenum Ops[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT};
    return Ops[static_cast<u32>(op)];
}

GLenum GlBlendFactor(BlendFactor factor)
{
    static constexpr GLenum Factors[] = {GL_ZERO, GL_ONE, GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA, GL_CONSTANT_ALPHA,
                                         GL_ONE_MINUS_CONSTANT_ALPHA};
    return Factors[static_cast<u32>(factor)];
}

GLenum GlDepthFunc(DepthFunc func)
{
    static constexpr GLenum Funcs[] = {GL_ALWAYS, GL_GEQUAL, GL_GREATER, GL_LEQUAL, GL_LESS};
    return Funcs[static_cast<u32>(func)];
}

GLenum GlTopology(Topology topology)
{
    static constexpr GLenum Topologies[] = {GL_TRIANGLES, GL_LINES, GL_POINTS};
    return Topologies[static_cast<u32>(topology)];
}

// What OpenGL keeps for a target besides its texture: its framebuffer, the wrapping sums' float buffer not yet added to it, its
// alpha's changes (a stencil made from it is good while it's the same), and for a depth-stencil buffer the destination alpha
// test its stencil holds (its colour target, DATM, that one's alpha version and the scissor; none when stencilFor is null)
struct GlTarget
{
    GLuint fbo = 0;
    GLuint accum = 0;
    bool accumPending = false;
    // The alpha bit the pending sums' batches write where they draw (their alpha count says where): -1 none of them writes it
    s32 accumAlpha = -1;
    u64 alphaVersion = 0;
    const Target* stencilFor = nullptr;
    u32 stencilDatm = 0;
    u64 stencilAlphaVersion = 0;
    s32 stencilScissor[4] = {};
};

class GlDevice : public GpuDevice
{
public:
    GlDevice(HardwareStats& stats, const Gs::Gs& gs) : stats_(stats), gs_(gs) {}

    bool Init() override;
    void MakeCurrent() override;
    bool ReadsDestination() const override { return false; }
    u32 MaxTargetSize() const override { return GlMaxTargetSize; }

    bool MakeTarget(Target& target) override;
    void FreeTarget(Target& target) override;
    void ClearDepth(Target& target) override;
    bool Pending(const Target& target) override
    {
        auto found = state_.find(&target);
        return found != state_.end() && found->second.accumPending;
    }
    void Resolve(Target& target) override;
    TextureId CopyTarget(Target& target) override { return CopyTargetRegion(target, nullptr); }
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
    void Finish() override { GetGl().Finish(); }
    // (not measured: OpenGL's timer queries aren't used)
    double GpuSeconds() const override { return -1.0; }

private:
    const Program& ProgramFor(u32 bits);
    GLuint FramebufferFor(Target* colour, Target* depth);
    GLuint AccumFramebufferFor(Target* colour, Target* depth);
    GLuint CopyTargetRegion(Target& target, const Rect* region);
    Target* ScratchStencil(Target* colour);
    // A rectangle of a texture copied into the framebuffer bound (its size given) at the same places
    void CopyRect(GLuint program, GLuint source, f32 viewWidth, f32 viewHeight, s32 x0, s32 y0, s32 x1, s32 y1);
    void UseLayout(GLuint program, u32 psm, u32 bp, u32 bw, u32 pagesDown);
    GLuint PictureFramebuffer(const Picture& picture) { return picture.target != nullptr ? state_[picture.target].fbo : mergeFbo_; }

    // TWIN_HW_STATS: the render passes begun (a draw into another framebuffer or attachment than the last), by cause
    void Pass(u64 identity, const char* cause)
    {
        static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
        if (counting && identity != passIdentity_)
        {
            passCounts_[cause]++;
        }

        passIdentity_ = identity;
    }

    HardwareStats& stats_;
    const Gs::Gs& gs_;
    std::unordered_map<const Target*, GlTarget> state_;
    std::map<std::pair<Target*, Target*>, GLuint> framebuffers_;
    std::map<std::pair<Target*, Target*>, GLuint> accumFramebuffers_;
    std::unordered_map<u32, Program> programs_;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint depthProgram_ = 0;
    GLuint resolveProgram_ = 0;
    GLuint mergeProgram_ = 0;
    GLuint mergeTexture_ = 0;
    GLuint mergeFbo_ = 0;
    u32 mergeWidth_ = 0;
    u32 mergeHeight_ = 0;
    GLuint emptyArray_ = 0;
    // The copies of targets a batch reads (by size)
    std::map<std::tuple<u32, u32>, std::pair<GLuint, GLuint>> copies_;
    // Stencil buffers for the destination alpha test of draws without a depth buffer, by colour target
    std::map<Target*, std::unique_ptr<Target>> scratchStencils_;
    GLuint dateProgram_ = 0;
    // GS local memory on the GPU, the layouts' tables (32, 16, 16S, 8, 4 bit), the passes between it and the targets
    GLuint memTexture_ = 0;
    // The buffer CPU writes go to memory's copy through (the GPU copies from it: no wait for the texture's earlier use)
    GLuint uploadBuffer_ = 0;
    GLuint memFbo_ = 0;
    GLuint fwdTables_[5] = {};
    GLuint invTables_[5] = {};
    GLuint memFromTargetProgram_ = 0;
    GLuint targetFromMemProgram_ = 0;
    GLuint copyColourProgram_ = 0;
    GLuint copyWordProgram_ = 0;
    GLuint mergeWritesProgram_ = 0;
    // A depth target's copy as floats (the shader's native depth test reads it), by size
    std::map<std::pair<u32, u32>, std::pair<GLuint, GLuint>> depthCopies_;
    GLuint decodeProgram_ = 0;
    GLuint upscaleProgram_ = 0;
    GLuint decodeFbo_ = 0;
    u64 passIdentity_ = 0;
    std::map<std::string, u64> passCounts_;
    u64 passFrames_ = 0;
    GLuint clutTexture_ = 0;
    u32 clutUploaded_ = 0;
    // Rings of textures written by the CPU (a texture the GPU may still read is never changed: the driver would wait for it):
    // CLUTs, and staging for memory's pages (copied into memory's copy on the GPU)
    static constexpr u32 ClutRing = 64;
    GLuint clutRing_[ClutRing] = {};
    u32 clutNext_ = 0;
    static constexpr u32 StagingRing = 8;
    GLuint memStaging_[StagingRing] = {};
    GLuint stagingFbo_[StagingRing] = {};
    u32 stagingNext_ = 0;
    std::map<std::string, bool> reported_;
    SDL_Window* ownWindow_ = nullptr;
    SDL_GLContext context_ = nullptr;
    SDL_Window* window_ = nullptr;
    // The shown picture's copies for the render thread's hand-over
    struct ShownCopy
    {
        GLuint texture = 0;
        GLuint fbo = 0;
        u32 width = 0;
        u32 height = 0;
    };
    ShownCopy shownCopies_[3];
};

void GlDevice::MakeCurrent()
{
    if (SDL_GL_GetCurrentContext() != context_)
    {
        SDL_GL_MakeCurrent(window_, context_);
    }
}

bool GlDevice::Init()
{
    if (!RenderContext(&window_, &context_) && !PresenterContext(&window_, &context_))
    {
        // No window: one of its own, hidden
        if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::fprintf(stderr, "graphics: hardware renderer: no video: %s\n", SDL_GetError());
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        ownWindow_ = SDL_CreateWindow("graphics", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (ownWindow_ == nullptr)
        {
            std::fprintf(stderr, "graphics: hardware renderer: no window: %s\n", SDL_GetError());
            return false;
        }

        context_ = SDL_GL_CreateContext(ownWindow_);
        if (context_ == nullptr)
        {
            std::fprintf(stderr, "graphics: hardware renderer: no OpenGL 3.3: %s\n", SDL_GetError());
            return false;
        }

        window_ = ownWindow_;
    }

    MakeCurrent();
    if (!LoadGl())
    {
        return false;
    }

    Gl& gl = GetGl();
    gl.GenVertexArrays(1, &vertexArray_);
    gl.GenBuffers(1, &vertexBuffer_);
    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, x)));
    gl.VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, z)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, r)));
    gl.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, s)));
    gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, fog)));
    for (GLuint i = 0; i < 5; i++)
    {
        gl.EnableVertexAttribArray(i);
    }

    gl.GenVertexArrays(1, &emptyArray_);
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, DepthVertexSource);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, DepthFragmentSource);
    depthProgram_ = LinkProgram(vertex, fragment, false);
    resolveProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, ResolveFragmentSource),
                                  false);
    dateProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, DateFragmentSource),
                               false);
    mergeProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, MergeFragmentSource),
                                false);
    return true;
}

const Program& GlDevice::ProgramFor(u32 bits)
{
    auto found = programs_.find(bits);
    if (found != programs_.end())
    {
        return found->second;
    }

    std::string header = "#version 330 core\n";
    auto define = [&](const char* name, u32 value) { header += "#define " + std::string(name) + " " + std::to_string(value) + "\n"; };
    define("TEXTURED", (bits & BitTextured) ? 1 : 0);
    define("TFX", (bits >> BitTfxShift) & 3);
    define("TCC", (bits & BitTcc) ? 1 : 0);
    define("FOG", (bits & BitFog) ? 1 : 0);
    define("ATST", (bits >> BitAtstShift) & 7);
    define("ALPHATEST", (bits & BitAlphaTest) ? 1 : 0);
    define("UVMODE", (bits & BitUv) ? 1 : 0);
    define("TEXALPHA", (bits >> BitTexAlphaShift) & 3);
    define("FRAME16", (bits & BitFrame16) ? 1 : 0);
    define("DITHER", (bits & BitDither) ? 1 : 0);
    header += "#define SAMPLE_SHIFT " + std::to_string(-VertexNudge()) + "\n";
    define("FBA", (bits & BitFba) ? 1 : 0);
    define("MIPMAP", (bits & BitMipmap) ? 1 : 0);
    define("LODCONST", (bits & BitLodConstant) ? 1 : 0);
    define("FAILPASS", (bits & BitFailPass) ? 1 : 0);
    define("DEPTH", (bits & BitDepth) ? 1 : 0);
    define("SHADERBLEND", (bits & BitShaderBlend) ? 1 : 0);
    define("DATE", (bits & BitDate) ? 1 : 0);
    define("FRAME24", (bits & BitFrame24) ? 1 : 0);
    define("WRAPADD", (bits & BitWrapAdd) ? 1 : 0);
    define("NATIVEDEPTH", (bits & BitNativeDepth) ? 1 : 0);
    define("NATIVETEXEL", (bits & BitNativeTexel) ? 1 : 0);
    define("REGION", (bits & BitRegion) ? 1 : 0);
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, header + FragmentSource);
    Gl& gl = GetGl();
    Program p;
    p.id = LinkProgram(vertex, fragment, true);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    p.viewSize = gl.GetUniformLocation(p.id, "viewSize");
    p.viewOrigin = gl.GetUniformLocation(p.id, "viewOrigin");
    p.depthCopy = gl.GetUniformLocation(p.id, "depthCopy");
    p.nativeZtst = gl.GetUniformLocation(p.id, "nativeZtst");
    p.targetScale = gl.GetUniformLocation(p.id, "targetScale");
    p.zMax = gl.GetUniformLocation(p.id, "zMax");
    p.zMaxU = gl.GetUniformLocation(p.id, "zMaxU");
    p.depthReverse = gl.GetUniformLocation(p.id, "depthReverse");
    p.pointSize = gl.GetUniformLocation(p.id, "pointSize");
    p.tex = gl.GetUniformLocation(p.id, "tex");
    p.texSize = gl.GetUniformLocation(p.id, "texSize");
    p.texActual = gl.GetUniformLocation(p.id, "texActual");
    p.texSnap = gl.GetUniformLocation(p.id, "texSnap");
    p.aref = gl.GetUniformLocation(p.id, "aref");
    p.fogColour = gl.GetUniformLocation(p.id, "fogColour");
    p.ta0 = gl.GetUniformLocation(p.id, "ta0");
    p.ta1 = gl.GetUniformLocation(p.id, "ta1");
    p.aem = gl.GetUniformLocation(p.id, "aem");
    p.lodK = gl.GetUniformLocation(p.id, "lodK");
    p.lodL = gl.GetUniformLocation(p.id, "lodL");
    p.lodMax = gl.GetUniformLocation(p.id, "lodMax");
    p.dimx = gl.GetUniformLocation(p.id, "dimx");
    p.scale = gl.GetUniformLocation(p.id, "scale");
    p.destination = gl.GetUniformLocation(p.id, "destination");
    p.blendOn = gl.GetUniformLocation(p.id, "blendOn");
    p.blendA = gl.GetUniformLocation(p.id, "blendA");
    p.blendB = gl.GetUniformLocation(p.id, "blendB");
    p.blendC = gl.GetUniformLocation(p.id, "blendC");
    p.blendD = gl.GetUniformLocation(p.id, "blendD");
    p.blendFix = gl.GetUniformLocation(p.id, "blendFix");
    p.pabe = gl.GetUniformLocation(p.id, "pabe");
    p.colclamp = gl.GetUniformLocation(p.id, "colclamp");
    p.datm = gl.GetUniformLocation(p.id, "datm");
    p.frameMask = gl.GetUniformLocation(p.id, "frameMask");
    p.regionMode = gl.GetUniformLocation(p.id, "regionMode");
    p.regionBounds = gl.GetUniformLocation(p.id, "regionBounds");
    return programs_[bits] = p;
}

bool GlDevice::MakeTarget(Target& t)
{
    GlTarget& g = state_[&t];
    Gl& gl = GetGl();
    GLuint texture = 0;
    gl.GenTextures(1, &texture);
    t.texture = texture;
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLsizei w = static_cast<GLsizei>(t.width * t.scale);
    GLsizei h = static_cast<GLsizei>(t.height * t.scale);
    gl.GenFramebuffers(1, &g.fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    // (no storage for a size GL can't make: the framebuffer is left incomplete, and the target to the software GS). A draw into
    // an incomplete framebuffer is an error in GL, and macOS's GL (on Metal) aborts on it instead (a render pass with no usable
    // attachment)
    const bool fits = w > 0 && h > 0 && static_cast<u32>(w) <= GlMaxTargetSize && static_cast<u32>(h) <= GlMaxTargetSize;
    if (t.depth)
    {
        if (fits)
        {
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH32F_STENCIL8), w, h, 0, GL_DEPTH_STENCIL,
                          GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
        }

        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
        gl.DrawBuffer(GL_NONE);
        gl.ReadBuffer(GL_NONE);
    }
    else
    {
        if (fits)
        {
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }

        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }

    return fits && gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void GlDevice::FreeTarget(Target& target)
{
    Gl& gl = GetGl();
    for (auto it = framebuffers_.begin(); it != framebuffers_.end();)
    {
        if (it->first.first == &target || it->first.second == &target)
        {
            gl.DeleteFramebuffers(1, &it->second);
            it = framebuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    auto scratch = scratchStencils_.find(&target);
    if (scratch != scratchStencils_.end())
    {
        Target* stencil = scratch->second.get();
        for (auto it = framebuffers_.begin(); it != framebuffers_.end();)
        {
            if (it->first.second == stencil)
            {
                gl.DeleteFramebuffers(1, &it->second);
                it = framebuffers_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        gl.DeleteTextures(1, &stencil->texture);
        state_.erase(stencil);
        scratchStencils_.erase(scratch);
    }

    for (auto it = accumFramebuffers_.begin(); it != accumFramebuffers_.end();)
    {
        if (it->first.first == &target || it->first.second == &target)
        {
            gl.DeleteFramebuffers(1, &it->second);
            it = accumFramebuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    GlTarget& g = state_[&target];
    if (g.accum != 0)
    {
        gl.DeleteTextures(1, &g.accum);
    }

    gl.DeleteFramebuffers(1, &g.fbo);
    gl.DeleteTextures(1, &target.texture);
    state_.erase(&target);
}

void GlDevice::ClearDepth(Target& depth)
{
    // Zero either way round: 0.0, or reversed 1.0
    Gl& gl = GetGl();
    gl.BindFramebuffer(GL_FRAMEBUFFER, state_[&depth].fbo);
    gl.Disable(GL_SCISSOR_TEST);
    gl.DepthMask(GL_TRUE);
    gl.ClearDepthf(depth.reversed ? 1.0f : 0.0f);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    gl.ClearDepthf(1.0f);
}

GLuint GlDevice::FramebufferFor(Target* colour, Target* depth)
{
    if (depth == nullptr)
    {
        return state_[colour].fbo;
    }

    auto key = std::make_pair(colour, depth);
    auto found = framebuffers_.find(key);
    if (found != framebuffers_.end())
    {
        return found->second;
    }

    Gl& gl = GetGl();
    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour->texture, 0);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth->texture, 0);
    framebuffers_[key] = fbo;
    return fbo;
}

void GlDevice::InitMemory(const LayoutTables& tables)
{
    Gl& gl = GetGl();
    gl.GenTextures(1, &memTexture_);
    gl.BindTexture(GL_TEXTURE_2D, memTexture_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    gl.GenFramebuffers(1, &memFbo_);
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, memTexture_, 0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "graphics: hardware renderer: GS memory's framebuffer isn't complete\n");
    }

    for (u32 f = 0; f < 5; f++)
    {
        const FamilyInfo& info = Families[f];
        const u32 units = info.unitsPerBlock * 32;
        gl.GenTextures(1, &fwdTables_[f]);
        gl.BindTexture(GL_TEXTURE_2D, fwdTables_[f]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), static_cast<GLsizei>(info.pageWidth),
                      static_cast<GLsizei>(info.pageHeight), 0, GL_RED_INTEGER, GL_UNSIGNED_INT, tables.forward[f].data());
        gl.GenTextures(1, &invTables_[f]);
        gl.BindTexture(GL_TEXTURE_2D, invTables_[f]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, static_cast<GLsizei>(units / 1024), 0, GL_RED_INTEGER,
                      GL_UNSIGNED_INT, tables.inverse[f].data());
    }

    std::string header = "#version 330 core\n";
    memFromTargetProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                        CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + MemFromTargetSource), false);
    targetFromMemProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                        CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + TargetFromMemSource), false);
    copyColourProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource), CompileShader(GL_FRAGMENT_SHADER, CopyColourSource),
                                     false);
    copyWordProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource), CompileShader(GL_FRAGMENT_SHADER, CopyWordSource),
                                   false);
    mergeWritesProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                      CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + MergeWritesSource), false);
    decodeProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                 CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + DecodeSource), false);
    upscaleProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource), CompileShader(GL_FRAGMENT_SHADER, UpscaleSource),
                                  false);
    gl.GenFramebuffers(1, &decodeFbo_);
    gl.GenTextures(1, &clutTexture_);
    gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI), 512, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    for (GLuint& clut : clutRing_)
    {
        gl.GenTextures(1, &clut);
        gl.BindTexture(GL_TEXTURE_2D, clut);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI), 512, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    }

    for (u32 i = 0; i < StagingRing; i++)
    {
        gl.GenTextures(1, &memStaging_[i]);
        gl.BindTexture(GL_TEXTURE_2D, memStaging_[i]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        gl.GenFramebuffers(1, &stagingFbo_[i]);
        gl.BindFramebuffer(GL_FRAMEBUFFER, stagingFbo_[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, memStaging_[i], 0);
    }
}

TextureId GlDevice::MakeTexture(const u32* widths, const u32* heights, u32 levels)
{
    Gl& gl = GetGl();
    GLuint id = 0;
    gl.GenTextures(1, &id);
    gl.BindTexture(GL_TEXTURE_2D, id);
    for (u32 i = 0; i < levels; i++)
    {
        gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(widths[i]),
                      static_cast<GLsizei>(heights[i]), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1));
    return id;
}

void GlDevice::UploadTexture(TextureId texture, u32 level, const Rect& rect, const u32* texels, u32 stride)
{
    Gl& gl = GetGl();
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
    gl.TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), rect.left, rect.top, rect.right - rect.left + 1, rect.bottom - rect.top + 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, texels);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

TextureId GlDevice::MakeImage(u32 width, u32 height, const u32* pixels)
{
    Gl& gl = GetGl();
    GLuint made = 0;
    gl.GenTextures(1, &made);
    gl.BindTexture(GL_TEXTURE_2D, made);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl.GenerateMipmap(GL_TEXTURE_2D);
    return made;
}

void GlDevice::FreeTexture(TextureId texture)
{
    GLuint id = texture;
    GetGl().DeleteTextures(1, &id);
}

TextureId GlDevice::MakeUpscaled(TextureId source, u32 width, u32 height, u32 scale)
{
    Gl& gl = GetGl();
    GLuint id = 0;
    const u32 outWidth = width * scale;
    const u32 outHeight = height * scale;
    gl.GenTextures(1, &id);
    gl.BindTexture(GL_TEXTURE_2D, id);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(outWidth), static_cast<GLsizei>(outHeight), 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, decodeFbo_);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, id, 0);
    Pass((2ull << 40) | id, "texture upscale");
    gl.Viewport(0, 0, static_cast<GLsizei>(outWidth), static_cast<GLsizei>(outHeight));
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(upscaleProgram_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source);
    gl.Uniform1i(gl.GetUniformLocation(upscaleProgram_, "source"), 0);
    gl.Uniform2f(gl.GetUniformLocation(upscaleProgram_, "sourceSize"), static_cast<f32>(width), static_cast<f32>(height));
    gl.Uniform1f(gl.GetUniformLocation(upscaleProgram_, "scale"), static_cast<f32>(scale));
    gl.Uniform2f(gl.GetUniformLocation(upscaleProgram_, "viewSize"), static_cast<f32>(outWidth), static_cast<f32>(outHeight));
    gl.Uniform4f(gl.GetUniformLocation(upscaleProgram_, "rect"), 0.0f, 0.0f, static_cast<f32>(outWidth), static_cast<f32>(outHeight));
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindTexture(GL_TEXTURE_2D, id);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    gl.GenerateMipmap(GL_TEXTURE_2D);
    return id;
}

void GlDevice::DecodeLevels(TextureId texture, const DecodeJob& job)
{
    Gl& gl = GetGl();
    if (job.indexed && clutUploaded_ != job.clutVersion)
    {
        clutTexture_ = clutRing_[clutNext_++ % ClutRing];
        gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 2);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT, job.clut);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        clutUploaded_ = job.clutVersion;
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, decodeFbo_);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.BindVertexArray(emptyArray_);
    for (u32 i = 0; i < job.levelCount; i++)
    {
        const Rect& rect = job.rects[i];
        if (rect.right < rect.left)
        {
            continue;
        }

        const Gs::TextureLevel& lv = job.levels[i];
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, static_cast<GLint>(i));
        Pass((1ull << 40) | (static_cast<u64>(texture) << 4) | i, "texture decode");
        gl.Viewport(0, 0, static_cast<GLsizei>(lv.width), static_cast<GLsizei>(lv.height));
        UseLayout(decodeProgram_, lv.psm, lv.bp, lv.bw, 1);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, memTexture_);
        gl.ActiveTexture(GL_TEXTURE0 + 3);
        gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
        gl.ActiveTexture(GL_TEXTURE0);
        auto uniform = [&](const char* name) { return gl.GetUniformLocation(decodeProgram_, name); };
        gl.Uniform1i(uniform("mem"), 0);
        gl.Uniform1i(uniform("clut"), 3);
        gl.Uniform1i(uniform("psm"), static_cast<GLint>(lv.psm));
        gl.Uniform1i(uniform("cpsm"), static_cast<GLint>(job.cpsm));
        gl.Uniform1i(uniform("csa"), static_cast<GLint>(job.csa));
        gl.Uniform1ui(uniform("ta0"), static_cast<GLuint>(job.ta0));
        gl.Uniform1ui(uniform("ta1"), static_cast<GLuint>(job.ta1));
        gl.Uniform1i(uniform("aem"), static_cast<GLint>(job.aem));
        gl.Uniform2f(uniform("viewSize"), static_cast<f32>(lv.width), static_cast<f32>(lv.height));
        gl.Uniform4f(uniform("rect"), static_cast<f32>(rect.left), static_cast<f32>(rect.top), static_cast<f32>(rect.right + 1),
                     static_cast<f32>(rect.bottom + 1));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void GlDevice::CopyRect(GLuint program, GLuint source, f32 viewWidth, f32 viewHeight, s32 x0, s32 y0, s32 x1, s32 y1)
{
    Gl& gl = GetGl();
    gl.Viewport(0, 0, static_cast<GLsizei>(viewWidth), static_cast<GLsizei>(viewHeight));
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(program);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source);
    gl.Uniform1i(gl.GetUniformLocation(program, "source"), 0);
    gl.Uniform2f(gl.GetUniformLocation(program, "viewSize"), viewWidth, viewHeight);
    gl.Uniform4f(gl.GetUniformLocation(program, "rect"), static_cast<f32>(x0), static_cast<f32>(y0), static_cast<f32>(x1),
                 static_cast<f32>(y1));
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GlDevice::UseLayout(GLuint program, u32 psm, u32 bp, u32 bw, u32 pagesDown)
{
    Gl& gl = GetGl();
    u32 family = 0;
    bool xorZ = false;
    LayoutOf(psm, &family, &xorZ);
    const FamilyInfo& info = Families[family];
    gl.UseProgram(program);
    auto uniform = [&](const char* name) { return gl.GetUniformLocation(program, name); };
    gl.Uniform1i(uniform("unitsPerBlock"), static_cast<GLint>(info.unitsPerBlock));
    gl.Uniform1i(uniform("unitShift"), static_cast<GLint>(info.unitShift));
    gl.Uniform1i(uniform("unitBits"), static_cast<GLint>(info.bits));
    gl.Uniform1i(uniform("pageWidth"), static_cast<GLint>(info.pageWidth));
    gl.Uniform1i(uniform("pageHeight"), static_cast<GLint>(info.pageHeight));
    gl.Uniform1i(uniform("pagesAcross"), static_cast<GLint>(std::max<u32>(info.halfWidth ? bw >> 1 : bw, 1)));
    gl.Uniform1i(uniform("pagesDown"), static_cast<GLint>(pagesDown));
    gl.Uniform1i(uniform("basePointer"), static_cast<GLint>(bp));
    gl.Uniform1i(uniform("xorZ"), xorZ ? 1 : 0);
    gl.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, fwdTables_[family]);
    gl.ActiveTexture(GL_TEXTURE0 + 2);
    gl.BindTexture(GL_TEXTURE_2D, invTables_[family]);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(uniform("fwd"), 1);
    gl.Uniform1i(uniform("inv"), 2);
}

void GlDevice::MemoryFromTarget(Target& target, const Gs::PageSet& pages)
{
    Gl& gl = GetGl();
    u32 family = 0;
    bool xorZ = false;
    LayoutOf(target.psm, &family, &xorZ);
    UseLayout(memFromTargetProgram_, target.psm, target.bp, target.bw, target.height / Families[family].pageHeight);
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    Pass(memFbo_, "target to memory");
    gl.Viewport(0, 0, 1024, 1024);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, target.texture);
    // (the memory sampler isn't read here: a table stands in for it, memory being the framebuffer)
    gl.ActiveTexture(GL_TEXTURE0 + 3);
    gl.BindTexture(GL_TEXTURE_2D, fwdTables_[0]);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "target"), 0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "mem"), 3);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "targetScale"), static_cast<GLint>(target.scale));
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "depthReverse"), target.reversed ? 1 : 0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "storage"), target.depth ? 2 : (Is32Class(target.cls) ? 0 : 1));
    gl.Uniform2f(gl.GetUniformLocation(memFromTargetProgram_, "viewSize"), 1024.0f, 1024.0f);
    GLint rect = gl.GetUniformLocation(memFromTargetProgram_, "rect");
    gl.BindVertexArray(emptyArray_);
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        // Runs of pages: two rows of memory a page
        if (!pages.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && pages.test(end + 1))
        {
            end++;
        }

        gl.Uniform4f(rect, 0.0f, static_cast<f32>(page * 2), 1024.0f, static_cast<f32>((end + 1) * 2));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        page = end;
    }
}

void GlDevice::TargetFromMemory(Target& target, const Gs::PageSet& pages, const Rect& all)
{
    Gl& gl = GetGl();
    u32 family = 0;
    bool xorZ = false;
    LayoutOf(target.psm, &family, &xorZ);
    UseLayout(targetFromMemProgram_, target.psm, target.bp, target.bw, target.height / Families[family].pageHeight);
    const GLint n = static_cast<GLint>(target.scale);
    GlTarget& g = state_[&target];
    gl.BindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    Pass(g.fbo, "memory to target");
    gl.Viewport(0, 0, static_cast<GLsizei>(target.width) * n, static_cast<GLsizei>(target.height) * n);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    if (target.depth)
    {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_ALWAYS);
        gl.DepthMask(GL_TRUE);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
    else
    {
        gl.Disable(GL_DEPTH_TEST);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, memTexture_);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "mem"), 0);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "targetScale"), n);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "depthReverse"), target.reversed ? 1 : 0);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "storage"), target.depth ? 2 : (Is32Class(target.cls) ? 0 : 1));
    gl.Uniform2f(gl.GetUniformLocation(targetFromMemProgram_, "viewSize"), static_cast<f32>(target.width * n),
                 static_cast<f32>(target.height * n));
    GLuint mask[16] = {};
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (pages.test(page))
        {
            mask[page >> 5] |= 1u << (page & 31);
        }
    }

    for (u32 i = 0; i < 16; i++)
    {
        char name[24];
        std::snprintf(name, sizeof(name), "pageMask[%u]", i);
        gl.Uniform1ui(gl.GetUniformLocation(targetFromMemProgram_, name), mask[i]);
    }

    // One draw over the pages' rectangle (the page mask leaves the others' pixels as they are)
    if (all.right >= all.left)
    {
        gl.BindVertexArray(emptyArray_);
        gl.Uniform4f(gl.GetUniformLocation(targetFromMemProgram_, "rect"), static_cast<f32>(all.left * n), static_cast<f32>(all.top * n),
                     static_cast<f32>((all.right + 1) * n), static_cast<f32>((all.bottom + 1) * n));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g.alphaVersion++;
}

void GlDevice::UploadMemory(const Gs::PageSet& uploads, const u8* memory)
{
    Gl& gl = GetGl();
    static const bool staged = std::getenv("TWIN_HW_STAGED_UPLOADS") != nullptr;
    if (!staged)
    {
        // Each run of pages into a fresh buffer's storage (the old storage stays the GPU's until it's done with it), copied
        // into memory's copy by the GPU
        if (uploadBuffer_ == 0)
        {
            gl.GenBuffers(1, &uploadBuffer_);
        }

        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer_);
        gl.BindTexture(GL_TEXTURE_2D, memTexture_);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (!uploads.test(page))
            {
                continue;
            }

            u32 end = page;
            while (end + 1 < Gs::PageCount && uploads.test(end + 1))
            {
                end++;
            }

            const size_t bytes = static_cast<size_t>(end - page + 1) * 8192;
            gl.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(bytes), memory + static_cast<size_t>(page) * 8192, GL_STREAM_DRAW);
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2),
                             GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            page = end;
        }

        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        Pass(1ull << 50, "CPU writes to memory");
        return;
    }

    // Into the next staging texture (at the pages' own rows), then copied into memory's copy by the GPU
    const u32 slot = stagingNext_++ % StagingRing;
    gl.BindTexture(GL_TEXTURE_2D, memStaging_[slot]);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    std::vector<std::pair<u32, u32>> runs;
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!uploads.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && uploads.test(end + 1))
        {
            end++;
        }

        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2),
                         GL_RED_INTEGER, GL_UNSIGNED_INT, memory + static_cast<size_t>(page) * 8192);
        runs.emplace_back(page, end);
        page = end;
    }

    // The staging rows copied into memory's copy by drawing
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    Pass(memFbo_, "CPU writes to memory");
    for (const auto& run : runs)
    {
        CopyRect(copyWordProgram_, memStaging_[slot], 1024.0f, 1024.0f, 0, static_cast<s32>(run.first * 2), 1024,
                 static_cast<s32>((run.second + 1) * 2));
    }
}

void GlDevice::ReadMemory(const Gs::PageSet& reads, u8* out)
{
    Gl& gl = GetGl();
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, memFbo_);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!reads.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && reads.test(end + 1))
        {
            end++;
        }

        gl.ReadPixels(0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2), GL_RED_INTEGER, GL_UNSIGNED_INT,
                      out + static_cast<size_t>(page) * 8192);
        page = end;
    }
}

void GlDevice::MergeWrites(const Gs::PageSet& pages, const u8* memory, const std::vector<MergeGroup>& groups)
{
    Gl& gl = GetGl();
    // Local memory's pages (fresh) and memory's copy as it is (old), in two staging textures at the pages' own rows
    const u32 freshSlot = stagingNext_++ % StagingRing;
    const u32 oldSlot = stagingNext_++ % StagingRing;
    // (through a fresh buffer's storage, as UploadMemory uploads: written straight into the texture, the CPU waited for the
    // GPU's reads of the slot from the merges before, about 0.4 ms each and 50 merges a frame in CTTR's races)
    if (uploadBuffer_ == 0)
    {
        gl.GenBuffers(1, &uploadBuffer_);
    }

    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer_);
    gl.BindTexture(GL_TEXTURE_2D, memStaging_[freshSlot]);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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

        gl.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(end - page + 1) * 8192, memory + static_cast<size_t>(page) * 8192,
                      GL_STREAM_DRAW);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2),
                         GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        runs.emplace_back(page, end);
        page = end;
    }

    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Each group in a pass of its own, from memory's copy as the pass before left it
    for (const MergeGroup& group : groups)
    {
        // The words the rectangles leave as they were: read from memory's copy itself after a texture barrier (each fragment
        // its own word), else from a copy of it (a pass of its own)
        const bool barrier = gl.TextureBarrierNV != nullptr;
        if (!barrier)
        {
            gl.BindFramebuffer(GL_FRAMEBUFFER, stagingFbo_[oldSlot]);
            Pass(stagingFbo_[oldSlot], "memory merge copy");
            for (const auto& run : runs)
            {
                CopyRect(copyWordProgram_, memTexture_, 1024.0f, 1024.0f, 0, static_cast<s32>(run.first * 2), 1024,
                         static_cast<s32>((run.second + 1) * 2));
            }
        }

        UseLayout(mergeWritesProgram_, group.psm, group.bp, group.bw, group.pagesDown);
        auto uniform = [&](const char* name) { return gl.GetUniformLocation(mergeWritesProgram_, name); };
        gl.ActiveTexture(GL_TEXTURE0 + 3);
        gl.BindTexture(GL_TEXTURE_2D, barrier ? memTexture_ : memStaging_[oldSlot]);
        gl.ActiveTexture(GL_TEXTURE0 + 4);
        gl.BindTexture(GL_TEXTURE_2D, memStaging_[freshSlot]);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.Uniform1i(uniform("mem"), 3);
        gl.Uniform1i(uniform("fresh"), 4);
        gl.Uniform1i(uniform("rectCount"), static_cast<GLint>(group.rects.size()));
        for (size_t r = 0; r < group.rects.size(); r++)
        {
            char name[24];
            std::snprintf(name, sizeof(name), "rects[%zu]", r);
            gl.Uniform4i(uniform(name), group.rects[r].left, group.rects[r].top, group.rects[r].right, group.rects[r].bottom);
        }

        gl.Uniform1ui(uniform("unitMask"), group.unitMask);
        GLuint mask[16] = {};
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (group.pages.test(page))
            {
                mask[page >> 5] |= 1u << (page & 31);
            }
        }

        for (u32 i = 0; i < 16; i++)
        {
            char name[24];
            std::snprintf(name, sizeof(name), "pageMask[%u]", i);
            gl.Uniform1ui(uniform(name), mask[i]);
        }

        gl.Uniform2f(uniform("viewSize"), 1024.0f, 1024.0f);
        gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
        Pass(memFbo_, "memory merge");
        gl.Viewport(0, 0, 1024, 1024);
        gl.Disable(GL_SCISSOR_TEST);
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_STENCIL_TEST);
        gl.Disable(GL_BLEND);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl.BindVertexArray(emptyArray_);
        if (barrier)
        {
            gl.TextureBarrierNV();
        }

        const GLint rectUniform = uniform("rect");
        for (const auto& run : runs)
        {
            gl.Uniform4f(rectUniform, 0.0f, static_cast<f32>(run.first * 2), 1024.0f, static_cast<f32>((run.second + 1) * 2));
            gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
}

GLuint GlDevice::AccumFramebufferFor(Target* colour, Target* depth)
{
    auto key = std::make_pair(colour, depth);
    auto found = accumFramebuffers_.find(key);
    if (found != accumFramebuffers_.end())
    {
        return found->second;
    }

    Gl& gl = GetGl();
    GlTarget& g = state_[colour];
    if (g.accum == 0)
    {
        gl.GenTextures(1, &g.accum);
        gl.BindTexture(GL_TEXTURE_2D, g.accum);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA16F), static_cast<GLsizei>(colour->width * colour->scale),
                      static_cast<GLsizei>(colour->height * colour->scale), 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    }

    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.accum, 0);
    if (depth != nullptr)
    {
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth->texture, 0);
    }

    accumFramebuffers_[key] = fbo;
    return fbo;
}

// The wrapping sums added to the target (its colour channels, modulo 256), once before anything else uses it
void GlDevice::Resolve(Target& target)
{
    GlTarget& g = state_[&target];
    if (!g.accumPending)
    {
        return;
    }

    g.accumPending = false;
    const s32 alpha = g.accumAlpha;
    g.accumAlpha = -1;
    Gl& gl = GetGl();
    GLuint before = CopyTargetRegion(target, nullptr);
    const GLsizei w = static_cast<GLsizei>(target.width * target.scale);
    const GLsizei h = static_cast<GLsizei>(target.height * target.scale);
    gl.BindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    Pass(g.fbo, "wrap-add resolve");
    gl.Viewport(0, 0, w, h);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, alpha >= 0 ? GL_TRUE : GL_FALSE);
    gl.UseProgram(resolveProgram_);
    gl.Uniform1f(gl.GetUniformLocation(resolveProgram_, "alphaBit"), alpha > 0 ? 128.0f / 255.0f : 0.0f);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, before);
    gl.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, g.accum);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(gl.GetUniformLocation(resolveProgram_, "before"), 0);
    gl.Uniform1i(gl.GetUniformLocation(resolveProgram_, "sum"), 1);
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (alpha >= 0)
    {
        g.alphaVersion++;
    }
}

// A copy of a colour target (the destination a batch's shader reads, the texture of a target drawn into)
GLuint GlDevice::CopyTargetRegion(Target& target, const Rect* region)
{
    auto key = std::make_tuple(target.width * target.scale, target.height * target.scale);
    auto found = copies_.find(key);
    GLuint texture = 0;
    GLuint fbo = 0;
    Gl& gl = GetGl();
    if (found == copies_.end())
    {
        gl.GenTextures(1, &texture);
        gl.BindTexture(GL_TEXTURE_2D, texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(target.width * target.scale),
                      static_cast<GLsizei>(target.height * target.scale), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        copies_[key] = {texture, fbo};
    }
    else
    {
        texture = found->second.first;
        fbo = found->second.second;
    }

    GLint w = static_cast<GLint>(target.width * target.scale);
    GLint h = static_cast<GLint>(target.height * target.scale);
    GLint x0 = 0;
    GLint y0 = 0;
    GLint x1 = w;
    GLint y1 = h;
    if (region != nullptr)
    {
        // Only the part the batch reads (its primitives' rectangle, a pixel more each way)
        const GLint n = static_cast<GLint>(target.scale);
        x0 = std::clamp((region->left - 1) * n, 0, w);
        y0 = std::clamp((region->top - 1) * n, 0, h);
        x1 = std::clamp((region->right + 2) * n, 0, w);
        y1 = std::clamp((region->bottom + 2) * n, 0, h);
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    Pass(fbo, "target copy");
    CopyRect(copyColourProgram_, target.texture, static_cast<f32>(w), static_cast<f32>(h), x0, y0, x1, y1);
    return texture;
}

Target* GlDevice::ScratchStencil(Target* colour)
{
    auto found = scratchStencils_.find(colour);
    if (found != scratchStencils_.end())
    {
        return found->second.get();
    }

    auto made = std::make_unique<Target>();
    made->width = colour->width;
    made->height = colour->height;
    made->scale = colour->scale;
    made->depth = true;
    made->complete = colour->complete;
    Gl& gl = GetGl();
    GLuint texture = 0;
    gl.GenTextures(1, &texture);
    made->texture = texture;
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH32F_STENCIL8), static_cast<GLsizei>(colour->width * colour->scale),
                  static_cast<GLsizei>(colour->height * colour->scale), 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
    Target* raw = made.get();
    scratchStencils_[colour] = std::move(made);
    state_[raw];
    return raw;
}

void GlDevice::DrawBatch(const BatchKey& k, const std::vector<HwVertex>& vertexes, const std::vector<BatchShape>& shapes)
{
    Gl& gl = GetGl();
    const GLsizei n = static_cast<GLsizei>(k.colour->scale);
    const GLsizei width = static_cast<GLsizei>(k.colour->width) * n;
    const GLsizei height = static_cast<GLsizei>(k.colour->height) * n;
    GlTarget& colour = state_[k.colour];
    // The destination as it was before the batch, for the shader's blend and the destination alpha test
    // A batch other than a wrapping sum sees the sums added first
    if (!k.wrapAdd && colour.accumPending)
    {
        Resolve(*k.colour);
    }

    Target* depthStencil = k.depth;
    if (k.stencilDate && depthStencil == nullptr)
    {
        depthStencil = ScratchStencil(k.colour);
    }

    GlTarget* stencil = depthStencil != nullptr ? &state_[depthStencil] : nullptr;
    // The stencil the destination alpha test made last time on this buffer is good while the target's alpha, DATM and the
    // scissor are the same: no copy, no prepass
    const bool stencilKept = k.stencilDate && stencil->stencilFor == k.colour && stencil->stencilDatm == k.datm &&
                             stencil->stencilAlphaVersion == colour.alphaVersion &&
                             std::memcmp(stencil->stencilScissor, k.scissor, sizeof(k.scissor)) == 0;
    GLuint destination = 0;
    if ((k.program & BitShaderBlend) || (k.stencilDate && !stencilKept))
    {
        // The shader's blend reads the batch's rectangle; the stencil's pass the whole scissor
        Rect region = {0x7FFF, 0x7FFF, -1, -1};
        for (const BatchShape& shape : shapes)
        {
            const Rect& r = shape.rect;
            region.left = std::min(region.left, r.left);
            region.top = std::min(region.top, r.top);
            region.right = std::max(region.right, r.right);
            region.bottom = std::max(region.bottom, r.bottom);
        }

        bool regional = (k.program & BitShaderBlend) && !k.stencilDate && region.right >= region.left;
        destination = CopyTargetRegion(*k.colour, regional ? &region : nullptr);
        stats_.copies++;
        static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
        if (counting)
        {
            static std::map<std::string, u64> counts;
            static u64 total = 0;
            char detail[160];
            std::snprintf(detail, sizeof(detail), "copy of %x/%u class %u: %s, %zu vertexes", k.colour->bp, k.colour->bw, k.colour->cls,
                          (k.program & BitShaderBlend) ? "shader blend" : "stencil DATE", vertexes.size());
            counts[detail]++;
            if (++total % 400 == 0)
            {
                for (auto& entry : counts)
                {
                    std::fprintf(stderr, "copies: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
                }
            }
        }
    }

    // The depth target as floats, for the shader's native depth test
    GLuint depthCopy = 0;
    if ((k.program & BitNativeDepth) && k.depth != nullptr)
    {
        const u32 w = k.depth->width * k.depth->scale;
        const u32 h = k.depth->height * k.depth->scale;
        auto& copy = depthCopies_[{w, h}];
        if (copy.first == 0)
        {
            gl.GenTextures(1, &copy.first);
            gl.BindTexture(GL_TEXTURE_2D, copy.first);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32F), static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RED, GL_FLOAT, nullptr);
            gl.GenFramebuffers(1, &copy.second);
            gl.BindFramebuffer(GL_FRAMEBUFFER, copy.second);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.first, 0);
        }

        gl.BindFramebuffer(GL_FRAMEBUFFER, copy.second);
        Pass(copy.second, "depth copy");
        CopyRect(copyColourProgram_, k.depth->texture, static_cast<f32>(w), static_cast<f32>(h), 0, 0, static_cast<s32>(w), static_cast<s32>(h));
        depthCopy = copy.first;
    }

    if (k.wrapAdd >= 2 && colour.accumAlpha >= 0 && colour.accumAlpha != static_cast<s32>(k.wrapAdd - 2))
    {
        // Sums writing the other alpha bit: the last one written is what shows, so those before are added first
        Resolve(*k.colour);
    }

    if (k.wrapAdd)
    {
        // Into the target's float buffer of sums (cleared when a run of them starts)
        gl.BindFramebuffer(GL_FRAMEBUFFER, AccumFramebufferFor(k.colour, depthStencil));
        Pass(AccumFramebufferFor(k.colour, depthStencil), "draw (wrap-add sums)");
        if (!colour.accumPending)
        {
            gl.Disable(GL_SCISSOR_TEST);
            gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            colour.accumPending = true;
        }

        if (k.wrapAdd >= 2)
        {
            colour.accumAlpha = static_cast<s32>(k.wrapAdd - 2);
        }
    }
    else
    {
        gl.BindFramebuffer(GL_FRAMEBUFFER, FramebufferFor(k.colour, depthStencil));
        Pass(FramebufferFor(k.colour, depthStencil), "draw");
    }

    gl.Viewport(0, 0, width, height);
    gl.Enable(GL_SCISSOR_TEST);
    gl.Scissor(k.scissor[0] * n, k.scissor[1] * n, (k.scissor[2] - k.scissor[0] + 1) * n, (k.scissor[3] - k.scissor[1] + 1) * n);
    gl.Disable(GL_STENCIL_TEST);
    bool stencilReady = k.stencilDate && stencil->stencilFor == k.colour && stencil->stencilDatm == k.datm &&
                        stencil->stencilAlphaVersion == colour.alphaVersion &&
                        std::memcmp(stencil->stencilScissor, k.scissor, sizeof(k.scissor)) == 0;
    if (k.stencilDate && stencilReady)
    {
        gl.Enable(GL_STENCIL_TEST);
        gl.StencilFunc(GL_EQUAL, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gl.StencilMask(0);
    }
    else if (k.stencilDate)
    {
        stencil->stencilFor = k.colour;
        stencil->stencilDatm = k.datm;
        stencil->stencilAlphaVersion = colour.alphaVersion;
        std::memcpy(stencil->stencilScissor, k.scissor, sizeof(k.scissor));
        // Stencil 1 where the destination's alpha bit is DATM, then the draw only there
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_BLEND);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl.DepthMask(GL_FALSE);
        gl.Enable(GL_STENCIL_TEST);
        gl.StencilMask(0xFF);
        gl.ClearStencil(0);
        gl.Clear(GL_STENCIL_BUFFER_BIT);
        gl.StencilFunc(GL_ALWAYS, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        gl.UseProgram(dateProgram_);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, destination);
        gl.Uniform1i(gl.GetUniformLocation(dateProgram_, "destination"), 0);
        gl.Uniform1i(gl.GetUniformLocation(dateProgram_, "datm"), static_cast<GLint>(k.datm));
        gl.BindVertexArray(emptyArray_);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl.StencilFunc(GL_EQUAL, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gl.StencilMask(0);
    }

    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_PROGRAM_POINT_SIZE);
    if (k.depth != nullptr)
    {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GlDepthFunc(k.depthFunc));
    }
    else
    {
        gl.Disable(GL_DEPTH_TEST);
    }

    if (k.wrapAdd)
    {
        gl.Enable(GL_BLEND);
        gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        // The alpha counts the batch's pixels (written only where the alpha bit is: the colour mask)
        gl.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
    }
    else if (k.blend)
    {
        gl.Enable(GL_BLEND);
        gl.BlendEquationSeparate(GlBlendOp(k.blendOp), GL_FUNC_ADD);
        gl.BlendFuncSeparate(GlBlendFactor(k.blendSource), GlBlendFactor(k.blendDestination), GL_ONE, GL_ZERO);
        f32 fix = std::min(static_cast<f32>(k.blendFix) / 128.0f, 1.0f);
        gl.BlendColor(0.0f, 0.0f, 0.0f, fix);
    }
    else
    {
        gl.Disable(GL_BLEND);
    }

    if (k.texture != 0)
    {
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, k.texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GlFilter(k.minFilter)));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GlFilter(k.magFilter)));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(k.clampS ? GL_CLAMP_TO_EDGE : GL_REPEAT));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(k.clampT ? GL_CLAMP_TO_EDGE : GL_REPEAT));
        gl.TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, static_cast<f32>(k.anisotropy != 0 ? k.anisotropy : 1));
    }

    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexes.size() * sizeof(HwVertex)), vertexes.data(), GL_STREAM_DRAW);

    auto draw = [&](bool failPass, bool colourOn, bool alphaOn, bool depthOn) {
        const Program& p = ProgramFor(k.program | (failPass ? BitFailPass : 0));
        gl.UseProgram(p.id);
        // A viewport far bigger than the target (the scissor keeps the drawing in it): primitives reaching past the target
        // aren't clipped, which would move their edges by the rasteriser's snapping of the new vertexes (a sixteenth of a
        // pixel, enough to lose a thin primitive's pixels); powers of two, so the positions stay exact
        static const bool smallViewport = std::getenv("TWIN_HW_SMALL_VIEWPORT") != nullptr;
        if (smallViewport)
        {
            gl.Viewport(0, 0, width, height);
            gl.Uniform2f(p.viewSize, static_cast<f32>(width), static_cast<f32>(height));
            gl.Uniform2f(p.viewOrigin, 0.0f, 0.0f);
        }
        else
        {
            gl.Viewport(-BigViewOrigin, -BigViewOrigin, BigViewSize, BigViewSize);
            gl.Uniform2f(p.viewSize, static_cast<f32>(BigViewSize), static_cast<f32>(BigViewSize));
            gl.Uniform2f(p.viewOrigin, static_cast<f32>(BigViewOrigin), static_cast<f32>(BigViewOrigin));
        }

        gl.Uniform1f(p.zMax, k.zMax);
        gl.Uniform1ui(p.zMaxU, k.zMaxU);
        gl.Uniform1i(p.depthReverse, static_cast<GLint>(k.depthReverse));
        if (k.program & BitNativeDepth)
        {
            gl.ActiveTexture(GL_TEXTURE0 + 4);
            gl.BindTexture(GL_TEXTURE_2D, depthCopy);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.Uniform1i(p.depthCopy, 4);
            gl.Uniform1i(p.nativeZtst, static_cast<GLint>(k.nativeDepthTest));
        }
        if (k.program & (BitNativeDepth | BitNativeTexel))
        {
            gl.Uniform1i(p.targetScale, static_cast<GLint>(n));
        }
        gl.Uniform1f(p.pointSize, static_cast<f32>(n));
        gl.Uniform1i(p.tex, 0);
        gl.Uniform2f(p.texSize, k.gsTexWidth, k.gsTexHeight);
        gl.Uniform2f(p.texActual, k.texWidth, k.texHeight);
        gl.Uniform1f(p.texSnap, static_cast<f32>(k.texSnap));
        gl.Uniform1f(p.aref, static_cast<f32>(k.aref));
        gl.Uniform3f(p.fogColour, static_cast<f32>(k.fogColour & 0xFF), static_cast<f32>((k.fogColour >> 8) & 0xFF),
                     static_cast<f32>((k.fogColour >> 16) & 0xFF));
        gl.Uniform1f(p.ta0, static_cast<f32>(k.ta0));
        gl.Uniform1f(p.ta1, static_cast<f32>(k.ta1));
        gl.Uniform1i(p.aem, static_cast<GLint>(k.aem));
        gl.Uniform1f(p.lodK, k.lodK);
        gl.Uniform1f(p.lodL, std::exp2(static_cast<f32>(k.lodL)));
        gl.Uniform1f(p.lodMax, static_cast<f32>(k.mxl));
        GLint dimx[16];
        for (u32 i = 0; i < 16; i++)
        {
            s32 value = static_cast<s32>((k.dimx >> (i * 4)) & 7);
            dimx[i] = value & 4 ? value - 8 : value;
        }

        gl.Uniform1iv(p.dimx, 16, dimx);
        gl.Uniform1f(p.scale, static_cast<f32>(k.ditherScale != 0 ? k.ditherScale : 1));
        gl.Uniform1i(p.colclamp, static_cast<GLint>(k.colclamp));
        if (k.program & BitRegion)
        {
            gl.Uniform4i(p.regionMode, static_cast<GLint>(k.regionMode[0]), static_cast<GLint>(k.regionMode[1]),
                         static_cast<GLint>(k.regionMode[2]), static_cast<GLint>(k.regionMode[3]));
            gl.Uniform4i(p.regionBounds, static_cast<GLint>(k.regionBounds[0]), static_cast<GLint>(k.regionBounds[1]),
                         static_cast<GLint>(k.regionBounds[2]), static_cast<GLint>(k.regionBounds[3]));
        }

        if (k.program & BitWrapAdd)
        {
            gl.Uniform1i(p.blendA, static_cast<GLint>(k.gsBlend[0]));
            gl.Uniform1i(p.blendB, static_cast<GLint>(k.gsBlend[1]));
            gl.Uniform1i(p.blendC, static_cast<GLint>(k.gsBlend[2]));
            gl.Uniform1f(p.blendFix, static_cast<f32>(k.gsFix));
        }

        if (k.program & BitShaderBlend)
        {
            gl.ActiveTexture(GL_TEXTURE0 + 1);
            gl.BindTexture(GL_TEXTURE_2D, destination);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.Uniform1i(p.destination, 1);
            bool blending = k.gsBlend[0] != 0 || k.gsBlend[1] != 0 || k.gsBlend[2] != 0 || k.gsBlend[3] != 0 || k.gsFix != 0;
            gl.Uniform1i(p.blendOn, blending ? 1 : 0);
            gl.Uniform1i(p.blendA, static_cast<GLint>(k.gsBlend[0]));
            gl.Uniform1i(p.blendB, static_cast<GLint>(k.gsBlend[1]));
            gl.Uniform1i(p.blendC, static_cast<GLint>(k.gsBlend[2]));
            gl.Uniform1i(p.blendD, static_cast<GLint>(k.gsBlend[3]));
            gl.Uniform1f(p.blendFix, static_cast<f32>(k.gsFix));
            gl.Uniform1i(p.pabe, static_cast<GLint>(k.pabe));
            gl.Uniform1i(p.datm, static_cast<GLint>(k.datm));
            gl.Uniform4ui(p.frameMask, k.frameMask[0], k.frameMask[1], k.frameMask[2], k.frameMask[3]);
        }
        u32 mask = k.colourMask;
        gl.ColorMask(colourOn && (mask & 1) ? GL_TRUE : GL_FALSE, colourOn && (mask & 2) ? GL_TRUE : GL_FALSE,
                     colourOn && (mask & 4) ? GL_TRUE : GL_FALSE, colourOn && alphaOn && (mask & 8) ? GL_TRUE : GL_FALSE);
        gl.DepthMask(depthOn && k.depthWrite ? GL_TRUE : GL_FALSE);
        gl.DrawArrays(GlTopology(k.primitive), 0, static_cast<GLsizei>(vertexes.size()));
    };

    // The pixels passing the alpha test with every write; then a failed test's AFAIL: the failing pixels with the frame only
    // (FB_ONLY), the depth only (ZB_ONLY) or the colour without alpha (RGB_ONLY)
    bool fails = k.alphaTested != 0;
    u32 atst = (k.program >> BitAtstShift) & 7;
    if (!fails || atst != Gs::AtstNever)
    {
        draw(false, true, true, true);
    }

    if (fails && k.afail != Gs::AfailKeep)
    {
        switch (k.afail)
        {
        case Gs::AfailFbOnly:
            draw(true, true, true, false);
            break;
        case Gs::AfailZbOnly:
            draw(true, false, false, true);
            break;
        default:
            draw(true, true, false, false);
            break;
        }
    }

    gl.Viewport(0, 0, width, height);
    // Debugging: TWIN_HW_TRACK=BP,X,Y (hex BP, the target's own pixels): the pixel after each batch into it, when it changes
    static const char* track = std::getenv("TWIN_HW_TRACK");
    if (track != nullptr)
    {
        u32 bp = 0;
        int tx = 0, ty = 0;
        static u32 last = ~0u;
        if (std::sscanf(track, "%x,%d,%d", &bp, &tx, &ty) == 3 && k.colour->bp == bp && !k.wrapAdd)
        {
            u8 c[4] = {};
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, colour.fbo);
            gl.ReadPixels(tx, ty, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
            u32 v = c[0] | c[1] << 8 | c[2] << 16 | static_cast<u32>(c[3]) << 24;
            if (v != last)
            {
                std::fprintf(stderr, "track: #%llu class %u program %x -> %08x\n", static_cast<unsigned long long>(gs_.primitivesDrawn), k.colour->cls,
                             k.program, v);
                last = v;
            }
        }
    }

    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.DepthMask(GL_TRUE);
    gl.Disable(GL_STENCIL_TEST);
    gl.StencilMask(0xFF);
    // A batch that can write the colour target's alpha makes its stencils out of date
    if ((k.colourMask & 8) != 0 || (k.program & BitShaderBlend) != 0)
    {
        colour.alphaVersion++;
    }
}

bool GlDevice::ReadTargetPixel(Target& t, u32 x, u32 y, u32* value)
{
    Gl& gl = GetGl();
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, state_[&t].fbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    GLint sx = static_cast<GLint>(x * t.scale);
    GLint sy = static_cast<GLint>(y * t.scale);
    if (t.depth)
    {
        f32 d = 0.0f;
        gl.ReadPixels(sx, sy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
        *value = static_cast<u32>(std::min(std::floor(static_cast<double>(d) * 4294967296.0 + 0.5), 4294967295.0));
        *value = t.reversed ? 0xFFFFFFFFu - *value : *value;
    }
    else
    {
        u8 c[4];
        gl.ReadPixels(sx, sy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
        *value = static_cast<u32>(c[0]) | static_cast<u32>(c[1]) << 8 | static_cast<u32>(c[2]) << 16 | static_cast<u32>(c[3]) << 24;
    }

    return true;
}

void GlDevice::ReadTargetScaled(Target& t, std::vector<u32>& pixels)
{
    Gl& gl = GetGl();
    pixels.resize(static_cast<size_t>(t.width * t.scale) * t.height * t.scale);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, state_[&t].fbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.ReadPixels(0, 0, static_cast<GLsizei>(t.width * t.scale), static_cast<GLsizei>(t.height * t.scale), GL_RGBA, GL_UNSIGNED_BYTE,
                  pixels.data());
}

void GlDevice::DropCopies()
{
    Gl& gl = GetGl();
    for (auto& copy : copies_)
    {
        gl.DeleteTextures(1, &copy.second.first);
        gl.DeleteFramebuffers(1, &copy.second.second);
    }

    copies_.clear();
}

void GlDevice::PictureDone()
{
    if (!passCounts_.empty() && ++passFrames_ % 60 == 0)
    {
        u64 total = 0;
        for (auto& entry : passCounts_)
        {
            total += entry.second;
        }

        std::fprintf(stderr, "passes: %.1f a frame:", static_cast<double>(total) / 60.0);
        for (auto& entry : passCounts_)
        {
            std::fprintf(stderr, " %s %.1f;", entry.first.c_str(), static_cast<double>(entry.second) / 60.0);
        }

        std::fprintf(stderr, "\n");
        passCounts_.clear();
    }
}

TextureId GlDevice::MergeCircuits(const CircuitPicture* circuits, u32 width, u32 height, f32 alp, bool ownAlpha, u32 background)
{
    Gl& gl = GetGl();
    if (mergeTexture_ == 0 || mergeWidth_ != width || mergeHeight_ != height)
    {
        if (mergeTexture_ == 0)
        {
            gl.GenTextures(1, &mergeTexture_);
            gl.GenFramebuffers(1, &mergeFbo_);
        }

        gl.BindTexture(GL_TEXTURE_2D, mergeTexture_);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, mergeFbo_);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mergeTexture_, 0);
        mergeWidth_ = width;
        mergeHeight_ = height;
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, mergeFbo_);
    gl.Viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(mergeProgram_);
    auto uniform = [&](const char* name) { return gl.GetUniformLocation(mergeProgram_, name); };
    const char* names[2][5] = {{"circuit1", "enabled1", "place1", "corner1", "size1"}, {"circuit2", "enabled2", "place2", "corner2", "size2"}};
    for (u32 i = 0; i < 2; i++)
    {
        const CircuitPicture& c = circuits[i];
        gl.ActiveTexture(GL_TEXTURE0 + i);
        gl.BindTexture(GL_TEXTURE_2D, c.enabled ? c.target->texture : circuits[i ^ 1].target->texture);
        gl.Uniform1i(uniform(names[i][0]), static_cast<GLint>(i));
        gl.Uniform1i(uniform(names[i][1]), c.enabled ? 1 : 0);
        gl.Uniform4f(uniform(names[i][2]), c.place[0], c.place[1], c.place[2], c.place[3]);
        gl.Uniform2f(uniform(names[i][3]), c.corner[0], c.corner[1]);
        gl.Uniform2f(uniform(names[i][4]), c.size[0], c.size[1]);
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1f(uniform("alp"), alp);
    gl.Uniform1i(uniform("ownAlpha"), ownAlpha ? 1 : 0);
    gl.Uniform3f(uniform("background"), static_cast<f32>(background & 0xFF) / 255.0f, static_cast<f32>((background >> 8) & 0xFF) / 255.0f,
                 static_cast<f32>((background >> 16) & 0xFF) / 255.0f);
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return mergeTexture_;
}

void GlDevice::ReadPicture(const Picture& picture, std::vector<u32>& pixels)
{
    Gl& gl = GetGl();
    pixels.resize(static_cast<size_t>(picture.width) * picture.height);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, PictureFramebuffer(picture));
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.ReadPixels(picture.left, picture.top, static_cast<GLsizei>(picture.width), static_cast<GLsizei>(picture.height), GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels.data());
}

bool GlDevice::CopyPicture(const Picture& picture, u32 slot, void* drawnFence, ShownTexture* shown)
{
    Gl& gl = GetGl();
    if (drawnFence != nullptr)
    {
        GLsync drawn = static_cast<GLsync>(drawnFence);
        gl.WaitSync(drawn, 0, GL_TIMEOUT_IGNORED);
        gl.DeleteSync(drawn);
    }

    ShownCopy& copy = shownCopies_[slot % 3];
    if (copy.texture == 0 || copy.width != picture.width || copy.height != picture.height)
    {
        if (copy.texture == 0)
        {
            gl.GenTextures(1, &copy.texture);
            gl.GenFramebuffers(1, &copy.fbo);
        }

        gl.BindTexture(GL_TEXTURE_2D, copy.texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(picture.width),
                      static_cast<GLsizei>(picture.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, copy.fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.texture, 0);
        copy.width = picture.width;
        copy.height = picture.height;
    }

    // The picture's rectangle of its target (or the circuits merged), rows as they are: the copy's first row is the top one
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, PictureFramebuffer(picture));
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, copy.fbo);
    gl.Disable(GL_SCISSOR_TEST);
    gl.BlitFramebuffer(picture.left, picture.top, picture.left + static_cast<GLint>(picture.width),
                       picture.top + static_cast<GLint>(picture.height), 0, 0, static_cast<GLint>(picture.width),
                       static_cast<GLint>(picture.height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    shown->texture = copy.texture;
    shown->width = picture.width;
    shown->height = picture.height;
    shown->fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.Flush();
    return true;
}
}

std::unique_ptr<GpuDevice> MakeGlDevice(HardwareStats& stats, const Gs::Gs& gs)
{
    return std::make_unique<GlDevice>(stats, gs);
}
}
