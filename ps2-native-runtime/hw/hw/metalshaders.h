#pragma once

// The Metal device's shaders (metaldevice.mm), compiled when the device starts. The draw's fragment shader is the software
// GS's pixel pipeline (gsdraw.cpp's ShadePixel) on the target's own pixel, read in the shader (programmable blending): the
// texture function, the alpha test and AFAIL, fog, the depth test on the GS's integer values, the destination alpha test, the
// blend, dithering, the clamp or wrap, FBA and the frame mask, bit for bit. The texture's sampling and the vertexes' placing
// at a scale are the OpenGL device's (gldevice.cpp), so the two draw the same pixels where their rules are the same. GS
// memory's copy is a buffer of its words; the swizzle tables and the conversions between it and the targets are gldevice.cpp's
// in compute kernels.

namespace NativeGraphics
{
constexpr const char* MetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

// ---- What the draws and the kernels share with the device (metaldevice.mm's copies of these are the same layout)

struct HwVertex
{
    float x;
    float y;
    uint z;
    uchar4 colour;
    float s;
    float t;
    float q;
    float fog;
};

struct VertexUniforms
{
    float2 viewSize;
    float pointSize;
    uint perPrimitive;
    uint zmax;
};

struct DrawUniforms
{
    float2 texSize;
    float2 texActual;
    float texSnap;
    int targetScale;
    float lodK;
    float lodL;
    float lodMax;
    uint ta0;
    uint ta1;
    uint aem;
    uint fogR;
    uint fogG;
    uint fogB;
    // The frame: its kind (0 32 bit, 1 24 bit, 2 16 bit), the mask, whether it's written
    uint frameKind;
    uint fbmsk;
    uint frameWrite;
    // The depth buffer: its kind (1 Z32, 2 Z24, 3 Z16; 0 none), the test, whether it's written, its largest value
    uint depthKind;
    uint ztst;
    uint zWrite;
    uint zmax;
    uint alphaTest;
    uint atst;
    uint aref;
    uint afail;
    uint date;
    uint datm;
    uint blend;
    uint blendA;
    uint blendB;
    uint blendC;
    uint blendD;
    uint blendFix;
    uint pabe;
    uint colclamp;
    uint fba;
    uint dither;
    uint ditherScale;
    // The GS's sampling: the texture's levels, the magnification filter, MMIN, MXL
    uint levelCount;
    uint magLinear;
    uint mmin;
    uint mxl;
    // The draw's features: textured, TFX, TCC, fog, UV coordinates, the texture's alpha from TEXA (1 24 bit, 2 16 bit), mipmaps,
    // a constant level of detail, the texel at the block's first sample, region modes, the GS's own sampling
    uint textured;
    uint tfx;
    uint tcc;
    uint fog;
    uint uvMode;
    uint texAlpha;
    uint mipmap;
    uint lodConst;
    uint nativeTexel;
    uint region;
    uint gsSample;
    // The depth test at the GS's sample point (the block's first sample) against a copy of the depth made when the batch began:
    // its ZTST, 0 for the test at each sample (OpenGL's nativeDepthTest)
    uint nativeZtst;
    int dimx[16];
    // The wrap modes (WMS, WMT), the region's filter (region modes alone), a target's samples a texel; MINU, MAXU, MINV, MAXV
    int4 regionMode;
    int4 regionBounds;
};

// ---- The draw

struct Varyings
{
    float4 position [[position]];
    float pointSize [[point_size]];
    float4 colour [[center_no_perspective]];
    float3 stq [[center_no_perspective]];
    float fog [[center_no_perspective]];
    float depth [[center_no_perspective]];
    // The primitive's depth when its vertexes all have the same (exact; a float holds 24 bits)
    uint zFlat [[flat]];
    uint zIsFlat [[flat]];
};

vertex Varyings DrawVertex(uint id [[vertex_id]], device const HwVertex* vertexes [[buffer(0)]],
                           constant VertexUniforms& u [[buffer(1)]])
{
    HwVertex v = vertexes[id];
    Varyings out;
    // Pixel coordinates at the scale, row 0 the top (the target's first row, as GS memory's)
    out.position = float4(v.x / u.viewSize.x * 2.0 - 1.0, 1.0 - v.y / u.viewSize.y * 2.0, 0.0, 1.0);
    out.pointSize = u.pointSize;
    out.colour = float4(v.colour);
    out.stq = float3(v.s, v.t, v.q);
    out.fog = v.fog;
    out.depth = float(min(v.z, u.zmax));
    uint first = id / u.perPrimitive * u.perPrimitive;
    bool same = true;
    for (uint i = 0; i < u.perPrimitive; i++)
    {
        same = same && vertexes[first + i].z == v.z;
    }

    out.zFlat = min(v.z, u.zmax);
    out.zIsFlat = same ? 1u : 0u;
    return out;
}

// A draw's feature: a specialised pipeline's constant (k_NAME, declared before this source by the device: metaldevice.mm's
// SpecialisedFeatures, compiled away with the branches it rules out), else the draw's constants' (the general pipeline)
#define FEATURE(name) (is_function_constant_defined(k_##name) ? k_##name : u.name)

// A texel coordinate wrapped by CLAMP's mode as the software GS wraps it (its region modes kept inside the level)
int RegionWrap(int c, int mode, int size, int low, int high)
{
    return mode == 0 ? c & (size - 1) : mode == 1 ? clamp(c, 0, size - 1)
                                      : mode == 2 ? min(c < low ? low : high < c ? high : c, size - 1)
                                                  : min((c & low) | high, size - 1);
}

// A sample of the texture (n samples a texel across and down): its texel wrapped, a clamped texel's samples clamped to its
// edge's, as the software GS reads a shadow
float4 RegionTexel(texture2d<float> tex, constant DrawUniforms& u, int2 at)
{
    int n = u.regionMode.w;
    int2 texel = int2(floor(float2(at) / float(n)));
    int2 within = at - texel * n;
    int2 size = int2(u.texSize);
    int2 wrapped = int2(RegionWrap(texel.x, u.regionMode.x, size.x, u.regionBounds.x, u.regionBounds.y),
                        RegionWrap(texel.y, u.regionMode.y, size.y, u.regionBounds.z, u.regionBounds.w));
    if ((u.regionMode.x == 1 || u.regionMode.x == 2) && wrapped.x != texel.x)
    {
        within.x = texel.x < wrapped.x ? 0 : n - 1;
    }
    if ((u.regionMode.y == 1 || u.regionMode.y == 2) && wrapped.y != texel.y)
    {
        within.y = texel.y < wrapped.y ? 0 : n - 1;
    }
    return floor(tex.read(uint2(wrapped * n + within)) * 255.0 + 0.5);
}

// The GS's bilinear step: a + (b - a) * f >> 4
float4 Lerp4(float4 a, float4 b, float f)
{
    return a + floor((b - a) * f / 16.0);
}

// The texture read as the GS reads it with a region clamp or repeat: the sample at the coordinates, or the four round them
// (half a sample off) weighted by 4 bit fractions, each wrapped on its own
float4 RegionSample(texture2d<float> tex, constant DrawUniforms& u, float2 uv)
{
    float2 at = uv * float(u.regionMode.w);
    if (u.regionMode.z == 0)
    {
        return RegionTexel(tex, u, int2(floor(at)));
    }
    int2 sixteenths = int2(floor((at - 0.5) * 16.0));
    int2 i = sixteenths >> 4;
    float2 f = float2(sixteenths & 15);
    float4 top = Lerp4(RegionTexel(tex, u, i), RegionTexel(tex, u, i + int2(1, 0)), f.x);
    float4 bottom = Lerp4(RegionTexel(tex, u, i + int2(0, 1)), RegionTexel(tex, u, i + int2(1, 1)), f.x);
    return Lerp4(top, bottom, f.y);
}

// A sample of a level as the software GS reads it (gsdraw.cpp's ShadowTexel: n samples a texel, a clamped texel's samples
// clamped to its edge's)
float4 GsTexel(texture2d<float> tex, constant DrawUniforms& u, int2 at, uint level)
{
    int n = u.regionMode.w;
    int2 size = max(int2(u.texSize) >> int(level), int2(1));
    int4 bounds = u.regionBounds >> int(level);
    int2 texel = int2(floor(float2(at) / float(n)));
    int2 within = at - texel * n;
    int2 wrapped = int2(RegionWrap(texel.x, u.regionMode.x, size.x, bounds.x, bounds.y),
                        RegionWrap(texel.y, u.regionMode.y, size.y, bounds.z, bounds.w));
    if ((u.regionMode.x == 1 || u.regionMode.x == 2) && wrapped.x != texel.x)
    {
        within.x = texel.x < wrapped.x ? 0 : n - 1;
    }
    if ((u.regionMode.y == 1 || u.regionMode.y == 2) && wrapped.y != texel.y)
    {
        within.y = texel.y < wrapped.y ? 0 : n - 1;
    }
    return floor(tex.read(uint2(wrapped * n + within), level) * 255.0 + 0.5);
}

// A level read at texel coordinates (SampleLevel): the nearest texel, or the four round the point half a texel back weighted by
// 4 bit fractions; a target at a higher scale read unfiltered at each texel's first sample (the GS's sample point)
float4 GsLevel(texture2d<float> tex, constant DrawUniforms& u, float2 uv, uint level, bool linear)
{
    float2 texels = uv / float(1u << level);
    float2 at = texels * float(u.regionMode.w);
    if (!linear)
    {
        if (u.texSnap > 0.0)
        {
            at = floor(texels) * float(u.regionMode.w);
        }
        return GsTexel(tex, u, int2(floor(at)), level);
    }
    int2 sixteenths = int2(floor((at - 0.5) * 16.0));
    int2 i = sixteenths >> 4;
    float2 f = float2(sixteenths & 15);
    float4 top = Lerp4(GsTexel(tex, u, i, level), GsTexel(tex, u, i + int2(1, 0), level), f.x);
    float4 bottom = Lerp4(GsTexel(tex, u, i + int2(0, 1), level), GsTexel(tex, u, i + int2(1, 1), level), f.x);
    return Lerp4(top, bottom, f.y);
}

// The texture's colour as the GS works it out (SampleTexture): the level of detail picks the magnification filter (LOD <= 0)
// or MMIN's, and with mipmaps the level (rounded, or the two round it blended by the fraction)
float4 GsSample(texture2d<float> tex, constant DrawUniforms& u, float2 uv, float q)
{
    if (FEATURE(mxl) == 0)
    {
        return GsLevel(tex, u, uv, 0, FEATURE(magLinear) != 0);
    }
    float lod = u.lodConst != 0 ? u.lodK : -log2(abs(q)) * u.lodL + u.lodK;
    if (lod <= 0.0 || FEATURE(levelCount) == 1)
    {
        return GsLevel(tex, u, uv, 0, lod <= 0.0 ? FEATURE(magLinear) != 0 : (FEATURE(mmin) & 1) != 0);
    }
    bool linear = FEATURE(mmin) >= 4;
    float maxLevel = float(FEATURE(levelCount) - 1);
    if ((FEATURE(mmin) & 1) == 0)
    {
        return GsLevel(tex, u, uv, uint(min(floor(lod + 0.5), maxLevel)), linear);
    }
    float clamped = min(lod, maxLevel);
    uint level = uint(floor(clamped));
    float fraction = float(uint((clamped - float(level)) * 16.0) & 15u);
    float4 c0 = GsLevel(tex, u, uv, level, linear);
    if (level + 1 >= FEATURE(levelCount) || fraction == 0.0)
    {
        return c0;
    }
    return Lerp4(c0, GsLevel(tex, u, uv, level + 1, linear), fraction);
}

struct Pixel
{
    float4 colour [[color(0)]];
#if DEPTH
    uint depth [[color(1)]];
#endif
};

uint Pick(uint select, uint source, uint destination)
{
    return select == 0 ? source : select == 1 ? destination : 0u;
}

fragment Pixel DrawFragment(Varyings in [[stage_in]], float4 destination [[color(0)]],
#if DEPTH
                            uint destinationDepth [[color(1)]],
#endif
                            texture2d<float> tex [[texture(0)]], texture2d<uint> depthCopy [[texture(1)]], sampler smp [[sampler(0)]],
                            constant DrawUniforms& u [[buffer(0)]])
{
    // The texture's coordinates in texels (derivatives taken before anything diverges)
    float2 uv = FEATURE(uvMode) != 0 ? in.stq.xy : in.stq.xy / in.stq.z * u.texSize;
    float2 uvAcross = dfdx(uv);
    float2 uvDown = dfdy(uv);
    if (FEATURE(nativeTexel) != 0)
    {
        // The texture read where the GS reads it: the block's first sample's coordinates
        float2 block = floor(in.position.xy / float(u.targetScale)) * float(u.targetScale);
        float2 back = in.position.xy - (block + 0.5);
        uv = uv - uvAcross * back.x - uvDown * back.y;
    }

    // The depth at the pixel: the primitive's own when it's the same at every vertex, else interpolated (truncated, exact while
    // below 2^24, as the OpenGL device's)
    float depthAcross = dfdx(in.depth);
    float depthDown = dfdy(in.depth);
    uint z = in.zFlat;
    if (in.zIsFlat == 0)
    {
        float d = in.depth;
        z = d < 16777216.0 ? uint(floor(max(d, 0.0) + 1.0 / 64.0)) : uint(min(d, 4294967040.0));
    }
    z = min(z, u.zmax);

    // The depth test
#if DEPTH
    uint storedDepth = destinationDepth;
    uint zd = FEATURE(depthKind) == 2 ? storedDepth & 0xFFFFFFu : FEATURE(depthKind) == 3 ? storedDepth & 0xFFFFu : storedDepth;
    if (FEATURE(nativeZtst) == 0 && ((FEATURE(ztst) == 2 && z < zd) || (FEATURE(ztst) == 3 && z <= zd)))
    {
        discard_fragment();
    }
#endif
    if (FEATURE(nativeZtst) != 0)
    {
        // A texture at the PS2's resolution made from upscaled buffers tested where the GS tests it: at the block's first
        // sample, against the depth there as the batch found it (else the samples near where the test changes pass with the
        // texel of the other side: a line along it)
        float2 block = floor(in.position.xy / float(u.targetScale)) * float(u.targetScale);
        float2 back = in.position.xy - (block + 0.5);
        float here = in.zIsFlat != 0 ? float(in.zFlat) : in.depth - depthAcross * back.x - depthDown * back.y;
        uint there = depthCopy.read(uint2(block)).r;
        there = FEATURE(depthKind) == 2 ? there & 0xFFFFFFu : FEATURE(depthKind) == 3 ? there & 0xFFFFu : there;
        uint at = min(uint(clamp(here, 0.0, 4294967040.0)), u.zmax);
        if ((FEATURE(nativeZtst) == 2 && at < there) || (FEATURE(nativeZtst) == 3 && at <= there))
        {
            discard_fragment();
        }
    }

    // The texture function (the vertexes' colours with 7 fraction bits, as the GS interpolates them)
    // (a hair added: a colour the same at every vertex, interpolated in float, can land a hair under itself)
    uint4 c7 = uint4(max(floor(in.colour * 128.0 + 1.0 / 32.0), 0.0));
    uint va = c7.a >> 7;
    uint r = c7.r >> 7;
    uint g = c7.g >> 7;
    uint b = c7.b >> 7;
    uint a = va;
    if (FEATURE(textured) != 0)
    {
        // The GS's texel coordinates are exact (S/Q times the size, on its sixteenth of a texel grid); the GPU's interpolation
        // can land a hair under a texel's edge where the GS lands on it: a 256th of a texel puts it back on the GS's side
        uv += 1.0 / 256.0;
        float4 t;
        if (FEATURE(gsSample) != 0)
        {
            t = GsSample(tex, u, uv, in.stq.z);
        }
        else if (FEATURE(region) != 0)
        {
            t = RegionSample(tex, u, uv);
        }
        else
        {
            float lod = 0.0;
            if (FEATURE(mipmap) != 0)
            {
                lod = min(FEATURE(lodConst) != 0 ? u.lodK : log2(1.0 / abs(in.stq.z)) * u.lodL + u.lodK, u.lodMax);
            }
            if (u.texSnap > 0.0)
            {
                uv = floor(uv) + 0.5 / u.texSnap;
            }
            t = floor(tex.sample(smp, uv / u.texActual, level(lod)) * 255.0 + 0.5);
        }
        if (FEATURE(texAlpha) == 1)
        {
            t.a = (u.aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : float(u.ta0);
        }
        else if (FEATURE(texAlpha) == 2)
        {
            t.rgb = floor(t.rgb / 8.0) * 8.0;
            t.a = t.a >= 128.0 ? float(u.ta1) : ((u.aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : float(u.ta0));
        }
        uint4 tx = uint4(t);
        if (FEATURE(tfx) == 1)
        {
            r = tx.r;
            g = tx.g;
            b = tx.b;
            a = FEATURE(tcc) != 0 ? tx.a : va;
        }
        else
        {
            // Modulate, and the highlights with the vertex's alpha added
            uint add = FEATURE(tfx) == 0 ? 0u : va;
            r = min(((tx.r * c7.r) >> 14) + add, 255u);
            g = min(((tx.g * c7.g) >> 14) + add, 255u);
            b = min(((tx.b * c7.b) >> 14) + add, 255u);
            a = FEATURE(tcc) == 0 ? va : FEATURE(tfx) == 0 ? min((tx.a * c7.a) >> 14, 255u) : FEATURE(tfx) == 2 ? min(tx.a + va, 255u) : tx.a;
        }
    }

    // The alpha test: a failure keeps nothing, or keeps the frame, the depth or the frame's colour
    bool writeFrame = FEATURE(frameWrite) != 0;
    bool writeDepth = FEATURE(zWrite) != 0;
    uint alphaMask = 0;
    if (FEATURE(alphaTest) != 0)
    {
        bool pass = FEATURE(atst) == 0 ? false : FEATURE(atst) == 1 ? true : FEATURE(atst) == 2 ? a < u.aref : FEATURE(atst) == 3 ? a <= u.aref
                  : FEATURE(atst) == 4 ? a == u.aref : FEATURE(atst) == 5 ? a >= u.aref : FEATURE(atst) == 6 ? a > u.aref : a != u.aref;
        if (!pass)
        {
            if (FEATURE(afail) == 0)
            {
                discard_fragment();
            }
            writeDepth = writeDepth && FEATURE(afail) == 2;
            writeFrame = writeFrame && FEATURE(afail) != 2;
            alphaMask = FEATURE(afail) == 3 ? 0xFF000000u : 0u;
        }
    }

    // Fog
    if (FEATURE(fog) != 0)
    {
        uint f = uint(max(floor(in.fog + 1.0 / 4096.0), 0.0));
        r = (f * r + (255u - f) * u.fogR) >> 8;
        g = (f * g + (255u - f) * u.fogG) >> 8;
        b = (f * b + (255u - f) * u.fogB) >> 8;
    }

    // The destination: the target's bytes (a 16 bit frame's channels as their 5 bits shifted up, its alpha bit 0x80)
    uint4 d = uint4(destination * 255.0 + 0.5);
    uint fd = d.r | d.g << 8 | d.b << 16 | d.a << 24;
    uint fd16 = (d.r >> 3) | (d.g >> 3) << 5 | (d.b >> 3) << 10 | (d.a >= 0x80u ? 0x8000u : 0u);
    if (FEATURE(date) != 0)
    {
        uint bit = FEATURE(frameKind) == 2 ? (fd16 >> 15) & 1u : fd >> 31;
        if (bit != FEATURE(datm))
        {
            discard_fragment();
        }
    }

    Pixel out;
#if DEPTH
    out.depth = storedDepth;
    if (writeDepth)
    {
        out.depth = FEATURE(depthKind) == 2 ? (storedDepth & 0xFF000000u) | (z & 0xFFFFFFu) : FEATURE(depthKind) == 3 ? (z & 0xFFFFu) : z;
    }
#endif
    out.colour = destination;
    if (!writeFrame)
    {
#if DEPTH
        if (!writeDepth)
        {
            discard_fragment();
        }
#else
        discard_fragment();
#endif
        return out;
    }

    uint dr = d.r;
    uint dg = d.g;
    uint db = d.b;
    uint da = FEATURE(frameKind) == 1 ? 0x80u : d.a;

    // The blend: ((A - B) * C >> 7) + D on each colour channel; PABE blends only pixels whose alpha's top bit is set
    int sr = int(r);
    int sg = int(g);
    int sb = int(b);
    if (FEATURE(blend) != 0 && !(FEATURE(pabe) != 0 && (a & 0x80u) == 0))
    {
        int cf = int(FEATURE(blendC) == 0 ? a : FEATURE(blendC) == 1 ? da : u.blendFix);
        sr = ((int(Pick(FEATURE(blendA), r, dr)) - int(Pick(FEATURE(blendB), r, dr))) * cf >> 7) + int(Pick(FEATURE(blendD), r, dr));
        sg = ((int(Pick(FEATURE(blendA), g, dg)) - int(Pick(FEATURE(blendB), g, dg))) * cf >> 7) + int(Pick(FEATURE(blendD), g, dg));
        sb = ((int(Pick(FEATURE(blendA), b, db)) - int(Pick(FEATURE(blendB), b, db))) * cf >> 7) + int(Pick(FEATURE(blendD), b, db));
    }

    // Dithering of 16 bit frames (the matrix's place the PS2's pixel, or the screen's), then the clamp (or the low byte kept)
    if (FEATURE(dither) != 0)
    {
        uint2 p = uint2(in.position.xy) / max(u.ditherScale, 1u);
        int dm = u.dimx[(p.y & 3) * 4 + (p.x & 3)];
        sr += dm;
        sg += dm;
        sb += dm;
    }
    uint fr = FEATURE(colclamp) != 0 ? uint(clamp(sr, 0, 255)) : uint(sr) & 0xFFu;
    uint fg = FEATURE(colclamp) != 0 ? uint(clamp(sg, 0, 255)) : uint(sg) & 0xFFu;
    uint fb = FEATURE(colclamp) != 0 ? uint(clamp(sb, 0, 255)) : uint(sb) & 0xFFu;
    uint fa = a | (FEATURE(fba) != 0 ? 0x80u : 0u);
    uint mask = u.fbmsk | alphaMask;
    uint written;
    if (FEATURE(frameKind) == 2)
    {
        uint c16 = (fr >> 3) | (fg >> 3) << 5 | (fb >> 3) << 10 | (fa & 0x80u) << 8;
        uint m16 = ((mask >> 3) & 0x1Fu) | ((mask >> 6) & 0x3E0u) | ((mask >> 9) & 0x7C00u) | ((mask >> 16) & 0x8000u);
        c16 = (c16 & ~m16) | (fd16 & m16);
        written = (c16 & 31u) << 3 | ((c16 >> 5) & 31u) << 11 | ((c16 >> 10) & 31u) << 19 | ((c16 & 0x8000u) != 0 ? 0x80000000u : 0u);
    }
    else
    {
        uint colour = fr | fg << 8 | fb << 16 | fa << 24;
        written = (colour & ~mask) | (fd & mask);
    }
    out.colour = float4(uint4(written & 0xFFu, (written >> 8) & 0xFFu, (written >> 16) & 0xFFu, written >> 24)) / 255.0;
    return out;
}

// ---- GS memory's copy (a buffer of its words) and the conversions between it and the targets, by the GS's swizzles: a
// layout's table of where each pixel of a page is (its block in the page, its unit in the block) and the inverse (a page's
// units' pixels)

struct Layout
{
    int unitsPerBlock;
    int unitShift;
    int unitBits;
    int pageWidth;
    int pageHeight;
    int pagesAcross;
    int pagesDown;
    int basePointer;
    int xorZ;
};

uint MemUnit(device const uint* mem, constant Layout& l, uint address)
{
    uint w = (address >> uint(l.unitShift)) & 0xFFFFFu;
    uint word = mem[w];
    uint sub = address & ((1u << uint(l.unitShift)) - 1u);
    uint mask = l.unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(l.unitBits)) - 1u);
    return (word >> (sub * uint(l.unitBits))) & mask;
}

uint UnitAddress(constant Layout& l, device const uint* fwd, int2 p)
{
    uint page = uint((p.y / l.pageHeight) * l.pagesAcross + p.x / l.pageWidth);
    uint f = fwd[(p.y % l.pageHeight) * l.pageWidth + p.x % l.pageWidth];
    uint block = uint(l.basePointer) + page * 32u + (f >> 16);
    if (l.xorZ != 0)
    {
        block ^= 24u;
    }
    return (block & 16383u) * uint(l.unitsPerBlock) + (f & 0xFFFFu);
}

// The pixel a unit of memory is (false: none of the buffer's)
bool UnitPixel(constant Layout& l, device const uint* inv, uint address, thread int2& p)
{
    uint block = address / uint(l.unitsPerBlock);
    uint inBlock = address % uint(l.unitsPerBlock);
    if (l.xorZ != 0)
    {
        block ^= 24u;
    }
    uint rel = (block - uint(l.basePointer)) & 16383u;
    uint page = rel >> 5;
    if (page >= uint(l.pagesAcross * l.pagesDown))
    {
        return false;
    }
    uint i = (rel & 31u) * uint(l.unitsPerBlock) + inBlock;
    uint v = inv[i];
    p = int2(int(page % uint(l.pagesAcross)) * l.pageWidth + int(v & 0xFFFFu), int(page / uint(l.pagesAcross)) * l.pageHeight + int(v >> 16));
    return true;
}

struct Conversion
{
    // 0 colour 32, 1 colour 16, 2 depth
    uint storage;
    uint targetScale;
    uint firstWord;
    uint words;
    uint pageMask[16];
    int4 rect;
};

// A target's pixels written into memory: each word of the pages, each unit its pixel's (the target's sample at the GS's
// sample point); a unit of no pixel of the target keeps memory's
kernel void MemoryFromTarget(uint index [[thread_position_in_grid]], device uint* mem [[buffer(0)]],
                             constant Layout& l [[buffer(1)]], device const uint* inv [[buffer(2)]],
                             constant Conversion& c [[buffer(3)]], texture2d<float> colour [[texture(0)]],
                             texture2d<uint> depth [[texture(1)]])
{
    if (index >= c.words)
    {
        return;
    }
    uint w = c.firstWord + index;
    uint units = 1u << uint(l.unitShift);
    uint unitAll = l.unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(l.unitBits)) - 1u);
    uint value = mem[w];
    for (uint k = 0u; k < units; k++)
    {
        int2 p;
        if (!UnitPixel(l, inv, (w << uint(l.unitShift)) | k, p))
        {
            continue;
        }
        uint2 at = uint2(p) * c.targetScale;
        uint unit;
        if (c.storage == 2)
        {
            uint v = depth.read(at).r;
            unit = l.unitBits == 32 ? v : v & 0xFFFFu;
        }
        else
        {
            uint4 b = uint4(colour.read(at) * 255.0 + 0.5);
            unit = c.storage == 0 ? (b.r | b.g << 8 | b.b << 16 | b.a << 24)
                                  : ((b.r >> 3) | (b.g >> 3) << 5 | (b.b >> 3) << 10 | (b.a >= 128u ? 0x8000u : 0u));
        }
        uint shift = k * uint(l.unitBits);
        value = (value & ~(unitAll << shift)) | ((unit & unitAll) << shift);
    }
    mem[w] = value;
}

