#pragma once

// The GPU's side of the hardware renderer (hwgs.cpp): HwGs decides what is drawn where (targets by GS page, which copy of a
// page is newest, the texture caches, batching), and a GpuDevice does it on one GPU interface: OpenGL (gldevice.cpp) or Metal
// (metaldevice.mm). docs/metal.md has why the line is here.
//
// What both sides share: the targets' records, the vertexes a batch draws, the batch's key (everything a draw depends on),
// GS memory's layouts, and what a texture decode or a merge of the CPU's writes needs.

#include "common.h"

#include "../gs/backend.h"
#include "hwgs.h"

#include <memory>
#include <vector>

namespace NativeGraphics
{
// A texture of the device's (0: none)
using TextureId = u32;

// The layouts targets keep: 32 bit colour (and 24), 16 bit colour, 16 bit colour S, Z32 (and Z24), Z16, Z16S
enum TargetClass : u32
{
    ClassColour32,
    ClassColour16,
    ClassColour16S,
    ClassDepth32,
    ClassDepth16,
    ClassDepth16S,
    // A frame in a Z format: colour kept, laid out as the Z format
    ClassColourZ32,
    ClassColourZ16,
    ClassColourZ16S,
    ClassNone,
};

inline u32 ClassOf(u32 psm)
{
    switch (psm)
    {
    case Gs::PSMCT32:
    case Gs::PSMCT24:
        return ClassColour32;
    case Gs::PSMCT16:
        return ClassColour16;
    case Gs::PSMCT16S:
        return ClassColour16S;
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        return ClassDepth32;
    case Gs::PSMZ16:
        return ClassDepth16;
    case Gs::PSMZ16S:
        return ClassDepth16S;
    default:
        return ClassNone;
    }
}

// A frame buffer's class: a Z format's frame is colour laid out as Z
inline u32 FrameClassOf(u32 psm)
{
    switch (psm)
    {
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        return ClassColourZ32;
    case Gs::PSMZ16:
        return ClassColourZ16;
    case Gs::PSMZ16S:
        return ClassColourZ16S;
    default:
        return ClassOf(psm);
    }
}

// Colour classes stored as 32 bits (the others as 16)
inline bool Is32Class(u32 cls)
{
    return cls == ClassColour32 || cls == ClassColourZ32;
}

inline u32 StoredPsm(u32 cls)
{
    static constexpr u32 Formats[] = {Gs::PSMCT32, Gs::PSMCT16, Gs::PSMCT16S, Gs::PSMZ32, Gs::PSMZ16, Gs::PSMZ16S,
                                      Gs::PSMZ32,  Gs::PSMZ16,  Gs::PSMZ16S, 0};
    return Formats[cls];
}

inline bool IsDepthClass(u32 cls)
{
    return cls == ClassDepth32 || cls == ClassDepth16 || cls == ClassDepth16S;
}

struct Rect
{
    s32 left;
    s32 top;
    s32 right;
    s32 bottom;
};

// A primitive of a batch the shader blends: the pixels it can draw in and, for a triangle, its corners (the GS's 12.4 fixed
// point, relative to the drawing offset)
struct BatchShape
{
    Rect rect;
    bool triangle;
    s32 x[3];
    s32 y[3];
};

// A frame or depth buffer drawn into, kept by the device at the internal scale
struct Target
{
    u32 bp = 0;
    u32 bw = 0;
    u32 cls = 0;
    u32 psm = 0;
    bool depth = false;
    // A 32 bit depth buffer whose values are near the top (an effect's, made from colours): a device that keeps depth as a
    // float keeps it as (2^32 - 1 - z) / 2^32, exact there (a float's steps near 1.0 are 256 of the GS's units)
    bool reversed = false;
    // Its depth is all zero: cleared whole since it last held anything else. Which way it's kept is open again, and the next
    // draw of other values decides it, as the draw making a buffer does
    bool zeroOnly = false;
    // Its size in the PS2's pixels, and its pixels a PS2 pixel each way (the internal scale; 1 for the effects' small buffers
    // under native scaling)
    u32 width = 0;
    u32 height = 0;
    u32 scale = 1;
    // Pages laid out as rectangles (its base at a page's start)
    bool aligned = false;
    // The device's texture of it, and whether it can be drawn into: not when its size at the scale is 0 or past what a
    // texture can be (such a target is the software GS's)
    TextureId texture = 0;
    bool complete = false;
    Gs::PageSet pages;
    Gs::PageSet valid;
    // The pages it holds newest with every other target's copy out of date (MarkDrawn has nothing to do there)
    Gs::PageSet owned;
    // Each page's pixels (y << 11 | x) and their bounding rectangle
    std::vector<std::vector<u32>> pixels;
    std::vector<Rect> rects;
};

struct HwVertex
{
    f32 x;
    f32 y;
    u32 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    f32 s;
    f32 t;
    f32 q;
    f32 fog;
};

// The texture filters (the GS's MMIN codes' order)
enum class Filter : u32
{
    Nearest,
    Linear,
    NearestMipNearest,
    NearestMipLinear,
    LinearMipNearest,
    LinearMipLinear,
};

// A fixed function blend: result = source * sourceFactor op destination * destinationFactor (the source alpha a second
// output's, the constant the FIX / 128)
enum class BlendOp : u32
{
    Add,
    Subtract,
    ReverseSubtract,
};

enum class BlendFactor : u32
{
    Zero,
    One,
    SourceAlpha,
    InverseSourceAlpha,
    ConstantAlpha,
    InverseConstantAlpha,
};

enum class DepthFunc : u32
{
    Always,
    Gequal,
    Greater,
    Lequal,
    Less,
};

enum class Topology : u32
{
    Triangles,
    Lines,
    Points,
};

// What a batch draws with: everything that changes the device's state or the shader's constants (compared as bytes)
struct BatchKey
{
    Target* colour;
    Target* depth;
    TextureId texture;
    Topology primitive;
    u32 program;
    Filter minFilter;
    Filter magFilter;
    // Clamped (else repeated) across and down
    u32 clampS;
    u32 clampT;
    u32 maxLevel;
    f32 texWidth;
    f32 texHeight;
    f32 gsTexWidth;
    f32 gsTexHeight;
    u32 blend;
    BlendOp blendOp;
    BlendFactor blendSource;
    BlendFactor blendDestination;
    u32 blendFix;
    DepthFunc depthFunc;
    u32 depthWrite;
    u32 colourMask;
    s32 scissor[4];
    u32 aref;
    u32 afail;
    u32 alphaTested;
    u32 fogColour;
    u32 ta0;
    u32 ta1;
    u32 aem;
    f32 lodK;
    u32 lodL;
    u32 mxl;
    u64 dimx;
    f32 zMax;
    // The shader's blend (the GS's A, B, C, D, FIX, PABE, COLCLAMP), the frame mask as each channel's stored bits, DATM, and
    // the destination alpha test by a stencil
    u32 gsBlend[4];
    u32 gsFix;
    u32 pabe;
    u32 colclamp;
    u32 frameMask[4];
    u32 datm;
    u32 stencilDate;
    u32 ditherScale;
    u32 anisotropy;
    u32 wrapAdd;
    // A target sampled without a filter at a higher scale than the frame's: its scale (each texel read at its GS sample point)
    u32 texSnap;
    u32 depthReverse;
    u32 zMaxU;
    // The depth test at the GS's sample point (the block's first sample) in the shader: its ZTST, 0 for the device's own test
    u32 nativeDepthTest;
    // A texture's region clamp or repeat: WMS, WMT, the GS's bilinear filter (1) or none, and the texture's samples a texel;
    // MINU, MAXU, MINV, MAXV
    u32 regionMode[4];
    u32 regionBounds[4];
    // The GS's own pixel rules, for a device that makes them all in its shader (ReadsDestination; zero for one that doesn't):
    // the frame's kind (0 32 bit, 1 24 bit, 2 16 bit), its mask and whether it's written; the depth buffer's kind (1 Z32, 2 Z24,
    // 3 Z16), its test, whether it's written and its largest value; the alpha test, the destination alpha test and the blend
    struct Exact
    {
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
        // The texture sampled as the GS samples it (no filtering, upscale or replacement option on): its levels, the
        // magnification filter, MMIN and MXL (the wrap modes, the region and a target's samples a texel in regionMode and
        // regionBounds)
        u32 gsSample;
        u32 levelCount;
        u32 magLinear;
        u32 mmin;
        u32 mxl;
    } exact;
};

// The shader's features (its variant's bits)
enum ProgramBits : u32
{
    BitTextured = 1u << 0,
    BitTfxShift = 1,
    BitTcc = 1u << 3,
    BitFog = 1u << 4,
    BitAtstShift = 5,
    BitAlphaTest = 1u << 8,
    BitUv = 1u << 9,
    BitTexAlphaShift = 10,
    BitFrame16 = 1u << 12,
    BitDither = 1u << 13,
    BitFba = 1u << 14,
    BitMipmap = 1u << 15,
    BitLodConstant = 1u << 16,
    BitFailPass = 1u << 17,
    BitDepth = 1u << 18,
    BitShaderBlend = 1u << 19,
    BitDate = 1u << 20,
    BitFrame24 = 1u << 21,
    BitWrapAdd = 1u << 22,
    BitNativeDepth = 1u << 23,
    BitNativeTexel = 1u << 24,
    BitRegion = 1u << 25,
};

// GS memory's layout families (0 32 bit, 1 16, 2 16S, 3 8, 4 4): a page's units (pixels) by block, and whether a format is a
// Z format's (its blocks XOR 0x18)
struct FamilyInfo
{
    u32 unitsPerBlock;
    u32 unitShift;
    u32 bits;
    u32 pageWidth;
    u32 pageHeight;
    bool halfWidth;
};

constexpr FamilyInfo Families[5] = {
    {64, 0, 32, 64, 32, false},
    {128, 1, 16, 64, 64, false},
    {128, 1, 16, 64, 64, false},
    {256, 2, 8, 128, 64, true},
    {512, 3, 4, 128, 128, true},
};

void LayoutOf(u32 psm, u32* family, bool* xorZ);

// Each family's tables, from the software GS's own layouts (a page at block 0): forward, a page's pixels' block << 16 | unit
// (pageWidth x pageHeight); inverse, a page's units' pixel x | y << 16 (unitsPerBlock x 32)
struct LayoutTables
{
    std::vector<u32> forward[5];
    std::vector<u32> inverse[5];
};

// The CPU's rectangles of one buffer (a layout) put into memory's copy in one pass: the bits of each unit they set (24 bit
// and the 8 and 4 bit formats in 32 bit words set part of each), the pages the pass touches, the buffer's pages down
struct MergeGroup
{
    u32 psm;
    u32 bp;
    u32 bw;
    u32 pagesDown;
    std::vector<Rect> rects;
    u32 unitMask;
    Gs::PageSet pages;
};

// A texture's levels decoded from memory's copy (as the software GS's DecodeTexel does: TEXA, the CLUT), each level's
// rectangle (an empty one: none of it)
struct DecodeJob
{
    u32 levelCount;
    Gs::TextureLevel levels[7];
    Rect rects[7];
    bool indexed;
    u32 cpsm;
    u32 csa;
    u32 ta0;
    u32 ta1;
    u32 aem;
    // The CLUT (512 16 bit entries) and its version (changed whenever it is)
    const u16* clut;
    u32 clutVersion;
};

// One of the PCRTC's circuits as the picture shows it: its target, its place in the picture (pixels at the scale), its corner
// in the target and the target's size (at the scale)
struct CircuitPicture
{
    bool enabled;
    const Target* target;
    f32 place[4];
    f32 corner[2];
    f32 size[2];
};

// The picture: a rectangle of a target, or the circuits merged (target null), at the scale
struct Picture
{
    const Target* target;
    s32 left;
    s32 top;
    u32 width;
    u32 height;
};

class GpuDevice
{
public:
    virtual ~GpuDevice() = default;

    // Ready on the calling thread (the render thread): false when this GPU interface isn't there
    virtual bool Init() = 0;
    // The device's state current on the calling thread before HwGs uses it
    virtual void MakeCurrent() = 0;
    // Whether the fragment shader reads the pixel it's drawing (programmable blending): HwGs then plans each draw for the
    // shader to make every GS rule itself (docs/metal.md)
    virtual bool ReadsDestination() const = 0;
    // The biggest side (pixels) a target can be at the scale
    virtual u32 MaxTargetSize() const = 0;

    // A target's texture at the scale (target.texture; true when it can be drawn into), and let go of
    virtual bool MakeTarget(Target& target) = 0;
    virtual void FreeTarget(Target& target) = 0;
    // A 32 bit depth target that holds only zeros set to the zero of the way it's now kept (target.reversed)
    virtual void ClearDepth(Target& target) = 0;
    // Work the device keeps aside for a target (sums not yet added to it): whether there is any, and put into the target
    // before anything reads it
    virtual bool Pending(const Target& target) = 0;
    virtual void Resolve(Target& target) = 0;
    // A copy of a colour target as it is now (a texture of the device's, good until the next copy of a target that size)
    virtual TextureId CopyTarget(Target& target) = 0;
    // A pixel at the scale (debugging): colour as RGBA bytes (16 bit classes as their 5 bits shifted up and the alpha bit
    // 0x80), depth as the GS's value
    virtual bool ReadTargetPixel(Target& target, u32 x, u32 y, u32* value) = 0;
    // The whole target at the scale as RGBA (debugging: TWIN_HW_DUMP_TARGET)
    virtual void ReadTargetScaled(Target& target, std::vector<u32>& pixels) = 0;
    // Everything kept for targets of the old scale let go of
    virtual void DropCopies() = 0;