// Memory's pixels written into a target (the pages asked for only: a page mask), every sample of the rectangle
kernel void TargetFromMemory(uint2 at [[thread_position_in_grid]], device const uint* mem [[buffer(0)]],
                             constant Layout& l [[buffer(1)]], device const uint* fwd [[buffer(2)]],
                             constant Conversion& c [[buffer(3)]], texture2d<float, access::write> colour [[texture(0)]],
                             texture2d<uint, access::write> depth [[texture(1)]])
{
    uint2 sample = uint2(c.rect.xy) + at;
    if (sample.x >= uint(c.rect.z) || sample.y >= uint(c.rect.w))
    {
        return;
    }
    int2 p = int2(sample / c.targetScale);
    uint address = UnitAddress(l, fwd, p);
    uint page = (address >> uint(l.unitShift)) >> 11;
    if ((c.pageMask[page >> 5] & (1u << (page & 31u))) == 0u)
    {
        return;
    }
    uint v = MemUnit(mem, l, address);
    if (c.storage == 2)
    {
        depth.write(uint4(v, 0, 0, 0), sample);
    }
    else if (c.storage == 0)
    {
        colour.write(float4(uint4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24)) / 255.0, sample);
    }
    else
    {
        colour.write(float4(float((v & 31u) << 3), float(((v >> 5) & 31u) << 3), float(((v >> 10) & 31u) << 3),
                            (v & 0x8000u) != 0u ? 128.0 : 0.0) / 255.0, sample);
    }
}