    // GS memory's copy on the GPU: made, pages of local memory written into it, read back from it (the GPU's work waited for),
    // a target's pixels written into its pages, its pages' pixels written into a target (within a rectangle of the PS2's
    // pixels), and the CPU's rectangles merged into it (the rest of their pages' words as they are)
    virtual void InitMemory(const LayoutTables& tables) = 0;
    virtual void UploadMemory(const Gs::PageSet& pages, const u8* memory) = 0;
    virtual void ReadMemory(const Gs::PageSet& pages, u8* out) = 0;
    virtual void MemoryFromTarget(Target& target, const Gs::PageSet& pages) = 0;
    virtual void TargetFromMemory(Target& target, const Gs::PageSet& pages, const Rect& rect) = 0;
    virtual void MergeWrites(const Gs::PageSet& pages, const u8* memory, const std::vector<MergeGroup>& groups) = 0;

    // Textures (RGBA bytes, the PS2's alpha, 0x80 = 1.0): levels made empty, a rectangle of a level written, an image with its
    // mipmaps made (a replacement), let go of, levels decoded from memory's copy, and level 0 upscaled into a new mipmapped
    // texture (the texture upscale option)
    virtual TextureId MakeTexture(const u32* widths, const u32* heights, u32 levels) = 0;
    virtual void UploadTexture(TextureId texture, u32 level, const Rect& rect, const u32* texels, u32 stride) = 0;
    virtual TextureId MakeImage(u32 width, u32 height, const u32* pixels) = 0;
    virtual void FreeTexture(TextureId texture) = 0;
    virtual void DecodeLevels(TextureId texture, const DecodeJob& job) = 0;
    virtual TextureId MakeUpscaled(TextureId source, u32 width, u32 height, u32 scale) = 0;