struct Merge
{
    uint firstWord;
    uint words;
    uint rectCount;
    uint unitMask;
    uint pageMask[16];
    int4 rects[8];
};

// The CPU's writes of rectangles of a buffer (one layout) put into memory's copy: each unit (pixel) of the pages' words in a
// rectangle the CPU's (fresh: local memory's pages, at the same words), every other memory's copy's as it was. The bits of a
// unit a write sets: unitMask (24 bit and the 8 and 4 bit formats in 32 bit words write part of each)
kernel void MergeWrites(uint index [[thread_position_in_grid]], device uint* mem [[buffer(0)]], constant Layout& l [[buffer(1)]],
                        device const uint* inv [[buffer(2)]], constant Merge& m [[buffer(3)]], device const uint* fresh [[buffer(4)]])
{
    if (index >= m.words)
    {
        return;
    }
    uint w = m.firstWord + index;
    uint page = w >> 11;
    if ((m.pageMask[page >> 5] & (1u << (page & 31u))) == 0u)
    {
        return;
    }
    uint units = 1u << uint(l.unitShift);
    uint unitAll = l.unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(l.unitBits)) - 1u);
    uint take = 0u;
    for (uint k = 0u; k < units; k++)
    {
        int2 p;
        if (!UnitPixel(l, inv, (w << uint(l.unitShift)) | k, p))
        {
            continue;
        }
        for (uint r = 0; r < m.rectCount; r++)
        {
            if (p.x >= m.rects[r].x && p.y >= m.rects[r].y && p.x <= m.rects[r].z && p.y <= m.rects[r].w)
            {
                take |= unitAll << (k * uint(l.unitBits));
                break;
            }
        }
    }
    take &= m.unitMask;
    mem[w] = (mem[w] & ~take) | (fresh[index] & take);
}

struct Decode
{
    int psm;
    int cpsm;
    int csa;
    uint ta0;
    uint ta1;
    uint aem;
    int4 rect;
};

uint Expand16(constant Decode& d, uint c)
{
    uint alpha = (c & 0x8000u) != 0u ? d.ta1 : ((d.aem != 0 && (c & 0x7FFFu) == 0u) ? 0u : d.ta0);
    return (c & 31u) << 3 | ((c >> 5) & 31u) << 11 | ((c >> 10) & 31u) << 19 | alpha << 24;
}

uint ClutColour(constant Decode& d, device const ushort* clut, uint index)
{
    if (d.cpsm == 0 || d.cpsm == 1)
    {
        int slot = int((index + uint(d.csa & 15) * 16u) & 255u);
        uint c = uint(clut[slot]) | uint(clut[slot + 256]) << 16;
        if (d.cpsm == 1)
        {
            uint alpha = (d.aem != 0 && (c & 0xFFFFFFu) == 0u) ? 0u : d.ta0;
            c = (c & 0xFFFFFFu) | alpha << 24;
        }
        return c;
    }
    return Expand16(d, uint(clut[int((index + uint(d.csa) * 16u) & 511u)]));
}

// A texture level decoded from memory's copy as the software GS decodes it (DecodeTexel: TEXA, the CLUT): RGBA, the PS2's
// alpha (0x80 = 1.0) in the bytes
kernel void DecodeLevel(uint2 at [[thread_position_in_grid]], device const uint* mem [[buffer(0)]], constant Layout& l [[buffer(1)]],
                        device const uint* fwd [[buffer(2)]], constant Decode& d [[buffer(3)]],
                        device const ushort* clut [[buffer(4)]], texture2d<float, access::write> level [[texture(0)]])
{
    int2 p = d.rect.xy + int2(at);
    if (p.x > d.rect.z || p.y > d.rect.w)
    {
        return;
    }
    uint v = MemUnit(mem, l, UnitAddress(l, fwd, p));
    uint c;
    // PSMCT32 0x00, CT24 0x01, CT16 0x02, CT16S 0x0A, T8 0x13, T4 0x14, T8H 0x1B, T4HL 0x24, T4HH 0x2C, Z32 0x30, Z24 0x31,
    // Z16 0x32, Z16S 0x3A
    if (d.psm == 0x00 || d.psm == 0x30)
    {
        c = v;
    }
    else if (d.psm == 0x01 || d.psm == 0x31)
    {
        c = v & 0xFFFFFFu;
        uint alpha = (d.aem != 0 && c == 0u) ? 0u : d.ta0;
        c = c | alpha << 24;
    }
    else if (d.psm == 0x02 || d.psm == 0x0A || d.psm == 0x32 || d.psm == 0x3A)
    {
        c = Expand16(d, v);
    }
    else if (d.psm == 0x1B)
    {
        c = ClutColour(d, clut, v >> 24);
    }
    else if (d.psm == 0x24)
    {
        c = ClutColour(d, clut, (v >> 24) & 15u);
    }
    else if (d.psm == 0x2C)
    {
        c = ClutColour(d, clut, v >> 28);
    }
    else
    {
        c = ClutColour(d, clut, v);
    }
    level.write(float4(uint4(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u, c >> 24)) / 255.0, uint2(p));
}