    // A batch drawn (its primitives' shapes: the pixels they can draw in)
    virtual void DrawBatch(const BatchKey& key, const std::vector<HwVertex>& vertexes, const std::vector<BatchShape>& shapes) = 0;

    // The PCRTC's two circuits merged over the background colour (circuit 1 over 2 by ALP or its own alpha) into a texture of
    // width x height; the picture read back (RGBA, top row first); the picture copied into the hand-over's slot for the
    // window (the window's fence from its last drawing of it waited for first)
    virtual TextureId MergeCircuits(const CircuitPicture* circuits, u32 width, u32 height, f32 alp, bool ownAlpha, u32 background) = 0;
    virtual void ReadPicture(const Picture& picture, std::vector<u32>& pixels) = 0;
    virtual bool CopyPicture(const Picture& picture, u32 slot, void* drawnFence, ShownTexture* shown) = 0;
    // A picture's end (statistics)
    virtual void PictureDone() = 0;
    // Everything given so far done by the GPU (waited for: timing)
    virtual void Finish() = 0;
    // The GPU's busy time on this interface's work since the start (seconds; any thread), negative where it isn't measured
    virtual double GpuSeconds() const = 0;
};

// Where a vertex goes past x / 16 * scale + 0.5, in pixels (TWIN_HW_VERTEX_OFFSET, 256ths)
f32 VertexNudge();

// The devices: OpenGL (with the render thread's context current, or one of its own), Metal (null where there's none)
std::unique_ptr<GpuDevice> MakeGlDevice(HardwareStats& stats, const Gs::Gs& gs);
std::unique_ptr<GpuDevice> MakeMetalDevice(HardwareStats& stats, const Gs::Gs& gs);
}