// The texture upscale: a texture's texels read at S times the size with Catmull-Rom's cubic, the edges' texels repeated
float4 Fetch(texture2d<float> source, int2 p)
{
    return source.read(uint2(clamp(p, int2(0), int2(source.get_width(), source.get_height()) - 1)));
}

float4 Weights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return float4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}

kernel void Upscale(uint2 at [[thread_position_in_grid]], texture2d<float> source [[texture(0)]],
                    texture2d<float, access::write> out [[texture(1)]], constant float& scale [[buffer(0)]])
{
    if (at.x >= out.get_width() || at.y >= out.get_height())
    {
        return;
    }
    float2 position = (float2(at) + 0.5) / scale - 0.5;
    float2 floored = floor(position);
    int2 base = int2(floored);
    float4 wx = Weights(position.x - floored.x);
    float4 wy = Weights(position.y - floored.y);
    float4 sum = float4(0.0);
    for (int y = 0; y < 4; y++)
    {
        float4 row = Fetch(source, base + int2(-1, y - 1)) * wx.x + Fetch(source, base + int2(0, y - 1)) * wx.y +
                     Fetch(source, base + int2(1, y - 1)) * wx.z + Fetch(source, base + int2(2, y - 1)) * wx.w;
        sum += row * wy[y];
    }
    out.write(clamp(sum, 0.0, 1.0), at);
}

// The PCRTC's two circuits merged (circuit 1 over circuit 2 by ALP or its own alpha, over the background colour), at the scale
struct Circuit
{
    uint enabled;
    float4 place;
    float2 corner;
};

struct Circuits
{
    Circuit circuit[2];
    float alp;
    uint ownAlpha;
    float3 background;
};

bool ReadCircuit(texture2d<float> buffer, Circuit c, float2 at, thread float4& colour)
{
    float2 local = at - c.place.xy;
    if (c.enabled == 0 || local.x < 0.0 || local.y < 0.0 || local.x >= c.place.z || local.y >= c.place.w)
    {
        return false;
    }
    colour = buffer.read(uint2(c.corner + local));
    return true;
}

kernel void MergeCircuits(uint2 at [[thread_position_in_grid]], texture2d<float> circuit1 [[texture(0)]],
                          texture2d<float> circuit2 [[texture(1)]], texture2d<float, access::write> out [[texture(2)]],
                          constant Circuits& m [[buffer(0)]])
{
    if (at.x >= out.get_width() || at.y >= out.get_height())
    {
        return;
    }
    float4 c1;
    float4 c2;
    bool has1 = ReadCircuit(circuit1, m.circuit[0], float2(at), c1);
    bool has2 = ReadCircuit(circuit2, m.circuit[1], float2(at), c2);
    float3 result = has2 ? c2.rgb : m.background;
    if (has1)
    {
        float a = m.ownAlpha != 0 ? min(c1.a * 2.0, 1.0) : m.alp;
        result = c1.rgb * a + result * (1.0 - a);
    }
    out.write(float4(result, 1.0), at);
}
)";
}
