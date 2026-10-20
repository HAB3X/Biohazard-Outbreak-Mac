// The hardware renderer (hwgs.h). The GS's semantics carried over to the GPU:
//
// - Coordinates: the GS draws a pixel when the point at its top-left corner is inside a primitive (left and top edges inside);
//   the GPU samples pixel centres. A vertex at x (12.4, less XYOFFSET) goes to x / 16 * scale + 0.5: a PS2 pixel's block of
//   scale x scale pixels has the GS's sample point as its first one's centre, the others at quarter (or half...) steps after
//   it. The positions are on the rasteriser's sixteenth of a pixel grid, so it snaps none of them: a nudge off the grid (as
//   this once had, 1/256 of a pixel) gets snapped a sixteenth away, losing thin primitives' pixels (shadow volumes' edges).
//   The draws' viewport is far bigger than the target (the scissor keeps them in it), so nothing reaching past the target is
//   clipped either (clipping makes new vertexes off the grid). At the PS2's resolution it's the GS's own sampling
// - Colours, texture coordinates, fog and depth are interpolated linearly in the screen (noperspective) as the GS's DDA does;
//   STQ is divided at each pixel. Texel coordinates are nudged a 256th of a texel up, off the edges the GPU's interpolation
//   falls a hair short of where the GS's exact coordinates are on them. Colours are the GS's bytes (0-255, alpha 0x80 = 1.0)
//   throughout the shader, truncated where the GS truncates
// - Depth is the GS's value over 2^32 (exact below 2^24) in a 32 bit float buffer, written from the shader (truncated as the
//   GS's is); the tests are GL's. A 32 bit depth buffer first drawn with values in the top half (an effect's, made from
//   colours) is kept reversed, (2^32 - 1 - z) / 2^32, exact up there, its tests reversed
// - A target drawn into by a filterless sampling of a target at a higher scale reads each texel's first sample (the GS's
//   sample point), not the one nearest the coordinate
// - The blend ((A - B) * C >> 7) + D is GL's blending: dual source, the second output's alpha C (the source alpha / 128), or
//   the constant alpha (FIX / 128); the alpha written is the source's (FBA applied), as the GS doesn't blend alpha
// - A failed alpha test's AFAIL is a second pass over the batch's failing pixels with the writes AFAIL keeps (where primitives
//   of a batch overlap, an earlier one's failing pixels are drawn after a later one's passing ones, as in PCSX2: breaking the
//   batch wherever they might, each triangle of a mesh is a batch of its own); none when AFAIL writes all a pass writes
// - 16 bit frames: the colour dithered (DIMX, at the PS2's pixel) and truncated to 5 bits a channel before it's written, the
//   alpha its top bit (no blending into them: the blend would have to happen before the truncation)
//
// Render targets: a frame or depth buffer drawn into gets a GPU texture (at the scale) for its base, width and layout (32 bit
// colour and 24 bit share one, as do Z32 and Z24). Local memory stays the PS2's; each 8 KB page records where its newest content
// is (local memory, or one target), each target which of its pages it holds current. A target is brought up to date from local
// memory before the GPU uses it (uploads), and local memory from a target before the CPU side reads or writes the page
// (downloads: the target read back at the PS2's resolution). A texture that is a target's buffer in its own layout is sampled
// from the target; any other is decoded from local memory by the software GS's decoder (CLUT, TEXA, every format) and cached.
//
// The GPU's own work (textures, passes, draws, read backs) is a GpuDevice's (gpudevice.h): OpenGL's is gldevice.cpp, where the
// notes above on depth, blending and AFAIL are carried out.
#include "hwgs.h"

#include "gpudevice.h"
#include "replace.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "../png.h"

#include "../hardware.h"
#include "../gs/backend.h"
#include "../gs/drawstate.h"
#include "../gs/gs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NativeGraphics
{
// A layout's family (0 32 bit, 1 16, 2 16S, 3 8, 4 4) and whether it's a Z format's (its blocks XOR 0x18)
void LayoutOf(u32 psm, u32* family, bool* xorZ)
{
    *xorZ = false;
    switch (psm)
    {
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        *xorZ = true;
        *family = 0;
        break;
    case Gs::PSMCT16:
        *family = 1;
        break;
    case Gs::PSMZ16:
        *xorZ = true;
        *family = 1;
        break;
    case Gs::PSMCT16S:
        *family = 2;
        break;
    case Gs::PSMZ16S:
        *xorZ = true;
        *family = 2;
        break;
    case Gs::PSMT8:
        *family = 3;
        break;
    case Gs::PSMT4:
        *family = 4;
        break;
    default:
        *family = 0;
        break;
    }
}

// Where a vertex goes past x / 16 * scale + 0.5, in pixels (TWIN_HW_VERTEX_OFFSET, 256ths)
f32 VertexNudge()
{
    static const f32 nudge = std::getenv("TWIN_HW_VERTEX_OFFSET") != nullptr ? static_cast<f32>(std::atof(std::getenv("TWIN_HW_VERTEX_OFFSET"))) / 256.0f
                                                                          : 0.0f;
    return nudge;
}

namespace
{
// A page's size in pixels of a layout
void PageSize(u32 psm, s32* width, s32* height)
{
    switch (Gs::BitsPerPixel(psm))
    {
    case 16:
        *width = 64;
        *height = 64;
        break;
    case 8:
        *width = 128;
        *height = 64;
        break;
    case 4:
        *width = 128;
        *height = 128;
        break;
    default:
        *width = 64;
        *height = 32;
        break;
    }
}

// Whether a triangle's edge keeps the other triangle's pixels out: every corner of the other one is on its outer side, or on the
// edge's line only where it is one of the edge's own ends (a mesh's triangles share edges, and rasterise those without a pixel
// in both), else a quarter pixel (4 units) clear of it, far past the GPU's snapping of corners
bool EdgeSeparates(const BatchShape& a, u32 from, u32 to, const BatchShape& b, s64 orientation)
{
    const s64 ex = a.x[to] - a.x[from];
    const s64 ey = a.y[to] - a.y[from];
    const s64 length2 = ex * ex + ey * ey;
    for (u32 i = 0; i < 3; i++)
    {
        const s64 side = (ex * (b.y[i] - a.y[from]) - ey * (b.x[i] - a.x[from])) * orientation;
        const bool onEnd = (b.x[i] == a.x[from] && b.y[i] == a.y[from]) || (b.x[i] == a.x[to] && b.y[i] == a.y[to]);
        if (!(side < 0 && side * side >= length2 * 16) && !onEnd)
        {
            return false;
        }
    }

    return true;
}

bool ShapesOverlap(const BatchShape& a, const BatchShape& b)
{
    if (a.rect.left > b.rect.right || b.rect.left > a.rect.right || a.rect.top > b.rect.bottom || b.rect.top > a.rect.bottom)
    {
        return false;
    }

    if (!a.triangle || !b.triangle)
    {
        return true;
    }

    // Two triangles (convex) share no pixel when an edge of either has the other wholly outside it
    for (u32 side = 0; side < 2; side++)
    {
        const BatchShape& t = side == 0 ? a : b;
        const BatchShape& o = side == 0 ? b : a;
        const s64 area = static_cast<s64>(t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - static_cast<s64>(t.y[1] - t.y[0]) * (t.x[2] - t.x[0]);
        if (area == 0)
        {
            return true;
        }

        for (u32 e = 0; e < 3; e++)
        {
            if (EdgeSeparates(t, e, (e + 1) % 3, o, area > 0 ? 1 : -1))
            {
                return false;
            }
        }
    }

    return true;
}

struct CachedTexture
{
    TextureId id = 0;
    u32 width = 0;
    u32 height = 0;
    u32 levels = 0;
    u32 versions[7] = {};
    u32 tilesVersions[7] = {};
    // Level 0's 8x8 tiles uploaded at its version (the decoder decodes what draws reach; the rest isn't read)
    std::vector<u8> tiles;
    // The replacement (a pack's texture) looked for at level 0's version: its texture (0: none), whether it has mipmaps
    u32 replacementVersion = ~0u;
    TextureId replacement = 0;
    bool replacementMipmaps = false;
    bool dumped = false;
};

// A texture decoded on the GPU: its levels, each level's 8x8 tiles' decode epochs (0: not decoded; good while every page under
// the tile is older and no target holds a newer copy) and pages
struct GpuTextureKey
{
    u32 psm;
    u32 cpsm;
    u32 csa;
    u32 clutVersion;
    u64 texa;
    u32 levels;
    u32 bp[7];
    u32 bw[7];
    u32 width;
    u32 height;
    bool operator==(const GpuTextureKey&) const = default;
};

struct GpuTextureKeyHash
{
    size_t operator()(const GpuTextureKey& k) const
    {
        u64 h = 1469598103934665603ull;
        auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };
        mix(k.psm);
        mix(k.cpsm | k.csa << 8);
        mix(k.clutVersion);
        mix(k.texa);
        mix(k.levels);
        for (u32 i = 0; i < k.levels; i++)
        {
            mix(k.bp[i] | static_cast<u64>(k.bw[i]) << 32);
        }

        mix(k.width | static_cast<u64>(k.height) << 32);
        return static_cast<size_t>(h);
    }
};

struct GpuTexture
{
    TextureId id = 0;
    struct Level
    {
        u32 width;
        u32 height;
        u32 tilesWide;
        u32 tilesHigh;
        std::vector<u32> tileEpoch;
        std::vector<u16> tilePage;
    } levels[7];
    u32 levelCount = 0;
    Gs::PageSet pages;
    // Every tile decoded, checked good at this epoch
    bool complete = false;
    u32 checkedEpoch = 0;
    // A replacement (a pack's texture) looked for when the texture was last decoded whole from the CPU's data; 0: none
    TextureId replacement = 0;
    // The texture upscaled (the texture upscale option, native/GRAPHICS.md), from its own tiles: 0 none, and the scale it was made at
    TextureId upscaled = 0;
    u32 upscaledScale = 0;
    // The texture drawn with (its own, its replacement, or a decode of the same content made for another place)
    TextureId shown = 0;
    // Shown from the content cache: good while every page is older than this and no target holds a newer copy (0: not)
    u32 contentEpoch = 0;
    std::vector<u16> pageList;
    // The batch its tiles were last decoded for (HwGs::batchSerial_)
    u64 decodedBatch = 0;
};

// A texture's content decoded once (the game streams its textures through the same places of GS memory every frame): by the
// hash of its GS blocks, format, size, CLUT and TEXA
struct ContentTexture
{
    TextureId id = 0;
    TextureId replacement = 0;
    // Upscaled (see GpuTexture::upscaled)
    TextureId upscaled = 0;
    u32 upscaledScale = 0;
};

class HwGs : public Gs::Backend
{
public:
    // The upscaled textures let go of (the texture upscale option changed)
    void DropUpscaled();
    using DeviceMaker = std::unique_ptr<GpuDevice> (*)(HardwareStats& stats, const Gs::Gs& gs);
    HwGs(Gs::Gs& gs, u32 scale, DeviceMaker makeDevice) : gs_(gs), scale_(scale), device_(makeDevice(stats, gs)) {}
    ~HwGs() override;

    bool Init();
    bool Draw(Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count) override;
    void BeforeCpuRead(const Gs::PageSet& pages) override;
    void BeforeCpuWrite(const Gs::PageSet& pages) override { BeforeCpuRead(pages); }
    void BeforeCpuWriteRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const Gs::PageSet& pages,
                            const Gs::PageSet& full) override;
    void BeforeCpuReadRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const Gs::PageSet& pages) override;
    void Flush() override;
    bool PagesGpuNewer(const Gs::PageSet& pages) override
    {
        ProcessWrites();
        return (pages & (cpuStale_ | newestMask_)).any();
    }
    bool DebugPixel(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32* value) override;

    void SetScale(u32 scale);
    u32 scale() const { return scale_; }
    // The shown picture (false: the shown buffer isn't a target)
    bool ShownPicture(NativeGraphics::DisplayTexture* display, Picture* picture);
    bool ReadDisplay(Gs::DisplayImage& image);
    bool CopyDisplay(u32 slot, void* drawnFence, NativeGraphics::ShownTexture* shown);
    void MakeCurrent() { device_->MakeCurrent(); }

    HardwareStats stats = {};

private:
    Target* TargetFor(u32 bp, u32 bw, u32 psm, u32 height, bool frame = false);
    void DestroyTarget(Target* target);
    void ProcessWrites();
    void EnsureValid(Target* target, const Gs::PageSet& pages);
    void MarkDrawn(Target* target, const Gs::PageSet& pages);
    void RectPages(const Target* target, const Rect& rect, Gs::PageSet& pages) const;
    bool Fallback(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, const Rect& rect, const char* why);
    // A copy of a target as it is now (its batch drawn, the device's work for it put in first)
    TextureId CopyOf(Target* target);
    // The target the open batch draws into and reads a copy of, made when the batch began (ps2-native-runtime's addition):
    // a primitive that reads the buffer it draws into joins the batch instead of copying the target again. The GS's next
    // primitive reads what the one before drew, so each would need a copy and two render passes of its own: hundreds a frame
    // in WoC's Warp Room, each a copy of the whole target. Like PCSX2's hardware renderers, the primitives of a
    // batch read the target as it was before the batch. TWIN_HW_EXACT_FEEDBACK=1: a copy for each primitive
    Target* selfCopyFor_ = nullptr;
    TextureId selfCopy_ = 0;

    Gs::Gs& gs_;
    u32 scale_;
    std::unique_ptr<GpuDevice> device_;
    // The draw making a depth target has its depth in the top half (the target is made reversed)
    bool nextDepthHigh_ = false;
    bool nativeSmall_ = std::getenv("TWIN_HW_NATIVE_SMALL") == nullptr || std::getenv("TWIN_HW_NATIVE_SMALL")[0] != '0';
    std::vector<std::unique_ptr<Target>> targets_;
    Target* newest_[Gs::PageCount] = {};
    // The pages a target holds newer than local memory (newest_ not null)
    Gs::PageSet newestMask_;
    // Each texture level's whole pages (by base, width, format and size)
    struct LevelKeyHash
    {
        size_t operator()(const std::tuple<u32, u32, u32, u32, u32>& k) const
        {
            u64 h = std::get<0>(k) * 0x9E3779B97F4A7C15ull;
            h ^= (static_cast<u64>(std::get<1>(k)) << 20 | std::get<2>(k) << 8) + (h << 6) + (h >> 2);
            h ^= (static_cast<u64>(std::get<3>(k)) << 16 | std::get<4>(k)) + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };
    std::unordered_map<std::tuple<u32, u32, u32, u32, u32>, Gs::PageSet, LevelKeyHash> levelPages_;
    // A texture level's whole pages (levelPages_)
    const Gs::PageSet& LevelPages(const Gs::TextureLevel& level)
    {
        auto key = std::make_tuple(level.bp, level.bw, level.psm, level.width, level.height);
        auto found = levelPages_.find(key);
        if (found == levelPages_.end())
        {
            Gs::PageSet whole;
            Gs::PagesOfRect(level.psm, level.bp, level.bw, 0, 0, static_cast<s32>(level.width) - 1, static_cast<s32>(level.height) - 1,
                            whole);
            found = levelPages_.emplace(key, whole).first;
        }

        return found->second;
    }
    std::unordered_map<const void*, CachedTexture> textures_;
    BatchKey key_ = {};
    bool batching_ = false;
    std::vector<HwVertex> vertexes_;
    // A batch blended by the shader: its primitives' shapes (one that can draw a pixel an earlier one can starts a new batch, as
    // the destination copy is from before the batch)
    std::vector<BatchShape> batchShapes_;
    // The CPU's writes into pages whose newer copy was the GPU's (a texture uploaded over what a target drew), waiting to be put
    // into memory's copy (SyncMem): local memory has only their rectangles right, so its pages stay out of date (cpuStale_)
    // (and once merged, kept while local memory has the rectangle right in those of its pages: valid; a read of a rectangle
    // inside it, a CLUT uploaded with its texture, needs no read back)
    struct PartialWrite
    {
        u32 psm;
        u32 bp;
        u32 bw;
        Rect rect;
        Gs::PageSet pages;
        Gs::PageSet valid;
        // The blocks it wrote whole (none for a format that leaves some of their bytes: 24 bit, the high 8 and 4 bit ones)
        Gs::BlockSet blocks;
    };
    std::vector<PartialWrite> partialWrites_;
    Gs::PageSet mergePages_;
    void MergeWrites(const Gs::PageSet& pages);
    // Whether local memory has a texture's levels right though their pages are out of date there: each a rectangle the CPU
    // wrote (in its layout) since the GPU last drew over its pages (a texture uploaded where a target was: its decode found
    // by its content without putting the upload into memory's copy first)
    bool CpuWroteLevels(const Gs::Gs::DrawState& state) const
    {
        for (u32 i = 0; i < state.levelCount; i++)
        {
            const Gs::TextureLevel& lv = state.levels[i];
            if (CpuHasRect(lv.psm, lv.bp, lv.bw, Rect{0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1}))
            {
                continue;
            }

            bool found = false;
            for (const PartialWrite& w : partialWrites_)
            {
                if (w.psm == lv.psm && w.bp == lv.bp && w.bw == lv.bw && w.rect.left <= 0 && w.rect.top <= 0 &&
                    w.rect.right >= static_cast<s32>(lv.width) - 1 && w.rect.bottom >= static_cast<s32>(lv.height) - 1)
                {
                    Gs::PageSet pages;
                    Gs::PagesOfRect(lv.psm, lv.bp, lv.bw, 0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1, pages);
                    found = (pages & newestMask_).none() && (pages & cpuStale_ & ~w.valid).none();
                    if (found)
                    {
                        break;
                    }
                }
            }

            if (!found)
            {
                return false;
            }
        }

        return true;
    }
    // Whether local memory has a rectangle right, in any format: each of its blocks on a page out of date there written whole by
    // the CPU since (a write still valid on that page), and none on a page a target holds newer (an 8 bit texture uploaded as
    // 32 bit pixels, a CLUT written as a rectangle of 32 bit ones)
    bool CpuHasRect(u32 psm, u32 bp, u32 bw, const Rect& r) const
    {
        if (r.right < r.left || r.bottom < r.top)
        {
            return true;
        }

        for (s32 y = r.top & ~7; y <= r.bottom; y += 8)
        {
            for (s32 x = r.left & ~7; x <= r.right; x += 8)
            {
                const u32 block = Gs::PixelBlock(psm, static_cast<u32>(x) & 2047, static_cast<u32>(y) & 2047, bp, bw) & (Gs::BlockCount - 1);
                const u32 page = block >> 5;
                if (newestMask_.test(page))
                {
                    return false;
                }

                if (!cpuStale_.test(page))
                {
                    continue;
                }

                bool covered = false;
                for (const PartialWrite& w : partialWrites_)
                {
                    if (w.valid.test(page) && w.blocks.test(block))
                    {
                        covered = true;
                        break;
                    }
                }

                if (!covered)
                {
                    return false;
                }
            }
        }

        return true;
    }
    // The records of no use any more: merged, and local memory's rectangle no longer known right
    void DropPartialWrites()
    {
        partialWrites_.erase(std::remove_if(partialWrites_.begin(), partialWrites_.end(),
                                            [](const PartialWrite& w) { return w.pages.none() && w.valid.none(); }),
                             partialWrites_.end());
    }
    // Each page's state: memory's copy out of date, local memory's (the CPU's) out of date, the shadow of what memory's copy
    // holds good (for re-uploads of the same bytes), and when its content last changed
    Gs::PageSet memStale_;
    // Pages whose content memory's copy has from a target, and from one at a higher scale than 1 (read back at the PS2's
    // resolution)
    Gs::PageSet fromTargetPages_;
    Gs::PageSet scaledPages_;
    Gs::PageSet cpuStale_;
    Gs::PageSet shadowGood_;
    std::vector<u8> shadow_;
    u32 pageEpoch_[Gs::PageCount] = {};
    u32 epoch_ = 1;
    void InitMemory();
    // Textures decoded on the GPU
    std::unordered_map<GpuTextureKey, GpuTexture, GpuTextureKeyHash> gpuTextures_;
    // TWIN_HW_CONVERSION_TRACE=FIRST,LAST: each conversion of the draws numbered FIRST to LAST, with its pages
    void TraceConversion(const char* direction, const Target* target, const Gs::PageSet& pages)
    {
        static const char* range = std::getenv("TWIN_HW_CONVERSION_TRACE");
        if (range == nullptr)
        {
            return;
        }

        unsigned long long first = 0, last = 0;
        std::sscanf(range, "%llu,%llu", &first, &last);
        if (gs_.primitivesDrawn < first || gs_.primitivesDrawn > last)
        {
            return;
        }

        std::fprintf(stderr, "conversion: #%llu %s %x/%u class %u (%s) pages", static_cast<unsigned long long>(gs_.primitivesDrawn), direction,
                     target->bp, target->bw, target->cls, cpuReason);
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (pages.test(page) && target->pages.test(page))
            {
                const Target* held = newest_[page];
                std::fprintf(stderr, " %x(newest %x/%u/%u)", page, held ? held->bp : 0, held ? held->bw : 0, held ? held->cls : 99);
            }
        }

        std::fprintf(stderr, "\n");
    }
    // Replacement images' textures, made once each
    std::map<NativeGraphics::ReplacementKey, TextureId> replacementTextures_;
    TextureId DecodeOnGpu(const Gs::Gs::DrawState& state, s32 left, s32 top, s32 right, s32 bottom, Target* drawing);
    // Counts the batches (each Flush ends one), and whether the draw being set up reads a texture as it was decoded when the
    // batch began (decoded again if the batch ends before the draw joins it)
    u64 batchSerial_ = 1;
    bool readsBatchStart_ = false;
    void DecodePasses(const Gs::Gs::DrawState& state, TextureId texture, const Rect* rects, u32 levels);
    u64 ContentHash(const Gs::Gs::DrawState& state);
    TextureId MakeLevels(const Gs::Gs::DrawState& state);
    // A texture's levels decoded by the software GS from local memory (current for them) into a new texture
    TextureId DecodeOnCpu(const Gs::Gs::DrawState& state);
    // The scale wanted for a texture of a size (0: not upscaled)
    u32 UpscaleFor(u32 width, u32 height) const;
    std::unordered_map<u64, ContentTexture> contents_;
    void SyncMem(const Gs::PageSet& pages);
    void SyncCpu(const Gs::PageSet& pages);
    void MemFromTarget(Target* target, const Gs::PageSet& pages);
    void TargetFromMem(Target* target, const Gs::PageSet& pages);
    // A draw's blend, frame mask and destination alpha test made by OpenGL's fixed functions where they can (FixedPlan: what's
    // left to the shader)
    struct FixedPlan
    {
        bool shaderBlend = false;
        bool wrapAdd = false;
        bool stencilDate = false;
        u32 maskBytes[4] = {};
    };
    FixedPlan PlanFixedFunction(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, bool frame16, bool region,
                                const u32* channelMasks, const u32* channelFull, BatchKey& key);
    const char* downloadReason_ = "";
    std::map<std::string, bool> reported_;
};

HwGs* g_Hw = nullptr;
// Whether g_Hw is there, for the other threads (the UI's settings)
std::atomic<bool> g_HwActive{false};
// TWIN_HW_EXACT_FEEDBACK=1: each primitive reading the target it draws into gets a copy of its own (see selfCopyFor_)
bool ExactFeedback()
{
    static const bool exact = std::getenv("TWIN_HW_EXACT_FEEDBACK") != nullptr && std::getenv("TWIN_HW_EXACT_FEEDBACK")[0] == '1';
    return exact;
}
int g_DitherMode = 2;
int g_FilterMode = 0;
// The texture upscale option: 0 off, 1 2x, 2 4x
int g_TextureUpscale = 0;

HwGs::~HwGs()
{
    gs_.SetBackend(nullptr);
}

bool HwGs::Init()
{
    if (!device_->Init())
    {
        return false;
    }

    InitMemory();
    gs_.SetBackend(this);
    return true;
}

Target* HwGs::TargetFor(u32 bp, u32 bw, u32 psm, u32 height, bool frame)
{
    u32 cls = frame ? FrameClassOf(psm) : ClassOf(psm);
    s32 pageWidth = 0;
    s32 pageHeight = 0;
    PageSize(StoredPsm(cls), &pageWidth, &pageHeight);
    height = std::min<u32>((height + pageHeight - 1) / pageHeight * pageHeight, 2048);
    for (auto& known : targets_)
    {
        if (known->bp == bp && known->bw == bw && known->cls == cls)
        {
            if (known->height >= height)
            {
                return known.get();
            }

            // Taller than it was: a new one is made (the old one's newest pages go to memory's copy as it goes)
            height = std::max(height, known->height);
            DestroyTarget(known.get());
            break;
        }
    }

    auto made = std::make_unique<Target>();
    Target& t = *made;
    t.bp = bp;
    t.bw = bw;
    t.cls = cls;
    t.psm = StoredPsm(cls);
    t.depth = IsDepthClass(cls);
    t.reversed = cls == ClassDepth32 && nextDepthHigh_;
    t.width = bw * 64;
    t.height = height;
    // Native scaling: buffers narrower than the screen (the effects' half resolution buffers) stay at the PS2's resolution and
    // are filtered up where the scene reads them, as PCSX2's native scaling does (their sub-pixels would otherwise pick up the
    // frame's pixel patterns)
    t.scale = (nativeSmall_ && bw < 8) ? 1 : scale_;
    t.aligned = (bp % 32) == 0;
    t.pixels.resize(Gs::PageCount);
    t.rects.assign(Gs::PageCount, Rect{0x7FFF, 0x7FFF, -1, -1});
    for (u32 y = 0; y < t.height; y++)
    {
        for (u32 x = 0; x < t.width; x++)
        {
            u32 page = Gs::PixelPage(t.psm, x, y, bp, bw) & (Gs::PageCount - 1);
            t.pages.set(page);
            t.pixels[page].push_back(y << 11 | x);
            Rect& r = t.rects[page];
            r.left = std::min<s32>(r.left, static_cast<s32>(x));
            r.top = std::min<s32>(r.top, static_cast<s32>(y));
            r.right = std::max<s32>(r.right, static_cast<s32>(x));
            r.bottom = std::max<s32>(r.bottom, static_cast<s32>(y));
        }
    }

    t.complete = device_->MakeTarget(t);
    if (!t.complete)
    {
        std::fprintf(stderr, "graphics: hardware renderer: the target %x/%u class %u (%ux%u at the scale) can't be drawn into: the "
                     "software GS draws it\n", bp, bw, cls, t.width * t.scale, t.height * t.scale);
    }

    targets_.push_back(std::move(made));
    return targets_.back().get();
}

void HwGs::DestroyTarget(Target* target)
{
    Flush();
    {
        Gs::PageSet mine;
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (newest_[page] == target)
            {
                mine.set(page);
            }
        }

        if (mine.any())
        {
            SyncMem(mine);
        }
    }

    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (newest_[page] == target)
        {
            newest_[page] = nullptr;
            newestMask_.reset(page);
        }
    }

    device_->FreeTarget(*target);
    targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [&](const auto& t) { return t.get() == target; }),
                   targets_.end());
}

void HwGs::RectPages(const Target* target, const Rect& rect, Gs::PageSet& pages) const
{
    if (target->aligned)
    {
        s32 pageWidth = 0;
        s32 pageHeight = 0;
        PageSize(target->psm, &pageWidth, &pageHeight);
        u32 base = target->bp / 32;
        for (s32 row = rect.top / pageHeight; row <= rect.bottom / pageHeight; row++)
        {
            for (s32 column = rect.left / pageWidth; column <= rect.right / pageWidth; column++)
            {
                pages.set((base + static_cast<u32>(row) * target->bw + static_cast<u32>(column)) & (Gs::PageCount - 1));
            }
        }

        return;
    }

    Gs::PagesOfRect(target->psm, target->bp, target->bw, rect.left, rect.top, rect.right, rect.bottom, pages);
}

void HwGs::InitMemory()
{
    shadow_.assign(Gs::LocalMemoryBytes, 0);
    memStale_.set();
    cpuStale_.reset();
    shadowGood_.reset();

    // The tables, from the software GS's own layouts (a page at block 0)
    LayoutTables tables;
    for (u32 f = 0; f < 5; f++)
    {
        const FamilyInfo& info = Families[f];
        u32 units = info.unitsPerBlock * 32;
        std::vector<u32>& forward = tables.forward[f];
        std::vector<u32>& inverse = tables.inverse[f];
        forward.assign(info.pageWidth * info.pageHeight, 0);
        inverse.assign(units, 0xFFFFFFFFu);
        for (u32 y = 0; y < info.pageHeight; y++)
        {
            for (u32 x = 0; x < info.pageWidth; x++)
            {
                u32 a = 0;
                switch (f)
                {
                case 0:
                    a = Gs::PixelAddress32(x, y, 0, 1);
                    break;
                case 1:
                    a = Gs::PixelAddress16(x, y, 0, 1);
                    break;
                case 2:
                    a = Gs::PixelAddress16S(x, y, 0, 1);
                    break;
                case 3:
                    a = Gs::PixelAddress8(x, y, 0, 2);
                    break;
                default:
                    a = Gs::PixelAddress4(x, y, 0, 2);
                    break;
                }

                u32 block = a / info.unitsPerBlock;
                u32 unit = a % info.unitsPerBlock;
                forward[y * info.pageWidth + x] = block << 16 | unit;
                inverse[a] = x | y << 16;
            }
        }

        if (std::count(inverse.begin(), inverse.end(), 0xFFFFFFFFu) != 0)
        {
            std::fprintf(stderr, "graphics: hardware renderer: layout %u's table isn't whole\n", f);
        }
    }

    device_->InitMemory(tables);
}

TextureId HwGs::DecodeOnGpu(const Gs::Gs::DrawState& state, s32 left, s32 top, s32 right, s32 bottom, Target* drawing)
{
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    const bool indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                         base.psm == Gs::PSMT4HH;
    GpuTextureKey key = {};
    key.psm = base.psm;
    key.cpsm = indexed ? static_cast<u32>(tex0.cpsm) : 0;
    key.csa = indexed ? static_cast<u32>(tex0.csa) : 0;
    key.clutVersion = indexed ? gs_.clutVersion() : 0;
    key.texa = gs_.texa().value & 0x000000FF000080FFull;
    key.levels = state.levelCount;
    for (u32 i = 0; i < state.levelCount; i++)
    {
        key.bp[i] = state.levels[i].bp;
        key.bw[i] = state.levels[i].bw;
    }

    key.width = base.width;
    key.height = base.height;
    // The previous draw's texture again: nothing to look up
    static GpuTextureKey lastKey = {};
    static GpuTexture* lastEntry = nullptr;
    GpuTexture* entry = nullptr;
    if (lastEntry != nullptr && lastKey == key)
    {
        entry = lastEntry;
    }
    else
    {
        if (gpuTextures_.size() > 4096)
        {
            Flush();
            for (auto& e : gpuTextures_)
            {
                device_->FreeTexture(e.second.id);
            }

            gpuTextures_.clear();
        }

        entry = &gpuTextures_[key];
        lastKey = key;
        lastEntry = entry;
    }

    GpuTexture& t = *entry;
    if (t.id == 0)
    {
        t.levelCount = state.levelCount;
        for (u32 i = 0; i < state.levelCount; i++)
        {
            const Gs::TextureLevel& lv = state.levels[i];
            GpuTexture::Level& L = t.levels[i];
            L.width = lv.width;
            L.height = lv.height;
            L.tilesWide = (lv.width + 7) / 8;
            L.tilesHigh = (lv.height + 7) / 8;
            L.tileEpoch.assign(static_cast<size_t>(L.tilesWide) * L.tilesHigh, 0);
            L.tilePage.resize(L.tileEpoch.size());
            for (u32 ty = 0; ty < L.tilesHigh; ty++)
            {
                for (u32 tx = 0; tx < L.tilesWide; tx++)
                {
                    u32 page = Gs::PixelPage(lv.psm, tx * 8, ty * 8, lv.bp, lv.bw) & (Gs::PageCount - 1);
                    L.tilePage[ty * L.tilesWide + tx] = static_cast<u16>(page);
                    t.pages.set(page);
                }
            }
        }

        t.id = MakeLevels(state);
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (t.pages.test(page))
            {
                t.pageList.push_back(static_cast<u16>(page));
            }
        }
    }

    // Good as it was: every tile decoded, nothing changed anywhere since it was checked, no target holding newer pages of it
    if (t.complete && t.checkedEpoch == epoch_ && (t.pages & newestMask_).none() && t.shown != 0)
    {
        return t.shown;
    }

    // Shown from the content cache: still good while no page of it changed
    if (t.contentEpoch != 0)
    {
        bool good = true;
        for (u16 page : t.pageList)
        {
            if (pageEpoch_[page] >= t.contentEpoch || newestMask_.test(page))
            {
                good = false;
                break;
            }
        }

        if (good)
        {
            t.checkedEpoch = epoch_;
            return t.shown;
        }

        t.contentEpoch = 0;
        t.complete = false;
    }

    // The tiles the draw reads (every level's whole for mipmaps and textures up to 256x256; else level 0's region)
    Rect want[7];
    const bool whole = state.levelCount > 1 || static_cast<u64>(base.width) * base.height <= 65536 || left > right || top > bottom;
    for (u32 i = 0; i < t.levelCount; i++)
    {
        want[i] = whole ? Rect{0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1}
                        : Rect{left, top, right, bottom};
    }

    // Stale tiles, their pages
    Gs::PageSet needed;
    bool anyStale = false;
    Rect stale[7];
    auto findStale = [&]() {
        needed.reset();
        anyStale = false;
        for (u32 i = 0; i < t.levelCount; i++)
        {
            GpuTexture::Level& L = t.levels[i];
            stale[i] = {0x7FFF, 0x7FFF, -1, -1};
            for (s32 ty = want[i].top / 8; ty <= want[i].bottom / 8; ty++)
            {
                for (s32 tx = want[i].left / 8; tx <= want[i].right / 8; tx++)
                {
                    u32 tile = static_cast<u32>(ty) * L.tilesWide + static_cast<u32>(tx);
                    u32 page = L.tilePage[tile];
                    if (L.tileEpoch[tile] != 0 && L.tileEpoch[tile] > pageEpoch_[page] && !newestMask_.test(page))
                    {
                        continue;
                    }

                    anyStale = true;
                    needed.set(page);
                    stale[i].left = std::min(stale[i].left, tx * 8);
                    stale[i].top = std::min(stale[i].top, ty * 8);
                    stale[i].right = std::max(stale[i].right, std::min(tx * 8 + 7, static_cast<s32>(L.width) - 1));
                    stale[i].bottom = std::max(stale[i].bottom, std::min(ty * 8 + 7, static_cast<s32>(L.height) - 1));
                }
            }
        }
    };
    findStale();
    // A texture over the target the batch draws into (a draw reading its own buffer as other texels, a strip a primitive),
    // decoded when the batch began and changed since only by the batch's drawing: read as it was then, as the batch's
    // primitives read a texture that is the target in its own layout (selfCopy_). Decoded again for each primitive, each would
    // end the batch and take a pass to bring memory's copy up to date and another to decode
    if (anyStale && batching_ && drawing != nullptr && key_.colour == drawing && t.decodedBatch == batchSerial_ && !ExactFeedback())
    {
        bool batchOnly = true;
        for (u32 i = 0; i < t.levelCount && batchOnly; i++)
        {
            GpuTexture::Level& L = t.levels[i];
            for (s32 ty = want[i].top / 8; ty <= want[i].bottom / 8 && batchOnly; ty++)
            {
                for (s32 tx = want[i].left / 8; tx <= want[i].right / 8 && batchOnly; tx++)
                {
                    u32 tile = static_cast<u32>(ty) * L.tilesWide + static_cast<u32>(tx);
                    u32 page = L.tilePage[tile];
                    batchOnly = L.tileEpoch[tile] != 0 && L.tileEpoch[tile] > pageEpoch_[page] &&
                                (!newestMask_.test(page) || newest_[page] == drawing);
                }
            }
        }

        if (batchOnly)
        {
            readsBatchStart_ = true;
            return t.shown;
        }
    }

    // A big texture read from a render target a piece at a time (the effects' strips): the whole of it decoded at once,
    // so the next pieces find it decoded. Also once the target's pages are in memory's copy: another texture over them (the
    // same buffer with the next CLUT) would be decoded a piece a draw, each a pass of its own between the draws' passes
    if (!whole && anyStale && (needed & (newestMask_ | fromTargetPages_)).any())
    {
        for (u32 i = 0; i < t.levelCount; i++)
        {
            want[i] = {0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1};
        }

        findStale();
    }

    // Data the CPU wrote: decoded once for its content, wherever and whenever it's uploaded again
    // ($TWIN_HW_CONTENT_CACHE=0 turns it off)
    static const bool contentCache = std::getenv("TWIN_HW_CONTENT_CACHE") == nullptr || std::getenv("TWIN_HW_CONTENT_CACHE")[0] != '0';
    if (contentCache && whole && ((t.pages & (cpuStale_ | newestMask_)).none() || CpuWroteLevels(state)))
    {
        const u64 hash = ContentHash(state);
        ContentTexture& c = contents_[hash];
        if (c.id == 0 && (t.pages & (cpuStale_ | newestMask_)).any())
        {
            // Uploaded where the GPU drew (CpuWroteLevels): decoded by the software GS from local memory, which has the levels
            // right, instead of putting the upload into memory's copy first (two passes, about 50 a frame in CTTR's races) to
            // decode it there; memory's copy takes it when something reads it there
            c.id = DecodeOnCpu(state);
            stats.decodes++;
        }
        else if (c.id == 0)
        {
            Flush();
            SyncMem(t.pages);
            c.id = MakeLevels(state);
            Rect all[7];
            for (u32 i = 0; i < t.levelCount; i++)
            {
                all[i] = {0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1};
            }

            DecodePasses(state, c.id, all, t.levelCount);
            stats.decodes++;
            if (ReplacementsEnabled())
            {
                NativeGraphics::ReplacementKey replacementKey;
                const NativeGraphics::ReplacementImage* image = nullptr;
                if (NativeGraphics::ReplacementKeyFor(gs_, base, tex0, state.context->clamp, &replacementKey))
                {
                    image = NativeGraphics::FindReplacement(replacementKey);
                }

                if (image != nullptr)
                {
                    TextureId& made = replacementTextures_[replacementKey];
                    if (made == 0)
                    {
                        made = device_->MakeImage(image->width, image->height, image->pixels.data());
                    }

                    c.replacement = made;
                    stats.replaced++;
                }
            }
        }
        else
        {
            stats.contentHits++;
        }

        // Upscaled when the option's on and no pack's texture stands in for it
        const bool replaced = c.replacement != 0 && ReplacementsEnabled();
        const u32 upscale = replaced ? 0 : UpscaleFor(base.width, base.height);
        if (upscale != 0 && (c.upscaled == 0 || c.upscaledScale != upscale))
        {
            if (c.upscaled != 0)
            {
                device_->FreeTexture(c.upscaled);
            }

            c.upscaled = device_->MakeUpscaled(c.id, base.width, base.height, upscale);
            c.upscaledScale = upscale;
        }

        // The texture's own tiles stay as they are (none of this was decoded into it)
        t.contentEpoch = ++epoch_;
        t.complete = true;
        t.checkedEpoch = epoch_;
        t.shown = replaced ? c.replacement : (upscale != 0 && c.upscaled != 0) ? c.upscaled : c.id;
        return t.shown;
    }

    if (anyStale)
    {
        // The pages current in memory's copy, then the stale tiles' rectangles decoded from it into the texture's own
        Flush();
        SyncMem(needed);
        DecodePasses(state, t.id, stale, t.levelCount);
        const u32 decodeEpoch = ++epoch_;
        for (u32 i = 0; i < t.levelCount; i++)
        {
            GpuTexture::Level& L = t.levels[i];
            if (stale[i].right < stale[i].left)
            {
                continue;
            }

            for (s32 ty = stale[i].top / 8; ty <= stale[i].bottom / 8; ty++)
            {
                for (s32 tx = stale[i].left / 8; tx <= stale[i].right / 8; tx++)
                {
                    L.tileEpoch[static_cast<u32>(ty) * L.tilesWide + static_cast<u32>(tx)] = decodeEpoch;
                }
            }
        }

        stats.decodes++;
        t.decodedBatch = batchSerial_;
    }

    // A replacement looked for when it's decoded whole from data the CPU wrote (render targets' textures aren't replaced,
    // as in PCSX2): its hash is local memory's, which is current for these pages
    if (anyStale && whole && ReplacementsEnabled() && (t.pages & (cpuStale_ | newestMask_)).none())
    {
        NativeGraphics::ReplacementKey replacementKey;
        const NativeGraphics::ReplacementImage* image = nullptr;
        if (NativeGraphics::ReplacementKeyFor(gs_, base, tex0, state.context->clamp, &replacementKey))
        {
            image = NativeGraphics::FindReplacement(replacementKey);
        }

        t.replacement = 0;
        if (image != nullptr)
        {
            // Each replacement made a texture once (the textures that share it, and the decodes again, use that one)
            TextureId& made = replacementTextures_[replacementKey];
            if (made == 0)
            {
                made = device_->MakeImage(image->width, image->height, image->pixels.data());
            }

            t.replacement = made;
            stats.replaced++;
        }
    }
    else if (anyStale && t.replacement != 0 && (t.pages & (cpuStale_ | newestMask_)).any())
    {
        // Its data is a render target's now: not replaced
        t.replacement = 0;
    }

    // Complete when every tile of every level is good now
    if (whole)
    {
        t.complete = true;
        for (u32 i = 0; i < t.levelCount && t.complete; i++)
        {
            for (u32 e : t.levels[i].tileEpoch)
            {
                if (e == 0)
                {
                    t.complete = false;
                    break;
                }
            }
        }

        t.checkedEpoch = epoch_;
    }

    // Upscaled when the option's on, the texture came whole from data the CPU wrote (not a render target's) and no pack's texture
    // stands in for it: made again when its tiles were decoded again or the scale changed
    t.shown = t.replacement != 0 && ReplacementsEnabled() ? t.replacement : t.id;
    if (t.shown == t.id)
    {
        const u32 upscale = whole && t.complete && (t.pages & (cpuStale_ | newestMask_)).none()
                                ? UpscaleFor(base.width, base.height)
                                : 0;
        if (upscale != 0 && (anyStale || t.upscaled == 0 || t.upscaledScale != upscale))
        {
            if (t.upscaled != 0)
            {
                device_->FreeTexture(t.upscaled);
            }

            t.upscaled = device_->MakeUpscaled(t.id, base.width, base.height, upscale);
            t.upscaledScale = upscale;
        }

        if (upscale != 0 && t.upscaled != 0)
        {
            t.shown = t.upscaled;
        }
    }

    return t.shown;
}

TextureId HwGs::DecodeOnCpu(const Gs::Gs::DrawState& state)
{
    Gs::Gs::DrawState decoded = state;
    gs_.DecodeForBackend(decoded, nullptr, 0);
    const TextureId id = MakeLevels(state);
    for (u32 i = 0; i < decoded.levelCount; i++)
    {
        const Gs::TextureLevel& lv = decoded.levels[i];
        device_->UploadTexture(id, i, Rect{0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1}, lv.texels, lv.stride);
    }

    return id;
}

TextureId HwGs::MakeLevels(const Gs::Gs::DrawState& state)
{
    u32 widths[7];
    u32 heights[7];
    for (u32 i = 0; i < state.levelCount; i++)
    {
        widths[i] = state.levels[i].width;
        heights[i] = state.levels[i].height;
    }

    return device_->MakeTexture(widths, heights, state.levelCount);
}

u32 HwGs::UpscaleFor(u32 width, u32 height) const
{
    // 0 off, 1 2x, 2 4x; not past 4096 texels a side (the big ones are the screens' and the skies' already)
    const u32 scale = g_TextureUpscale == 1 ? 2 : g_TextureUpscale == 2 ? 4 : 0;
    if (scale == 0 || width == 0 || height == 0 || width * scale > 4096 || height * scale > 4096)
    {
        return 0;
    }

    return scale;
}

void HwGs::DropUpscaled()
{
    for (auto& entry : gpuTextures_)
    {
        GpuTexture& t = entry.second;
        if (t.upscaled != 0)
        {
            device_->FreeTexture(t.upscaled);
            t.upscaled = 0;
            t.upscaledScale = 0;
        }

        // (shown again: decided afresh at the next draw that wants it)
        t.complete = false;
        t.contentEpoch = 0;
        t.shown = 0;
    }

    for (auto& entry : contents_)
    {
        ContentTexture& c = entry.second;
        if (c.upscaled != 0)
        {
            device_->FreeTexture(c.upscaled);
            c.upscaled = 0;
            c.upscaledScale = 0;
        }
    }
}

u64 HwGs::ContentHash(const Gs::Gs::DrawState& state)
{
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    const bool indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                         base.psm == Gs::PSMT4HH;
    XXH3_state_t* hash = XXH3_createState();
    XXH3_64bits_reset(hash);
    u64 header[4] = {static_cast<u64>(base.psm) | static_cast<u64>(state.levelCount) << 8 | static_cast<u64>(base.width) << 16 |
                         static_cast<u64>(base.height) << 32,
                     indexed ? (static_cast<u64>(gs_.clutVersion()) | static_cast<u64>(tex0.cpsm) << 32 | static_cast<u64>(tex0.csa) << 40)
                             : 0,
                     gs_.texa().value & 0x000000FF000080FFull, 0};
    XXH3_64bits_update(hash, header, sizeof(header));
    const u8* memory = gs_.memory();
    for (u32 i = 0; i < state.levelCount; i++)
    {
        const Gs::TextureLevel& lv = state.levels[i];
        u32 family = 0;
        bool xorZ = false;
        LayoutOf(lv.psm, &family, &xorZ);
        static constexpr u32 BlockWidth[5] = {8, 16, 16, 16, 32};
        static constexpr u32 BlockHeight[5] = {8, 8, 8, 16, 16};
        const FamilyInfo& info = Families[family];
        for (u32 y = 0; y < lv.height; y += BlockHeight[family])
        {
            for (u32 x = 0; x < lv.width; x += BlockWidth[family])
            {
                u32 unit = 0;
                switch (family)
                {
                case 0:
                    unit = xorZ ? Gs::PixelAddress32Z(x, y, lv.bp, lv.bw) : Gs::PixelAddress32(x, y, lv.bp, lv.bw);
                    break;
                case 1:
                    unit = xorZ ? Gs::PixelAddress16Z(x, y, lv.bp, lv.bw) : Gs::PixelAddress16(x, y, lv.bp, lv.bw);
                    break;
                case 2:
                    unit = xorZ ? Gs::PixelAddress16SZ(x, y, lv.bp, lv.bw) : Gs::PixelAddress16S(x, y, lv.bp, lv.bw);
                    break;
                case 3:
                    unit = Gs::PixelAddress8(x, y, lv.bp, lv.bw);
                    break;
                default:
                    unit = Gs::PixelAddress4(x, y, lv.bp, lv.bw);
                    break;
                }

                u32 block = (unit / info.unitsPerBlock) & (Gs::BlockCount - 1);
                XXH3_64bits_update(hash, memory + static_cast<size_t>(block) * 256, 256);
            }
        }
    }

    u64 result = XXH3_64bits_digest(hash);
    XXH3_freeState(hash);
    return result;
}

void HwGs::DecodePasses(const Gs::Gs::DrawState& state, TextureId texture, const Rect* rects, u32 levels)
{
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    DecodeJob job = {};
    job.levelCount = levels;
    for (u32 i = 0; i < levels; i++)
    {
        job.levels[i] = state.levels[i];
        job.rects[i] = rects[i];
    }

    job.indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                  base.psm == Gs::PSMT4HH;
    job.cpsm = tex0.cpsm;
    job.csa = tex0.csa;
    Gs::RegTexa texa = gs_.texa();
    job.ta0 = texa.ta0;
    job.ta1 = texa.ta1;
    job.aem = texa.aem;
    job.clut = gs_.clut();
    job.clutVersion = gs_.clutVersion();
    device_->DecodeLevels(texture, job);
}

void HwGs::MemFromTarget(Target* target, const Gs::PageSet& pages)
{
    stats.downloads++;
    TraceConversion("from", target, pages);
    fromTargetPages_ |= pages & target->pages;
    if (target->scale > 1)
    {
        scaledPages_ |= pages & target->pages;
    }
    else
    {
        scaledPages_ &= ~(pages & target->pages);
    }

    device_->MemoryFromTarget(*target, pages);
}

void HwGs::TargetFromMem(Target* target, const Gs::PageSet& pages)
{
    target->zeroOnly = false;
    stats.uploads++;
    TraceConversion("into", target, pages);
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[128];
        // Who held the pages newest before (the last target drawn into them)
        std::snprintf(detail, sizeof(detail), "into %x/%u class %u: %zu pages (%s)", target->bp, target->bw, target->cls, (pages & target->pages).count(),
                      cpuReason);
        counts[detail]++;
        if (++total % 400 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "conversions: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }

    // One pass over the pages' rectangle (the device leaves the other pages' pixels as they are)
    Rect all = {0x7FFF, 0x7FFF, -1, -1};
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!pages.test(page) || !target->pages.test(page))
        {
            continue;
        }

        const Rect& r = target->rects[page];
        all.left = std::min(all.left, r.left);
        all.top = std::min(all.top, r.top);
        all.right = std::max(all.right, r.right);
        all.bottom = std::max(all.bottom, r.bottom);
    }

    device_->TargetFromMemory(*target, pages, all);
}

// Memory's copy made current for the pages: targets' newer pages written into it, local memory's uploaded
void HwGs::SyncMem(const Gs::PageSet& pages)
{
    ProcessWrites();
    Gs::PageSet fromTargets = pages & newestMask_;
    if (fromTargets.any())
    {
        Flush();
        for (auto& target : targets_)
        {
            Gs::PageSet mine;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (fromTargets.test(page) && newest_[page] == target.get())
                {
                    mine.set(page);
                }
            }

            if (mine.none())
            {
                continue;
            }

            device_->Resolve(*target);
            MemFromTarget(target.get(), mine);
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (mine.test(page))
                {
                    newest_[page] = nullptr;
                    pageEpoch_[page] = ++epoch_;
                }
            }

            newestMask_ &= ~mine;
            memStale_ &= ~mine;
            shadowGood_ &= ~mine;
            target->owned &= ~mine;
        }
    }

    Gs::PageSet merges = pages & memStale_ & mergePages_ & ~newestMask_;
    if (merges.any())
    {
        MergeWrites(merges);
    }

    Gs::PageSet uploads = pages & memStale_ & ~newestMask_;
    if (uploads.none())
    {
        return;
    }

    Flush();
    device_->UploadMemory(uploads, gs_.memory());
    const u8* memory = gs_.memory();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (uploads.test(page))
        {
            std::memcpy(shadow_.data() + static_cast<size_t>(page) * 8192, memory + static_cast<size_t>(page) * 8192, 8192);
        }
    }

    memStale_ &= ~uploads;
    shadowGood_ |= uploads;
}

// Local memory made current for the pages (read back from memory's copy: the GPU's work waited for)
void HwGs::SyncCpu(const Gs::PageSet& pages)
{
    ProcessWrites();
    if ((pages & (cpuStale_ | newestMask_)).none())
    {
        return;
    }

    MakeCurrent();
    SyncMem(pages);
    Gs::PageSet reads = pages & cpuStale_;
    if (reads.none())
    {
        return;
    }

    stats.readbacks++;
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[128];
        u32 first = 0;
        while (first < Gs::PageCount && !reads.test(first))
        {
            first++;
        }

        std::snprintf(detail, sizeof(detail), "%s: %zu pages from %x", downloadReason_[0] ? downloadReason_ : cpuReason, reads.count(), first);
        counts[detail]++;
        if (++total % 50 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "readbacks: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }
    device_->ReadMemory(reads, shadow_.data());
    gs_.NextGeneration();
    gs_.SetBackendQuiet(true);
    u8* memory = gs_.memory();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!reads.test(page))
        {
            continue;
        }

        std::memcpy(memory + static_cast<size_t>(page) * 8192, shadow_.data() + static_cast<size_t>(page) * 8192, 8192);
        gs_.MarkWritten(page);
    }

    gs_.SetBackendQuiet(false);
    gs_.NextGeneration();
    cpuStale_ &= ~reads;
    shadowGood_ |= reads;
    // Local memory is whole again: its writes are the page's
    mergePages_ &= ~reads;
    for (PartialWrite& w : partialWrites_)
    {
        w.valid &= ~reads;
    }

    DropPartialWrites();
}

void HwGs::ProcessWrites()
{
    if (!anyWritten_.exchange(false, std::memory_order_acquire))
    {
        return;
    }

    // Pages the CPU side wrote: changed unless their bytes are what memory's copy already holds (the game uploads its textures
    // again every frame)
    Gs::PageSet written;
    const u8* memory = gs_.memory();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (written_[page].load(std::memory_order_relaxed) == 0)
        {
            continue;
        }

        written_[page].store(0, std::memory_order_relaxed);
        if (!memStale_.test(page) && shadowGood_.test(page) && newest_[page] == nullptr &&
            std::memcmp(memory + static_cast<size_t>(page) * 8192, shadow_.data() + static_cast<size_t>(page) * 8192, 8192) == 0)
        {
            continue;
        }

        written.set(page);
        newest_[page] = nullptr;
        pageEpoch_[page] = ++epoch_;
    }

    if (written.none())
    {
        return;
    }

    newestMask_ &= ~written;
    memStale_ |= written;
    fromTargetPages_ &= ~written;
    scaledPages_ &= ~written;
    // (local memory has only the rectangles right of a page whose writes are merged into memory's copy: still out of date)
    cpuStale_ &= ~written | mergePages_;
    shadowGood_ &= ~written;
    for (auto& target : targets_)
    {
        target->valid &= ~written;
        target->owned &= ~written;
    }
}

void HwGs::BeforeCpuRead(const Gs::PageSet& pages)
{
    SyncCpu(pages);
}

// A transfer into pages the GPU holds newer (a texture uploaded where an effect's buffer was): memory's copy brought up to
// date on the GPU, the rectangle's pixels put into it once the CPU has written them (MergeWrites), instead of reading the
// pages back first (which waits for the GPU's work so far: 25 a frame in Wrath of Cortex's levels held them to 45 frames a
// second)
void HwGs::BeforeCpuWriteRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const Gs::PageSet& pages,
                              const Gs::PageSet& full)
{
    ProcessWrites();
    // Pages written whole: local memory's will be all of them
    if ((full & mergePages_).any())
    {
        mergePages_ &= ~full;
        for (PartialWrite& w : partialWrites_)
        {
            w.pages &= ~full;
            w.valid &= ~full;
        }

        DropPartialWrites();
    }

    Gs::PageSet newer = pages & (cpuStale_ | newestMask_);
    if (newer.none())
    {
        return;
    }

    u32 family = 0;
    bool xorZ = false;
    LayoutOf(psm, &family, &xorZ);
    const FamilyInfo& info = Families[family];
    const u32 across = std::max<u32>(info.halfWidth ? bw >> 1 : bw, 1);
    const bool layoutKnown = psm == Gs::PSMCT32 || psm == Gs::PSMCT24 || psm == Gs::PSMCT16 || psm == Gs::PSMCT16S ||
                             psm == Gs::PSMT8 || psm == Gs::PSMT4 || psm == Gs::PSMT8H || psm == Gs::PSMT4HL || psm == Gs::PSMT4HH ||
                             psm == Gs::PSMZ32 || psm == Gs::PSMZ24 || psm == Gs::PSMZ16 || psm == Gs::PSMZ16S;
    // The rectangle inside the buffer's width (past it the GS's addresses wrap into other rows' pages) and its pages' rows
    if (!layoutKnown || bw == 0 || left < 0 || top < 0 || static_cast<u32>(right) >= across * info.pageWidth || bottom >= 2048)
    {
        SyncCpu(pages);
        return;
    }

    // Memory's copy takes the targets' newer pages now (the rectangle goes over them); the writes waiting for it on the other
    // pages are merged with this one later, from local memory as it then is (each merge two passes: a texture uploaded a piece
    // at a time over the same pages merged each piece before the next)
    SyncMem(newer & newestMask_);
    // (the oldest records' rectangles forgotten past a few dozen: the reads they spare are of what was written last)
    if (partialWrites_.size() >= 64)
    {
        for (size_t i = 0; i < partialWrites_.size() / 2; i++)
        {
            partialWrites_[i].valid.reset();
        }

        DropPartialWrites();
    }

    PartialWrite write = {psm, bp, bw, Rect{left, top, right, bottom}, newer, newer, {}};
    if (psm != Gs::PSMCT24 && psm != Gs::PSMZ24 && psm != Gs::PSMT8H && psm != Gs::PSMT4HL && psm != Gs::PSMT4HH)
    {
        Gs::WholeBlocksOfRect(psm, bp, bw, left, top, right, bottom, write.blocks);
    }

    partialWrites_.push_back(write);
    mergePages_ |= newer;
}

// A CPU read (a CLUT's load): the pages the GPU holds newer read back, but not where the rectangle is inside one the CPU wrote
// into them since (local memory has it right)
void HwGs::BeforeCpuReadRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const Gs::PageSet& pages)
{
    ProcessWrites();
    Gs::PageSet reads = pages & (cpuStale_ | newestMask_);
    if (reads.any() && CpuHasRect(psm, bp, bw, Rect{left, top, right, bottom}))
    {
        return;
    }

    for (const PartialWrite& w : partialWrites_)
    {
        if (w.psm == psm && w.bp == bp && w.bw == bw && w.rect.left <= left && w.rect.top <= top && w.rect.right >= right &&
            w.rect.bottom >= bottom)
        {
            reads &= ~(w.valid & ~newestMask_);
        }
    }

    if (reads.any())
    {
        SyncCpu(reads);
    }
}

// The CPU's writes of the pages waiting for it put into memory's copy (they're in local memory, its pages' other words not)
void HwGs::MergeWrites(const Gs::PageSet& pages)
{
    Flush();
    // Each layout's rectangles in a pass of their own (8 at most a pass), from memory's copy as the pass before left it
    std::vector<MergeGroup> groups;
    std::vector<bool> merged(partialWrites_.size(), false);
    for (size_t first = 0; first < partialWrites_.size(); first++)
    {
        const PartialWrite& lead = partialWrites_[first];
        if (merged[first] || (lead.pages & pages).none())
        {
            continue;
        }

        MergeGroup group = {lead.psm, lead.bp, lead.bw, 0, {}, 0, {}};
        s32 lowest = 0;
        for (size_t i = first; i < partialWrites_.size() && group.rects.size() < 8; i++)
        {
            const PartialWrite& w = partialWrites_[i];
            if (merged[i] || (w.pages & pages).none() || w.psm != lead.psm || w.bp != lead.bp || w.bw != lead.bw)
            {
                continue;
            }

            merged[i] = true;
            group.pages |= w.pages & pages;
            group.rects.push_back(w.rect);
            lowest = std::max(lowest, w.rect.bottom);
        }

        u32 family = 0;
        bool xorZ = false;
        LayoutOf(lead.psm, &family, &xorZ);
        group.pagesDown = static_cast<u32>(lowest) / Families[family].pageHeight + 1;
        group.unitMask = lead.psm == Gs::PSMCT24 || lead.psm == Gs::PSMZ24 ? 0x00FFFFFFu
                         : lead.psm == Gs::PSMT8H                          ? 0xFF000000u
                         : lead.psm == Gs::PSMT4HL                         ? 0x0F000000u
                         : lead.psm == Gs::PSMT4HH                         ? 0xF0000000u
                                                                           : 0xFFFFFFFFu;
        groups.push_back(std::move(group));
    }

    device_->MergeWrites(pages, gs_.memory(), groups);
    for (PartialWrite& w : partialWrites_)
    {
        w.pages &= ~pages;
    }

    memStale_ &= ~pages;
    mergePages_ &= ~pages;
}

void HwGs::EnsureValid(Target* target, const Gs::PageSet& pages)
{
    Gs::PageSet missing = pages & target->pages & ~target->valid;
    if (missing.none())
    {
        return;
    }

    // With the target's other pages that come the same way: those another target holding one of these newest holds newest
    // too, and those memory's copy already holds current. All in the same passes, so a buffer drawn into a page or two at a
    // time (a model drawn into an effect's buffer, over another view of it) isn't converted a pass a draw
    static const bool narrow = std::getenv("TWIN_HW_NO_WIDE_LOAD") != nullptr;
    if (!narrow)
    {
        ProcessWrites();
        const Gs::PageSet wanted = target->pages & ~target->valid;
        Gs::PageSet fromTargets = missing & newestMask_;
        if (fromTargets.any())
        {
            const Target* holders[4] = {};
            u32 holderCount = 0;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (fromTargets.test(page))
                {
                    const Target* held = newest_[page];
                    if (std::find(holders, holders + holderCount, held) == holders + holderCount && holderCount < 4)
                    {
                        holders[holderCount++] = held;
                    }
                }
            }

            const Gs::PageSet candidates = wanted & newestMask_;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (candidates.test(page) && std::find(holders, holders + holderCount, newest_[page]) != holders + holderCount)
                {
                    missing.set(page);
                }
            }
        }

        missing |= wanted & ~newestMask_ & (~memStale_ | mergePages_);
    }

    // From memory's copy (other targets' newer pages written into it first), on the GPU
    SyncMem(missing);

    Flush();
    TargetFromMem(target, missing);
    target->valid |= missing;
}

// Whether a draw of sprites writes every pixel of the depth buffer: each pixel in one of its sprites, the depth always written
// (no depth test, an alpha test every vertex passes or whose failures still write depth, no destination alpha test)
static bool WritesWholeDepth(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, u32 width, u32 height)
{
    if (state.type != Gs::PrimSprite || count % 2 != 0 || count > 128 || !state.zWritten || state.ztst != Gs::ZtstAlways || state.date)
    {
        return false;
    }

    auto passes = [&](u32 a) {
        switch (state.atst)
        {
        case Gs::AtstNever: return false;
        case Gs::AtstAlways: return true;
        case Gs::AtstLess: return a < state.aref;
        case Gs::AtstLequal: return a <= state.aref;
        case Gs::AtstEqual: return a == state.aref;
        case Gs::AtstGequal: return a >= state.aref;
        case Gs::AtstGreater: return a > state.aref;
        default: return a != state.aref;
        }
    };
    if (state.alphaTest && state.afail != Gs::AfailZbOnly)
    {
        for (u32 i = 0; i < count; i++)
        {
            if (state.textured || !passes(vertexes[i].a))
            {
                return false;
            }
        }
    }

    // The sprites' boxes in pixels (from the ceiling of the start to before the ceiling of the end, scissored), and every cell
    // between their edges inside one of them
    struct Box
    {
        s32 left, top, right, bottom;
    };
    std::vector<Box> boxes;
    std::vector<s32> xs = {0, static_cast<s32>(width)};
    std::vector<s32> ys = {0, static_cast<s32>(height)};
    for (u32 i = 0; i < count; i += 2)
    {
        auto pixel = [](s32 v, s32 offset) { return (v - offset + 15) >> 4; };
        s32 x0 = pixel(static_cast<s32>(vertexes[i].x), state.offsetX), x1 = pixel(static_cast<s32>(vertexes[i + 1].x), state.offsetX);
        s32 y0 = pixel(static_cast<s32>(vertexes[i].y), state.offsetY), y1 = pixel(static_cast<s32>(vertexes[i + 1].y), state.offsetY);
        Box b = {std::max(std::min(x0, x1), state.scissorLeft), std::max(std::min(y0, y1), state.scissorTop),
                 std::min(std::max(x0, x1), state.scissorRight + 1), std::min(std::max(y0, y1), state.scissorBottom + 1)};
        boxes.push_back(b);
        xs.push_back(b.left);
        xs.push_back(b.right);
        ys.push_back(b.top);
        ys.push_back(b.bottom);
    }

    for (s32 y : ys)
    {
        for (s32 x : xs)
        {
            if (x < 0 || y < 0 || x >= static_cast<s32>(width) || y >= static_cast<s32>(height))
            {
                continue;
            }

            bool inside = false;
            for (const Box& b : boxes)
            {
                inside = inside || (x >= b.left && x < b.right && y >= b.top && y < b.bottom);
            }

            if (!inside)
            {
                return false;
            }
        }
    }

    return true;
}

void HwGs::MarkDrawn(Target* target, const Gs::PageSet& pages)
{
    Gs::PageSet drawn = pages & target->pages;
    // Already its newest, every other target's copy out of date: nothing changes
    if ((drawn & ~target->owned).none())
    {
        return;
    }

    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (drawn.test(page))
        {
            newest_[page] = target;
        }
    }

    newestMask_ |= drawn;
    memStale_ |= drawn;
    cpuStale_ |= drawn;
    if (!partialWrites_.empty())
    {
        for (PartialWrite& w : partialWrites_)
        {
            w.valid &= ~drawn;
        }

        DropPartialWrites();
    }
    for (auto& other : targets_)
    {
        if (other.get() != target)
        {
            other->valid &= ~drawn;
            other->owned &= ~drawn;
        }
    }

    target->owned |= drawn;
}

bool HwGs::Fallback(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, const Rect& rect, const char* why)
{
    stats.fallbacks++;
    stats.lastFallback = why;
    if (!reported_[why])
    {
        reported_[why] = true;
        std::fprintf(stderr, "graphics: hardware renderer: left to the software GS: %s\n", why);
    }

    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%s (frame %x/%u/%x msk %08x z %x test %llx)", why, state.fbp, state.fbw, state.fpsm, state.fbmsk, state.zbp,
                      static_cast<unsigned long long>(state.context->test.value));
        counts[detail]++;
        if (++total % 2000 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "fallbacks: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }

    Gs::PageSet pages;
    if (rect.right >= rect.left && rect.bottom >= rect.top)
    {
        Gs::PagesOfRect(state.fpsm, state.fbp, state.fbw, rect.left, rect.top, rect.right, rect.bottom, pages);
        if (state.ztst != Gs::ZtstAlways || state.zWritten)
        {
            Gs::PagesOfRect(state.zpsm, state.zbp, state.fbw, rect.left, rect.top, rect.right, rect.bottom, pages);
        }
    }

    if (state.textured)
    {
        Gs::TexturePagesForDraw(state, vertexes, count, pages);
    }

    downloadReason_ = "fallback";
    BeforeCpuWrite(pages);
    downloadReason_ = "";
    return false;
}

// The texels a range of coordinates reads through a region clamp or repeat, as the software GS wraps them (its own clamp's
// order of tests, the level's size kept): exact for a span the loop can walk, else the whole level
void RegionRange(u32 mode, s32 size, u32 low, u32 high, s32* first, s32* last)
{
    if (*last - *first > 4096)
    {
        *first = 0;
        *last = size - 1;
        return;
    }

    const s32 lo = static_cast<s32>(low);
    const s32 hi = static_cast<s32>(high);
    s32 least = size - 1;
    s32 most = 0;
    for (s32 c = *first; c <= *last; c++)
    {
        s32 wrapped = mode == Gs::WrapRegionClamp ? (c < lo ? lo : hi < c ? hi : c) : static_cast<s32>((static_cast<u32>(c) & low) | high);
        wrapped = std::min(wrapped, size - 1);
        least = std::min(least, wrapped);
        most = std::max(most, wrapped);
    }

    *first = least;
    *last = most;
}

// The fixed function blend for the GS's ((A - B) * C >> 7) + D (A, B, D: 0 source, 1 destination, 2 zero); false when it can't
// make it
bool BlendFor(u32 a, u32 b, u32 d, BlendFactor factor, BlendFactor inverse, BlendOp* op, BlendFactor* source, BlendFactor* destination)
{
    auto set = [&](BlendOp e, BlendFactor s, BlendFactor dst) {
        *op = e;
        *source = s;
        *destination = dst;
        return true;
    };
    constexpr BlendOp Add = BlendOp::Add;
    constexpr BlendOp Subtract = BlendOp::Subtract;
    constexpr BlendOp ReverseSubtract = BlendOp::ReverseSubtract;
    constexpr BlendFactor One = BlendFactor::One;
    constexpr BlendFactor Zero = BlendFactor::Zero;
    if (a == b)
    {
        return d == 0 ? set(Add, One, Zero) : d == 1 ? set(Add, Zero, One) : set(Add, Zero, Zero);
    }

    u32 key = a * 9 + b * 3 + d;
    switch (key)
    {
    case 0 * 9 + 1 * 3 + 1:
        return set(Add, factor, inverse);
    case 0 * 9 + 1 * 3 + 2:
        return set(Subtract, factor, factor);
    case 0 * 9 + 2 * 3 + 1:
        return set(Add, factor, One);
    case 0 * 9 + 2 * 3 + 2:
        return set(Add, factor, Zero);
    case 1 * 9 + 0 * 3 + 0:
        return set(Add, inverse, factor);
    case 1 * 9 + 0 * 3 + 2:
        return set(ReverseSubtract, factor, factor);
    case 1 * 9 + 2 * 3 + 0:
        return set(Add, One, factor);
    case 1 * 9 + 2 * 3 + 2:
        return set(Add, Zero, factor);
    case 2 * 9 + 0 * 3 + 1:
        return set(ReverseSubtract, factor, One);
    case 2 * 9 + 0 * 3 + 0:
        return set(Add, inverse, Zero);
    case 2 * 9 + 0 * 3 + 2:
        return set(Add, Zero, Zero);
    case 2 * 9 + 1 * 3 + 0:
        return set(Subtract, One, factor);
    case 2 * 9 + 1 * 3 + 1:
        return set(Add, Zero, inverse);
    case 2 * 9 + 1 * 3 + 2:
        return set(Add, Zero, Zero);
    default:
        return false;
    }
}

TextureId HwGs::CopyOf(Target* target)
{
    Flush();
    device_->Resolve(*target);
    return device_->CopyTarget(*target);
}

// The GS's pixel rules for a draw, as they are, for a device that makes them all in its shader
void PlanExact(const Gs::Gs::DrawState& state, bool depthUsed, BatchKey& key)
{
    BatchKey::Exact& e = key.exact;
    const u32 fpsm = state.fpsm;
    e.frameKind = (fpsm == Gs::PSMCT24 || fpsm == Gs::PSMZ24) ? 1
                  : (fpsm == Gs::PSMCT16 || fpsm == Gs::PSMCT16S || fpsm == Gs::PSMZ16 || fpsm == Gs::PSMZ16S) ? 2
                                                                                                                   : 0;
    e.fbmsk = state.fbmsk;
    e.frameWrite = state.frameWritten ? 1 : 0;
    if (depthUsed)
    {
        e.depthKind = state.zpsm == Gs::PSMZ32 ? 1 : state.zpsm == Gs::PSMZ24 ? 2 : 3;
        e.ztst = state.ztst;
        e.zWrite = state.zWritten ? 1 : 0;
        e.zmax = state.zmax;
    }

    e.alphaTest = state.alphaTest && state.atst != Gs::AtstAlways ? 1 : 0;
    if (e.alphaTest)
    {
        e.atst = state.atst;
        e.aref = state.aref;
        e.afail = state.afail;
    }

    e.date = state.date ? 1 : 0;
    e.datm = state.date ? state.datm : 0;
    if (state.blend)
    {
        e.blend = 1;
        e.blendA = state.blendA;
        e.blendB = state.blendB;
        e.blendC = state.blendC;
        e.blendD = state.blendD;
        e.blendFix = state.blendFix;
        e.pabe = state.pabe ? 1 : 0;
    }

    e.colclamp = state.colclamp ? 1 : 0;
    e.fba = state.fba ? 1 : 0;
}

// How OpenGL's fixed functions make a draw's blend, frame mask and destination alpha test, and what's left to its shader on a
// copy of the target (the key's GL side)
HwGs::FixedPlan HwGs::PlanFixedFunction(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, bool frame16, bool region,
                                        const u32* channelMasks, const u32* channelFull, BatchKey& key)
{
    bool shaderBlend = false;
    for (u32 i = 0; i < 4; i++)
    {
        if (channelMasks[i] != 0 && channelMasks[i] != channelFull[i])
        {
            shaderBlend = true;
        }
    }

    // The destination alpha test: by a stencil when the draw can't change the alpha bit it tests (every primitive of a batch
    // tests what was there before it), else in the shader
    const bool alphaKept = (channelMasks[3] & 0x80) != 0 || !state.frameWritten;
    bool stencilDate = false;
    if (state.date)
    {
        static const bool noStencilDate = std::getenv("TWIN_HW_NO_STENCIL_DATE") != nullptr;
        if (alphaKept && !noStencilDate)
        {
            stencilDate = true;
        }
        else
        {
            shaderBlend = true;
        }
    }

    // A wrapping additive blend into a 16 bit frame (the shadow volumes' counting: COLCLAMP off, D the destination, A, B and C
    // the source's or FIX): summed in a float buffer, wrapped when it's resolved (its order doesn't matter, so the batch needn't
    // break where its primitives overlap). The alpha bit not written, or written the same everywhere the primitive draws: FBA's,
    // or the vertexes' alpha (no texture's) all on one side of 0x80, with no alpha test to fail (CTTR's shadow volumes write
    // alpha 0); the resolve sets it where the sums' batches drew
    bool wrapAdd = false;
    s32 wrapAlpha = -1;
    if (channelMasks[3] == 0 && (!state.alphaTest || state.atst == Gs::AtstAlways))
    {
        if (state.fba)
        {
            wrapAlpha = 1;
        }
        else if (!state.textured || !state.tcc)
        {
            u32 high = 0;
            for (u32 i = 0; i < count; i++)
            {
                high += vertexes[i].a >= 0x80 ? 1 : 0;
            }

            wrapAlpha = high == 0 ? 0 : high == count ? 1 : -1;
        }
    }

    static const bool noWrapAdd = std::getenv("TWIN_HW_NO_WRAPADD") != nullptr;
    if (!noWrapAdd && state.blend && !state.colclamp && !state.pabe && frame16 && state.blendD == 1 && state.blendA != 1 && state.blendB != 1 &&
        state.blendC != 1 && channelMasks[0] == 0 && channelMasks[1] == 0 && channelMasks[2] == 0 &&
        (channelMasks[3] == 0x80 || wrapAlpha >= 0) && !shaderBlend)
    {
        wrapAdd = true;
        key.wrapAdd = channelMasks[3] == 0x80 ? 1 : 2 + static_cast<u32>(wrapAlpha);
        key.gsBlend[0] = state.blendA;
        key.gsBlend[1] = state.blendB;
        key.gsBlend[2] = state.blendC;
        key.gsBlend[3] = state.blendD;
        key.gsFix = state.blendFix;
    }

    // The blend: GL's blending when it can make it exactly (a 16 bit frame's blend under a stencil DATE too: its truncation
    // to 5 bits is then left to the reads), else the shader
    if (state.blend && !wrapAdd)
    {
        BlendFactor factor = BlendFactor::SourceAlpha;
        BlendFactor inverse = BlendFactor::InverseSourceAlpha;
        bool glCan = state.colclamp && !state.pabe && (!frame16 || stencilDate);
        if (state.blendC == 2 && state.blendFix <= 0x80)
        {
            factor = BlendFactor::ConstantAlpha;
            inverse = BlendFactor::InverseConstantAlpha;
            key.blendFix = state.blendFix;
        }
        else if (state.blendC != 0)
        {
            glCan = false;
        }

        // (a texture's region modes are effects that build on what they drew before, CTTR's glow frame after frame: blended
        // with the GS's truncation, not GL's rounding, unless the game says its region modes aren't: gs.h's roundedRegionBlends)
        if (glCan && !shaderBlend && (!region || gs_.roundedRegionBlends) &&
            BlendFor(state.blendA, state.blendB, state.blendD, factor, inverse, &key.blendOp, &key.blendSource,
                     &key.blendDestination))
        {
            key.blend = 1;
        }
        else
        {
            shaderBlend = true;
            key.blendOp = BlendOp::Add;
            key.blendSource = BlendFactor::Zero;
            key.blendDestination = BlendFactor::Zero;
            key.blendFix = 0;
        }
    }

    u32 maskBytes[4];
    for (u32 i = 0; i < 4; i++)
    {
        // GL's colour mask: a channel wholly masked (the shader merges the partly masked ones)
        maskBytes[i] = channelMasks[i] == channelFull[i] ? 0xFF : 0;
    }

    static const bool why = std::getenv("TWIN_HW_WHY") != nullptr;
    if (why && shaderBlend && state.fbw == 4)
    {
        static int told = 0;
        if (told++ < 5)
        {
            std::fprintf(stderr, "shader blend: frame %x/%u/%x fbmsk %08x date %d datm %u alphaKept %d stencil %d blend %d abcd %u%u%u%u fix %x colclamp %d pabe %d masks %x %x %x %x\n",
                         state.fbp, state.fbw, state.fpsm, state.fbmsk, state.date, state.datm, alphaKept, stencilDate, state.blend, state.blendA,
                         state.blendB, state.blendC, state.blendD, state.blendFix, state.colclamp, state.pabe, channelMasks[0], channelMasks[1],
                         channelMasks[2], channelMasks[3]);
        }
    }

    if (shaderBlend)
    {
        key.gsBlend[0] = state.blend ? state.blendA : 0;
        key.gsBlend[1] = state.blend ? state.blendB : 0;
        key.gsBlend[2] = state.blend ? state.blendC : 0;
        key.gsBlend[3] = state.blend ? state.blendD : 0;
        key.gsFix = state.blend ? state.blendFix : 0;
        key.pabe = state.blend && state.pabe ? 1 : 0;
        key.blend = 0;
        for (u32 i = 0; i < 4; i++)
        {
            key.frameMask[i] = channelMasks[i];
        }
    }

    key.colclamp = state.colclamp ? 1 : 0;
    key.datm = state.datm;
    key.stencilDate = stencilDate ? 1 : 0;

    FixedPlan plan;
    plan.shaderBlend = shaderBlend;
    plan.wrapAdd = wrapAdd;
    plan.stencilDate = stencilDate;
    std::copy(maskBytes, maskBytes + 4, plan.maskBytes);
    return plan;
}

bool HwGs::Draw(Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count)
{
    MakeCurrent();
    ProcessWrites();
    // The colour target's pixels a PS2 pixel (set when it's known)
    s32 n = 1;

    // The rectangle the primitive can draw in (the PS2's pixels, inclusive)
    Rect rect;
    {
        s32 minX = 0x7FFFFFFF;
        s32 minY = 0x7FFFFFFF;
        s32 maxX = -0x7FFFFFFF;
        s32 maxY = -0x7FFFFFFF;
        for (u32 i = 0; i < count; i++)
        {
            s32 x = static_cast<s32>(vertexes[i].x) - state.offsetX;
            s32 y = static_cast<s32>(vertexes[i].y) - state.offsetY;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }

        // The GS's pixels: from the ceiling of the start to before the ceiling of the end (points and lines: the pixels their
        // ends are in)
        rect.left = std::max((minX + 15) >> 4, state.scissorLeft);
        rect.top = std::max((minY + 15) >> 4, state.scissorTop);
        rect.right = std::min(((maxX + 15) >> 4) - 1, state.scissorRight);
        rect.bottom = std::min(((maxY + 15) >> 4) - 1, state.scissorBottom);
        if (state.type == Gs::PrimPoint || state.type == Gs::PrimLine || state.type == Gs::PrimLineStrip)
        {
            rect.left = std::max(minX >> 4, state.scissorLeft);
            rect.top = std::max(minY >> 4, state.scissorTop);
            rect.right = std::min(maxX >> 4, state.scissorRight);
            rect.bottom = std::min(maxY >> 4, state.scissorBottom);
        }
    }

    if (rect.right < rect.left || rect.bottom < rect.top || state.ztst == Gs::ZtstNever)
    {
        stats.drawn++;
        return true;
    }

    // What the GPU can't draw exactly is the software GS's
    const bool depthUsed = state.ztst != Gs::ZtstAlways || state.zWritten;
    const u32 frameClass = FrameClassOf(state.fpsm);
    const bool frame16 = frameClass == ClassColour16 || frameClass == ClassColour16S || frameClass == ClassColourZ16 ||
                         frameClass == ClassColourZ16S;
    if (frameClass == ClassNone || IsDepthClass(frameClass))
    {
        return Fallback(state, vertexes, count, rect, "a frame in a depth or indexed format");
    }

    if (depthUsed && !IsDepthClass(ClassOf(state.zpsm)))
    {
        return Fallback(state, vertexes, count, rect, "a depth buffer in a colour format");
    }

    // A texture's region clamp or repeat: the shader wraps each texel it reads and filters as the GS does (RegionSample), with
    // one filter for the draw (none for mipmaps, or a filter that the level of detail would choose)
    const bool region = state.textured && (state.wms >= Gs::WrapRegionClamp || state.wmt >= Gs::WrapRegionClamp);
    const bool regionLinear = state.magLinear;
    if (region && (state.mipmapped || state.levels[0].scale != 1 || (state.mxl != 0 && regionLinear != ((state.mmin & 1) != 0))))
    {
        return Fallback(state, vertexes, count, rect, "a texture's region clamp or repeat with mipmaps or two filters");
    }

    BatchKey key;
    std::memset(&key, 0, sizeof(key));

    // The frame mask: whole channels by GL's colour mask; part of a channel by the shader on a copy of the destination
    // (a 16 bit frame's channels are its 5 bits and its alpha bit)
    u32 channelMasks[4];
    if (frame16)
    {
        channelMasks[0] = state.fbmsk & 0xF8;
        channelMasks[1] = (state.fbmsk >> 8) & 0xF8;
        channelMasks[2] = (state.fbmsk >> 16) & 0xF8;
        channelMasks[3] = (state.fbmsk >> 24) & 0x80;
    }
    else
    {
        for (u32 i = 0; i < 4; i++)
        {
            channelMasks[i] = (state.fbmsk >> (i * 8)) & 0xFF;
        }
    }

    const u32 channelFull[4] = {frame16 ? 0xF8u : 0xFFu, frame16 ? 0xF8u : 0xFFu, frame16 ? 0xF8u : 0xFFu, frame16 ? 0x80u : 0xFFu};
    // A game that says nothing reads its partly masked bits has them written with their channel (gs.h's looseFrameMasks)
    if (gs_.looseFrameMasks)
    {
        for (u32 i = 0; i < 4; i++)
        {
            if (channelMasks[i] != channelFull[i])
            {
                channelMasks[i] = 0;
            }
        }
    }
    // A device that reads the destination makes every rule in its shader (the key's exact state); OpenGL makes what its fixed
    // functions can
    const bool exact = device_->ReadsDestination();
    if (exact)
    {
        PlanExact(state, depthUsed, key);
    }

    const FixedPlan plan = exact ? FixedPlan{} : PlanFixedFunction(state, vertexes, count, frame16, region, channelMasks, channelFull, key);
    const bool shaderBlend = plan.shaderBlend;
    const bool wrapAdd = plan.wrapAdd;
    const bool stencilDate = plan.stencilDate;
    const u32* maskBytes = plan.maskBytes;

    // A glyph image's sprite (the UI's glyphs)
    Gs::Vertex glyphed[3];
    if (state.textured && state.uv && state.type == Gs::PrimSprite)
    {
        std::copy(vertexes, vertexes + count, glyphed);
        if (gs_.GlyphForBackend(state, glyphed, count))
        {
            vertexes = glyphed;
        }
    }

    // The targets
    // As tall as the primitive draws (a scissor taller than the buffer, as the game's full screen passes set, would make the
    // target run over the buffers past it)
    const u32 height = static_cast<u32>(rect.bottom + 1);
    Target* colour = TargetFor(state.fbp, state.fbw, state.fpsm, height, true);
    u32 highestZ = 0;
    for (u32 i = 0; i < count; i++)
    {
        highestZ = std::max(highestZ, vertexes[i].z);
    }

    nextDepthHigh_ = highestZ >= 0x80000000u;
    Target* depth = depthUsed ? TargetFor(state.zbp, state.fbw, state.zpsm, height) : nullptr;
    nextDepthHigh_ = false;
    if (!colour->complete || (depth != nullptr && !depth->complete))
    {
        return Fallback(state, vertexes, count, rect, "a target the GPU can't hold (no pixels, or past MaxTargetSize at the scale)");
    }

    if (depth != nullptr && depth->cls == ClassDepth32 && state.zWritten)
    {
        // A 32 bit depth buffer cleared to 0 is kept as its next values need, as if that draw had made it: Biohazard Outbreak's
        // menus clear theirs to 0 each frame, then draw their panels and text at 0xFFFFFFxx, which differ by less than a
        // float's step there, so a panel drawn over its text passed the depth test and hid the text (the network screens' rows)
        if (highestZ == 0)
        {
            depth->zeroOnly = depth->zeroOnly || WritesWholeDepth(state, vertexes, count, depth->width, depth->height);
        }
        else
        {
            const bool high = highestZ >= 0x80000000u;
            if (depth->zeroOnly && high != depth->reversed)
            {
                // Zero either way round
                Flush();
                depth->reversed = high;
                device_->ClearDepth(*depth);
            }

            depth->zeroOnly = false;
        }
    }

    n = static_cast<s32>(colour->scale);
    if (static_cast<u32>(rect.right) >= colour->width)
    {
        rect.right = static_cast<s32>(colour->width) - 1;
    }

    if (rect.right < rect.left)
    {
        stats.drawn++;
        return true;
    }

    // The texture (selfRead: the target's own copy)
    bool selfRead = false;
    readsBatchStart_ = false;
    Rect decodeRect = {};
    u32 program = 0;
    bool textureFromScaled = false;
    TextureId texture = 0;
    u32 texAlpha = 0;
    if (state.textured)
    {
        const Gs::TextureLevel& level = state.levels[0];
        program |= BitTextured | (state.tfx << BitTfxShift) | (state.tcc ? BitTcc : 0) | (state.uv ? BitUv : 0);
        key.gsTexWidth = static_cast<f32>(level.width);
        key.gsTexHeight = static_cast<f32>(level.height);
        key.texWidth = key.gsTexWidth;
        key.texHeight = key.gsTexHeight;
        key.clampS = state.wms == Gs::WrapClamp ? 1 : 0;
        key.clampT = state.wmt == Gs::WrapClamp ? 1 : 0;
        key.magFilter = state.magLinear ? Filter::Linear : Filter::Nearest;
        key.minFilter = key.magFilter;
        if (state.mipmapped)
        {
            key.minFilter = static_cast<Filter>(std::min<u32>(state.mmin, 5));
            key.maxLevel = state.levelCount - 1;
            key.lodK = state.k;
            key.lodL = state.l;
            key.mxl = state.mxl;
            program |= BitMipmap | (state.lodConstant ? BitLodConstant : 0);
        }

        s32 left = 0;
        s32 top = 0;
        s32 right = static_cast<s32>(level.width) - 1;
        s32 bottom = static_cast<s32>(level.height) - 1;
        s32 l = 0;
        s32 t = 0;
        s32 r = 0;
        s32 b = 0;
        if (Gs::TextureRegionForDraw(state, vertexes, count, &l, &t, &r, &b))
        {
            // As the decoder takes it: a clamped coordinate's range clamped, a repeating one's whole unless it stays inside
            s32 w = static_cast<s32>(level.width);
            s32 h = static_cast<s32>(level.height);
            if (state.wms >= Gs::WrapRegionClamp)
            {
                RegionRange(state.wms, w, level.minu, level.maxu, &l, &r);
            }

            if (state.wmt >= Gs::WrapRegionClamp)
            {
                RegionRange(state.wmt, h, level.minv, level.maxv, &t, &b);
            }

            if (state.wms == Gs::WrapClamp || (l >= 0 && r < w))
            {
                left = std::clamp(l, 0, w - 1);
                right = std::clamp(r, 0, w - 1);
            }

            if (state.wmt == Gs::WrapClamp || (t >= 0 && b < h))
            {
                top = std::clamp(t, 0, h - 1);
                bottom = std::clamp(b, 0, h - 1);
            }
        }

        // The software GS's decoder (on the CPU) for replacement textures and their dumps
        static const bool cpuTextures = std::getenv("TWIN_HW_DUMP_TEXTURES") != nullptr || std::getenv("TWIN_HW_CPU_TEXTURES") != nullptr;
        // A target's own buffer in its own layout (no mipmaps; its rectangle within the target)
        Target* source = nullptr;
        u32 sourceClass = ClassOf(level.psm);
        if (level.cacheId == nullptr && !state.mipmapped && sourceClass != ClassNone && !IsDepthClass(sourceClass) &&
            right >= left && bottom >= top)
        {
            for (auto& known : targets_)
            {
                // The region the draw reads inside the target (a repeating texture whose coordinates stay inside it doesn't
                // repeat)
                // (two texels past its edge allowed: a sprite's end coordinate is past its last pixel's and the region reaches
                // one more for the filter; the filter's weight there is nothing and the sampler clamps)
                if (known->complete && known->bp == level.bp && known->bw == level.bw && known->cls == sourceClass && left >= 0 &&
                    top >= 0 &&
                    static_cast<u32>(right) <= known->width + 1 && static_cast<u32>(bottom) <= known->height + 1 &&
                    (state.wms == Gs::WrapClamp || static_cast<u32>(right) < level.width) &&
                    (state.wmt == Gs::WrapClamp || static_cast<u32>(bottom) < level.height))
                {
                    source = known.get();
                }
            }
        }

        if (source != nullptr)
        {
            Gs::PageSet pages;
            RectPages(source, Rect{left, top, std::min(right, static_cast<s32>(source->width) - 1),
                                   std::min(bottom, static_cast<s32>(source->height) - 1)}, pages);
            cpuReason = "texture";
            EnsureValid(source, pages);
            if (source != colour && device_->Pending(*source))
            {
                Flush();
                device_->Resolve(*source);
            }

            if (source != colour)
            {
                texture = source->texture;
            }
            else if (selfCopyFor_ == colour && batching_ && !ExactFeedback())
            {
                // The batch drawing into it already reads a copy of it, made at its start
                texture = selfCopy_;
                selfRead = true;
            }
            else
            {
                texture = CopyOf(source);
                selfCopyFor_ = colour;
                selfCopy_ = texture;
                selfRead = true;
            }
            key.texWidth = static_cast<f32>(source->width);
            key.texHeight = static_cast<f32>(source->height);
            static const bool noSnap = std::getenv("TWIN_HW_NO_SNAP") != nullptr;
            if (!noSnap && source->scale > colour->scale && key.magFilter == Filter::Nearest && key.minFilter == Filter::Nearest)
            {
                key.texSnap = source->scale;
            }
            // Its edge isn't the texture's: a filter reaching past the region stops at the target's edge
            key.clampS = 1;
            key.clampT = 1;
            if (level.psm == Gs::PSMCT24)
            {
                texAlpha = 1;
            }
            else if (sourceClass != ClassColour32)
            {
                texAlpha = 2;
            }
        }
        else if (level.cacheId == nullptr && !cpuTextures)
        {
            // Decoded on the GPU from memory's copy
            decodeRect = {left, top, right, bottom};
            texture = DecodeOnGpu(state, left, top, right, bottom, colour);
            // Decoded from memory read back from an upscaled target: its texels are the blocks' first samples (the region's
            // pages looked for only when the whole texture has some: a region a draw is many pages' walk)
            if ((LevelPages(level) & scaledPages_).any())
            {
                Gs::PageSet texturePages;
                Gs::PagesOfRect(level.psm, level.bp, level.bw, left, top, right, bottom, texturePages);
                textureFromScaled = (texturePages & scaledPages_).any();
            }
            // The filtering setting (the game's textures, not its buffers read as other texels: their neighbours are other
            // pixels' bytes)
            if (g_FilterMode >= 1 && !textureFromScaled)
            {
                key.magFilter = Filter::Linear;
                key.minFilter = state.mipmapped ? (g_FilterMode == 2 ? Filter::LinearMipLinear : Filter::LinearMipNearest) : Filter::Linear;
                key.anisotropy = g_FilterMode == 2 && state.mipmapped ? 16 : 1;
            }
        }
        else
        {
            // Decoded by the software GS from local memory (made current for the pages it reads)
            if (level.cacheId == nullptr)
            {
                // Local memory current for the levels' whole pages: nothing to sync (the usual case); else the pages the
                // decode reads
                ProcessWrites();
                bool synced = true;
                for (u32 i = 0; i < state.levelCount && synced; i++)
                {
                    synced = (LevelPages(state.levels[i]) & newestMask_).none();
                }

                if (!synced)
                {
                    Gs::PageSet pages;
                    Gs::TexturePagesForDraw(state, vertexes, count, pages);
                    static char reasonText[128];
                    std::snprintf(reasonText, sizeof(reasonText), "texture %x/%u/%x %ux%u for frame %x/%u/%x", level.bp, level.bw, level.psm,
                                  level.width, level.height, state.fbp, state.fbw, state.fpsm);
                    downloadReason_ = reasonText;
                    BeforeCpuRead(pages);
                    downloadReason_ = "";
                }

                gs_.DecodeForBackend(state, vertexes, count);
            }

            CachedTexture& cached = textures_[state.levels[0].cacheId];
            const u32 levels = state.levelCount;
            if (cached.id == 0 || cached.width != level.width || cached.height != level.height || cached.levels != levels)
            {
                Flush();
                if (cached.id != 0)
                {
                    device_->FreeTexture(cached.id);
                }

                cached.id = MakeLevels(state);
                cached.width = level.width;
                cached.height = level.height;
                cached.levels = levels;
                std::fill(std::begin(cached.versions), std::end(cached.versions), ~0u);
                cached.tiles.assign(static_cast<size_t>((level.width + 7) / 8) * ((level.height + 7) / 8), 0);
            }

            // Pending draws may sample the texture: they're drawn before texels they read change (its content version), not
            // for tiles added (they read none of those)
            bool prepared = false;
            auto prepare = [&]() {
                if (!prepared)
                {
                    bool changed = false;
                    for (u32 i = 0; i < levels; i++)
                    {
                        changed |= cached.versions[i] != state.levels[i].version && cached.versions[i] != ~0u;
                    }

                    if (changed)
                    {
                        Flush();
                    }

                    prepared = true;
                }
            };
            for (u32 i = 0; i < levels; i++)
            {
                const Gs::TextureLevel& lv = state.levels[i];
                if (lv.texels == nullptr)
                {
                    continue;
                }

                // Level 0's region (the tiles of it not uploaded at this version); the others whole when they change
                Rect want = {0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1};
                if (i == 0)
                {
                    if (cached.versions[0] != lv.version)
                    {
                        std::fill(cached.tiles.begin(), cached.tiles.end(), 0);
                    }

                    if (left <= right && top <= bottom)
                    {
                        want = {left, top, right, bottom};
                    }

                    const u32 tilesWide = (lv.width + 7) / 8;
                    Rect missing = {0x7FFF, 0x7FFF, -1, -1};
                    for (s32 ty = want.top / 8; ty <= want.bottom / 8; ty++)
                    {
                        for (s32 tx = want.left / 8; tx <= want.right / 8; tx++)
                        {
                            u8& tile = cached.tiles[static_cast<size_t>(ty) * tilesWide + static_cast<size_t>(tx)];
                            if (tile == 0)
                            {
                                tile = 1;
                                missing.left = std::min(missing.left, tx * 8);
                                missing.top = std::min(missing.top, ty * 8);
                                missing.right = std::max(missing.right, std::min(tx * 8 + 7, static_cast<s32>(lv.width) - 1));
                                missing.bottom = std::max(missing.bottom, std::min(ty * 8 + 7, static_cast<s32>(lv.height) - 1));
                            }
                        }
                    }

                    cached.versions[0] = lv.version;
                    if (missing.right < missing.left)
                    {
                        continue;
                    }

                    // The missing tiles' rectangle: any other tile in it is either uploaded already (decoded, the same texels)
                    // or not reached by a draw yet
                    want = missing;
                }
                else if (cached.versions[i] == lv.version && cached.tilesVersions[i] == lv.tilesVersion)
                {
                    continue;
                }

                prepare();
                const u32* from = lv.texels + static_cast<size_t>(want.top) * lv.stride + static_cast<size_t>(want.left);
                device_->UploadTexture(cached.id, i, want, from, lv.stride);
                cached.versions[i] = lv.version;
                cached.tilesVersions[i] = lv.tilesVersion;
            }

            texture = cached.id;

            // A replacement of level 0 (PCSX2's packs), looked for when its texels change; and the dump of it in PCSX2's
            // names (TWIN_HW_DUMP_TEXTURES=folder)
            static const char* dumpFolder = std::getenv("TWIN_HW_DUMP_TEXTURES");
            const Gs::TextureLevel& base = state.levels[0];
            if (base.cacheId != nullptr && base.texels != nullptr && (ReplacementsEnabled() || dumpFolder != nullptr) &&
                cached.replacementVersion != base.version)
            {
                cached.replacementVersion = base.version;
                NativeGraphics::ReplacementKey replacementKey;
                bool keyed = NativeGraphics::ReplacementKeyFor(gs_, base, state.context->tex0, state.context->clamp, &replacementKey);
                if (keyed && dumpFolder != nullptr && !cached.dumped && base.stride == base.width &&
                    (left <= 0 && top <= 0 && right >= static_cast<s32>(base.width) - 1 && bottom >= static_cast<s32>(base.height) - 1))
                {
                    // The whole texture decoded (a draw that read all of it): written as PCSX2 dumps it, alpha in the PS2's range
                    cached.dumped = true;
                    std::string path = std::string(dumpFolder) + "/" + NativeGraphics::ReplacementName(replacementKey) + ".png";
                    WritePng(path, base.texels, base.width, base.height);
                }

                const NativeGraphics::ReplacementImage* image = keyed ? NativeGraphics::FindReplacement(replacementKey) : nullptr;
                if (image != nullptr)
                {
                    Flush();
                    if (cached.replacement != 0)
                    {
                        device_->FreeTexture(cached.replacement);
                    }

                    cached.replacement = device_->MakeImage(image->width, image->height, image->pixels.data());
                    cached.replacementMipmaps = true;
                }
                else if (cached.replacement != 0)
                {
                    Flush();
                    device_->FreeTexture(cached.replacement);
                    cached.replacement = 0;
                }
            }

            if (cached.replacement != 0 && ReplacementsEnabled() && !region)
            {
                // Sampled with the original's normalised coordinates; its mipmaps for the original's levels
                texture = cached.replacement;
                stats.replaced++;
            }
            // The filtering setting (the game's textures, not its buffers)
            if (g_FilterMode >= 1)
            {
                key.magFilter = Filter::Linear;
                key.minFilter = state.mipmapped ? (g_FilterMode == 2 ? Filter::LinearMipLinear : Filter::LinearMipNearest) : Filter::Linear;
                key.anisotropy = g_FilterMode == 2 && state.mipmapped ? 16 : 1;
            }
            if (textures_.size() > 4096)
            {
                // Too many: they're made again as they're needed
                Flush();
                for (auto& entry : textures_)
                {
                    device_->FreeTexture(entry.second.id);
                }

                textures_.clear();
                return Draw(state, vertexes, count);
            }
        }

        program |= texAlpha << BitTexAlphaShift;
        key.texture = texture;
        if (texAlpha != 0)
        {
            Gs::RegTexa texa = gs_.texa();
            key.ta0 = texa.ta0;
            key.ta1 = texa.ta1;
            key.aem = texa.aem;
        }

        // A device that makes the GS's rules in its shader samples the texture as the GS does too, unless an option changes the
        // textures (their filtering, upscale or replacements)
        if (exact && g_FilterMode == 0 && g_TextureUpscale == 0 && !ReplacementsEnabled())
        {
            BatchKey::Exact& e = key.exact;
            e.gsSample = 1;
            e.levelCount = state.levelCount;
            e.magLinear = state.magLinear ? 1 : 0;
            e.mmin = state.mmin;
            e.mxl = state.mxl;
            key.regionMode[0] = state.wms;
            key.regionMode[1] = state.wmt;
            key.regionMode[2] = state.magLinear ? 1 : 0;
            key.regionMode[3] = source != nullptr ? source->scale : 1;
            key.regionBounds[0] = level.minu;
            key.regionBounds[1] = level.maxu;
            key.regionBounds[2] = level.minv;
            key.regionBounds[3] = level.maxv;
        }

        if (region)
        {
            // The shader reads whole texels (samples of a target's) and filters them itself
            program |= BitRegion;
            key.regionMode[0] = state.wms;
            key.regionMode[1] = state.wmt;
            key.regionMode[2] = regionLinear ? 1 : 0;
            key.regionMode[3] = source != nullptr ? source->scale : 1;
            key.regionBounds[0] = level.minu;
            key.regionBounds[1] = level.maxu;
            key.regionBounds[2] = level.minv;
            key.regionBounds[3] = level.maxv;
            key.magFilter = Filter::Nearest;
            key.minFilter = Filter::Nearest;
            key.anisotropy = 1;
            key.texSnap = 0;
        }
    }

    // The targets current for the pages the primitive draws in
    Gs::PageSet drawnPages;
    RectPages(colour, rect, drawnPages);
    cpuReason = "frame";
    EnsureValid(colour, drawnPages);
    Gs::PageSet depthPages;
    if (depth != nullptr)
    {
        RectPages(depth, rect, depthPages);
        cpuReason = "depth";
        EnsureValid(depth, depthPages);
    }

    // The rest of the key
    if (state.fog)
    {
        program |= BitFog;
        key.fogColour = state.fogR | state.fogG << 8 | state.fogB << 16;
    }

    if (frame16)
    {
        program |= BitFrame16;
        if (state.dither && g_DitherMode != 0)
        {
            program |= BitDither;
            key.dimx = gs_.dimx();
            key.ditherScale = g_DitherMode == 1 ? static_cast<u32>(n) : 1;
        }
    }

    if (state.fba)
    {
        program |= BitFba;
    }

    if (wrapAdd)
    {
        program |= BitWrapAdd;
    }

    if (shaderBlend)
    {
        program |= BitShaderBlend;
        if (state.date && !stencilDate)
        {
            program |= BitDate;
        }

        if (state.fpsm == Gs::PSMCT24 || state.fpsm == Gs::PSMZ24)
        {
            program |= BitFrame24;
        }
    }

    if (depth != nullptr)
    {
        program |= BitDepth;
        key.depthReverse = depth->reversed ? 1 : 0;
        key.zMaxU = state.zmax;
        if (depth->reversed)
        {
            key.depthFunc = state.ztst == Gs::ZtstAlways ? DepthFunc::Always : state.ztst == Gs::ZtstGequal ? DepthFunc::Lequal : DepthFunc::Less;
        }
        else
        {
            key.depthFunc = state.ztst == Gs::ZtstAlways ? DepthFunc::Always : state.ztst == Gs::ZtstGequal ? DepthFunc::Gequal : DepthFunc::Greater;
        }

        // A texture at the PS2's resolution made from upscaled buffers (an effect's lookup of the depth, say) tested against
        // the upscaled depth: tested at the GS's sample point instead, as the texture was made there (else the samples near
        // where the test changes pass with the texel of the other side: a line along it)
        static const bool noNativeDepth = std::getenv("TWIN_HW_NO_NATIVE_DEPTH") != nullptr;
        if (!noNativeDepth && textureFromScaled && depth->scale > 1 && !state.zWritten &&
            (state.ztst == Gs::ZtstGequal || state.ztst == Gs::ZtstGreater))
        {
            key.nativeDepthTest = state.ztst;
            key.depthFunc = DepthFunc::Always;
            program |= BitNativeDepth;
        }
        key.depthWrite = state.zWritten ? 1 : 0;
        key.zMax = static_cast<f32>(state.zmax);
    }

    // A texture at the PS2's resolution made from upscaled buffers, unfiltered, drawn into an upscaled target: every sample
    // reads the texel its block's first sample (the GS's sample point) reads. An effect reading a buffer as other texels (its
    // depth as 8 bit indexes, say) steps through them a texel a pixel; the samples between would read other pixels' bytes
    if (textureFromScaled && n > 1 && key.magFilter == Filter::Nearest && key.minFilter == Filter::Nearest)
    {
        program |= BitNativeTexel;
    }

    // A failed alpha test whose AFAIL writes all a passing pixel writes changes nothing (FB_ONLY with the depth not written,
    // RGB_ONLY with neither the alpha nor the depth, ZB_ONLY with no frame): drawn without the test, in one pass. Two passes
    // would draw the batch's failing pixels after all its passing ones, where overlapping primitives (WoC's depth-of-field
    // sprites, tested GEQUAL with FB_ONLY) need them in the GS's order
    const bool alphaWritten = state.frameWritten && channelMasks[3] != channelFull[3];
    const bool failWritesAll = (state.afail == Gs::AfailFbOnly && !state.zWritten) ||
                               (state.afail == Gs::AfailRgbOnly && !state.zWritten && !alphaWritten) ||
                               (state.afail == Gs::AfailZbOnly && !state.frameWritten);
    if (state.alphaTest && state.atst != Gs::AtstAlways && !failWritesAll)
    {
        program |= BitAlphaTest | (state.atst << BitAtstShift);
        key.aref = state.aref;
        key.afail = state.afail;
        key.alphaTested = 1;
    }

    key.colourMask = (maskBytes[0] == 0 ? 1 : 0) | (maskBytes[1] == 0 ? 2 : 0) | (maskBytes[2] == 0 ? 4 : 0) |
                     (maskBytes[3] == 0 ? 8 : 0);
    if (frame16)
    {
        // A 16 bit frame's alpha is its top bit: FBMSK's top bit masks it
        key.colourMask = (key.colourMask & 7) | ((state.fbmsk & 0x80000000u) == 0 ? 8 : 0);
    }

    key.scissor[0] = state.scissorLeft;
    key.scissor[1] = state.scissorTop;
    key.scissor[2] = state.scissorRight;
    key.scissor[3] = state.scissorBottom;
    key.colour = colour;
    key.depth = depth;
    key.program = program;
    key.primitive = state.type == Gs::PrimPoint ? Topology::Points
                    : (state.type == Gs::PrimLine || state.type == Gs::PrimLineStrip) ? Topology::Lines
                                                                                       : Topology::Triangles;
    if (batching_ && std::memcmp(&key, &key_, sizeof(key)) != 0)
    {
        Flush();
    }

    BatchShape shape = {rect, false, {}, {}};
    if (count == 3 && (state.type == Gs::PrimTriangle || state.type == Gs::PrimTriangleStrip || state.type == Gs::PrimTriangleFan))
    {
        shape.triangle = true;
        for (u32 i = 0; i < 3; i++)
        {
            shape.x[i] = static_cast<s32>(vertexes[i].x) - state.offsetX;
            shape.y[i] = static_cast<s32>(vertexes[i].y) - state.offsetY;
        }
    }

    if (batching_ && shaderBlend)
    {
        for (const BatchShape& earlier : batchShapes_)
        {
            if (ShapesOverlap(shape, earlier))
            {
                Flush();
                break;
            }
        }
    }

    if (selfRead && selfCopyFor_ != colour)
    {
        // The batch whose copy it was going to read was drawn first: a copy with that batch's drawing
        key.texture = CopyOf(colour);
        selfCopyFor_ = colour;
        selfCopy_ = key.texture;
    }

    if (readsBatchStart_ && !batching_)
    {
        // The batch whose start's decode it was going to read was drawn first: decoded again with that batch's drawing
        readsBatchStart_ = false;
        key.texture = DecodeOnGpu(state, decodeRect.left, decodeRect.top, decodeRect.right, decodeRect.bottom, colour);
    }

    static const char* tracePrim = std::getenv("TWIN_HW_TRACE_PRIM");
    if (tracePrim != nullptr && std::strtoull(tracePrim, nullptr, 10) == gs_.primitivesDrawn)
    {
        std::fprintf(stderr, "hwprim: #%llu program %x depthFunc %x depthWrite %u colourMask %x stencilDate %u datm %u wrapAdd %u "
                     "blend %u gsBlend %u,%u,%u,%u fix %u colclamp %u depth %p scissor %d,%d,%d,%d rect %d,%d,%d,%d\n",
                     static_cast<unsigned long long>(gs_.primitivesDrawn), key.program, key.depthFunc, key.depthWrite, key.colourMask,
                     key.stencilDate, key.datm, key.wrapAdd, key.blend, key.gsBlend[0], key.gsBlend[1], key.gsBlend[2], key.gsBlend[3],
                     key.gsFix, key.colclamp, static_cast<void*>(key.depth), key.scissor[0], key.scissor[1],
                     key.scissor[2], key.scissor[3], rect.left, rect.top, rect.right, rect.bottom);
    }

    key_ = key;
    batching_ = true;
    if (shaderBlend)
    {
        batchShapes_.push_back(shape);
    }

    // The vertexes
    auto place = [&](const Gs::Vertex& v, HwVertex& out) {
        const f32 nudge = VertexNudge();
        out.x = static_cast<f32>(static_cast<s32>(v.x) - state.offsetX) / 16.0f * static_cast<f32>(n) + 0.5f + nudge;
        out.y = static_cast<f32>(static_cast<s32>(v.y) - state.offsetY) / 16.0f * static_cast<f32>(n) + 0.5f + nudge;
        out.z = v.z;
        out.r = v.r;
        out.g = v.g;
        out.b = v.b;
        out.a = v.a;
        if (state.uv)
        {
            out.s = static_cast<f32>(v.u) / 16.0f;
            out.t = static_cast<f32>(v.v) / 16.0f;
            out.q = 1.0f;
        }
        else
        {
            out.s = v.s;
            out.t = v.t;
            out.q = v.q;
        }

        out.fog = static_cast<f32>(v.f);
    };

    switch (state.type)
    {
    case Gs::PrimSprite:
    {
        // Two corners: the quad between them, its texture coordinates the corners', everything else the second vertex's
        const Gs::Vertex& a = vertexes[0];
        const Gs::Vertex& b = vertexes[1];
        HwVertex corners[4];
        Gs::Vertex v = b;
        for (u32 i = 0; i < 4; i++)
        {
            const Gs::Vertex& xs = (i & 1) ? b : a;
            const Gs::Vertex& ys = (i & 2) ? b : a;
            v.x = xs.x;
            v.u = xs.u;
            v.s = xs.s;
            v.y = ys.y;
            v.v = ys.v;
            v.t = ys.t;
            v.q = b.q;
            place(v, corners[i]);
        }

        if (!state.uv)
        {
            // STQ sprites: S over Q and T over Q across, with the second vertex's Q
            for (u32 i = 0; i < 4; i++)
            {
                const Gs::Vertex& xs = (i & 1) ? b : a;
                const Gs::Vertex& ys = (i & 2) ? b : a;
                corners[i].s = xs.s;
                corners[i].t = ys.t;
                corners[i].q = b.q;
            }
        }

        if (program & BitNativeTexel)
        {
            // Covering the GS's pixels' whole blocks: an edge between two samples of a block (a sprite 1.5 pixels high) would
            // leave the block's samples past it as they were, a line of them in every such block. The texture coordinates
            // carried along their planes
            auto along = [](f32 from, f32 to, f32 valueFrom, f32 valueTo, f32 at) {
                return from == to ? valueFrom : valueFrom + (valueTo - valueFrom) * (at - from) / (to - from);
            };
            const f32 xa = corners[0].x, xb = corners[1].x, ya = corners[0].y, yb = corners[2].y;
            const f32 left = static_cast<f32>(rect.left * n), right = static_cast<f32>((rect.right + 1) * n);
            const f32 top = static_cast<f32>(rect.top * n), bottom = static_cast<f32>((rect.bottom + 1) * n);
            const f32 toXa = xa <= xb ? left : right, toXb = xa <= xb ? right : left;
            const f32 toYa = ya <= yb ? top : bottom, toYb = ya <= yb ? bottom : top;
            const f32 sa = along(xa, xb, corners[0].s, corners[1].s, toXa), sb = along(xa, xb, corners[0].s, corners[1].s, toXb);
            const f32 ta = along(ya, yb, corners[0].t, corners[2].t, toYa), tb = along(ya, yb, corners[0].t, corners[2].t, toYb);
            for (u32 i = 0; i < 4; i++)
            {
                corners[i].x = (i & 1) ? toXb : toXa;
                corners[i].s = (i & 1) ? sb : sa;
                corners[i].y = (i & 2) ? toYb : toYa;
                corners[i].t = (i & 2) ? tb : ta;
            }
        }

        static constexpr u32 Order[6] = {0, 1, 2, 1, 3, 2};
        for (u32 i : Order)
        {
            vertexes_.push_back(corners[i]);
        }

        break;
    }
    case Gs::PrimPoint:
    {
        HwVertex out;
        place(vertexes[0], out);
        vertexes_.push_back(out);
        break;
    }
    case Gs::PrimLine:
    case Gs::PrimLineStrip:
    {
        HwVertex out[2];
        place(vertexes[0], out[0]);
        place(vertexes[1], out[1]);
        if (!state.gouraud)
        {
            out[0].r = out[1].r;
            out[0].g = out[1].g;
            out[0].b = out[1].b;
            out[0].a = out[1].a;
        }

        vertexes_.push_back(out[0]);
        vertexes_.push_back(out[1]);
        break;
    }
    default:
    {
        HwVertex out[3];
        for (u32 i = 0; i < 3; i++)
        {
            place(vertexes[i], out[i]);
        }

        if (!state.gouraud)
        {
            for (u32 i = 0; i < 2; i++)
            {
                out[i].r = out[2].r;
                out[i].g = out[2].g;
                out[i].b = out[2].b;
                out[i].a = out[2].a;
            }
        }

        for (const HwVertex& v : out)
        {
            vertexes_.push_back(v);
        }

        break;
    }
    }

    MarkDrawn(colour, drawnPages);
    if (depth != nullptr && state.zWritten)
    {
        MarkDrawn(depth, depthPages);
    }

    if (tracePrim != nullptr && std::strtoull(tracePrim, nullptr, 10) == gs_.primitivesDrawn)
    {
        for (size_t i = vertexes_.size() >= 3 ? vertexes_.size() - 3 : 0; i < vertexes_.size(); i++)
        {
            std::fprintf(stderr, "hwprim:   vertex %.6f,%.6f z %x (of %zu in the batch; colour target %ux%u scale %u)\n", vertexes_[i].x,
                         vertexes_[i].y, vertexes_[i].z, vertexes_.size(), colour->width, colour->height, colour->scale);
        }
    }

    stats.drawn++;
    return true;
}

void HwGs::Flush()
{
    selfCopyFor_ = nullptr;
    batchSerial_++;
    if (!batching_ || vertexes_.empty())
    {
        batching_ = false;
        vertexes_.clear();
        batchShapes_.clear();
        return;
    }

    MakeCurrent();
    stats.batches++;
    device_->DrawBatch(key_, vertexes_, batchShapes_);
    vertexes_.clear();
    batchShapes_.clear();
    batching_ = false;
}

bool HwGs::DebugPixel(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32* value)
{
    u32 cls = ClassOf(psm);
    for (auto& t : targets_)
    {
        if (t->bp != bp || t->bw != bw || t->cls != cls || x >= t->width || y >= t->height)
        {
            continue;
        }

        u32 page = Gs::PixelPage(t->psm, x, y, bp, bw) & (Gs::PageCount - 1);
        if (newest_[page] != t.get())
        {
            return false;
        }

        Flush();
        device_->Resolve(*t);
        return device_->ReadTargetPixel(*t, x, y, value);
    }

    return false;
}

void HwGs::SetScale(u32 scale)
{
    scale = std::clamp<u32>(scale, 1, 8);
    if (scale == scale_)
    {
        return;
    }

    MakeCurrent();
    Flush();
    SyncMem(newestMask_);

    while (!targets_.empty())
    {
        DestroyTarget(targets_.back().get());
    }

    device_->DropCopies();
    scale_ = scale;
}

bool HwGs::ShownPicture(NativeGraphics::DisplayTexture* display, Picture* picture)
{
    MakeCurrent();
    Flush();
    device_->PictureDone();
    ProcessWrites();
    for (auto& t : targets_)
    {
        device_->Resolve(*t);
    }

    static const char* dumpTarget = std::getenv("TWIN_HW_DUMP_TARGET");
    if (const char* dump = dumpTarget)
    {
        u32 bp = static_cast<u32>(std::strtoul(dump, nullptr, 16));
        for (auto& t : targets_)
        {
            if (t->bp == bp && !t->depth)
            {
                std::vector<u32> pixels;
                device_->ReadTargetScaled(*t, pixels);
                for (u32& p : pixels)
                {
                    p |= 0xFF000000u;
                }

                const char* folder = std::getenv("TWIN_HW_DUMP_DIR");
                folder = folder != nullptr ? folder : std::getenv("TMPDIR");
                folder = folder != nullptr ? folder : "/tmp";
                char name[1024];
                std::snprintf(name, sizeof(name), "%s/target_%x_%u.png", folder, t->bp, t->cls);
                WritePng(name, pixels.data(), t->width * t->scale, t->height * t->scale);
            }
        }
    }

    const Gs::Gs& gs = gs_;
    // The PCRTC's circuits, as Gs::ReadDisplay reads them
    struct Circuit
    {
        bool enabled;
        u32 width;
        u32 height;
        s32 x;
        s32 y;
        Target* target;
    } circuits[2] = {};
    bool interlaced = gs.smode2.interlaced != 0;
    for (u32 i = 0; i < 2; i++)
    {
        bool enabled = i == 0 ? gs.pmode.en1 != 0 : gs.pmode.en2 != 0;
        const Gs::RegDisplay& d = gs.display[i];
        if (!enabled || (gs.dispfb[i].fbw == 0 && d.dw == 0 && d.dh == 0 && d.magh == 0))
        {
            continue;
        }

        Circuit& c = circuits[i];
        c.enabled = true;
        c.width = static_cast<u32>(d.dw + 1) / static_cast<u32>(d.magh + 1);
        c.height = static_cast<u32>(d.dh + 1) / static_cast<u32>(d.magv + 1);
        if (gs.smode2.ffmd != 0 && interlaced)
        {
            c.height = (c.height + 1) >> 1;
        }

        c.x = static_cast<s32>(d.dx) / static_cast<s32>(d.magh + 1);
        c.y = static_cast<s32>(d.dy) / (interlaced ? 2 : 1);

        // Its buffer's target (made as tall as it's shown), current for the shown rectangle
        const Gs::RegDispfb& fb = gs.dispfb[i];
        u32 cls = ClassOf(fb.psm);
        for (auto& known : targets_)
        {
            if (known->bp == fb.fbp * 32 && known->bw == fb.fbw && known->cls == cls && fb.dbx + c.width <= known->width &&
                known->complete)
            {
                c.target = known.get();
            }
        }

        if (c.target == nullptr)
        {
            return false;
        }

        if (fb.dby + c.height > c.target->height)
        {
            c.target = TargetFor(c.target->bp, c.target->bw, c.target->psm, fb.dby + c.height);
            if (!c.target->complete)
            {
                return false;
            }
        }

        if (std::getenv("TWIN_HW_DISPLAY_TRACE") != nullptr)
        {
            std::fprintf(stderr, "display: circuit %u fb %x/%u/%x db %u,%u size %ux%u at %d,%d -> target %x/%u class %u %ux%u scale %u alp %u mmod %u\n", i,
                         static_cast<u32>(fb.fbp * 32), static_cast<u32>(fb.fbw), static_cast<u32>(fb.psm), static_cast<u32>(fb.dbx),
                         static_cast<u32>(fb.dby), c.width, c.height, c.x, c.y, c.target->bp, c.target->bw, c.target->cls, c.target->width,
                         c.target->height, c.target->scale, static_cast<u32>(gs.pmode.alp), static_cast<u32>(gs.pmode.mmod));
        }

        Rect rect = {static_cast<s32>(fb.dbx), static_cast<s32>(fb.dby), static_cast<s32>(fb.dbx + c.width) - 1,
                     static_cast<s32>(fb.dby + c.height) - 1};
        Gs::PageSet pages;
        RectPages(c.target, rect, pages);
        EnsureValid(c.target, pages);
    }

    if (!circuits[0].enabled && !circuits[1].enabled)
    {
        return false;
    }

    // One circuit, or both showing the same thing: its target's rectangle is the picture
    s32 only = -1;
    if (circuits[0].enabled != circuits[1].enabled)
    {
        only = circuits[0].enabled ? 0 : 1;
    }
    else if (gs.dispfb[0].value == gs.dispfb[1].value && circuits[0].x == circuits[1].x && circuits[0].y == circuits[1].y &&
             circuits[0].width == circuits[1].width && circuits[0].height == circuits[1].height)
    {
        only = 0;
    }

    if (only >= 0)
    {
        const Circuit& c = circuits[only];
        const Gs::RegDispfb& fb = gs.dispfb[only];
        display->texture = c.target->texture;
        display->u0 = static_cast<f32>(fb.dbx) / static_cast<f32>(c.target->width);
        display->v0 = static_cast<f32>(fb.dby) / static_cast<f32>(c.target->height);
        display->u1 = static_cast<f32>(fb.dbx + c.width) / static_cast<f32>(c.target->width);
        display->v1 = static_cast<f32>(fb.dby + c.height) / static_cast<f32>(c.target->height);
        display->width = c.width * c.target->scale;
        display->height = c.height * c.target->scale;
        *picture = {c.target, static_cast<s32>(fb.dbx * c.target->scale), static_cast<s32>(fb.dby * c.target->scale), display->width,
                    display->height};
        return true;
    }

    // Merged: the circuits' union from the nearest one's corner
    s32 originX = std::min(circuits[0].x, circuits[1].x);
    s32 originY = std::min(circuits[0].y, circuits[1].y);
    s32 endX = std::max(circuits[0].x + static_cast<s32>(circuits[0].width), circuits[1].x + static_cast<s32>(circuits[1].width));
    s32 endY = std::max(circuits[0].y + static_cast<s32>(circuits[0].height), circuits[1].y + static_cast<s32>(circuits[1].height));
    const u32 n = std::max(circuits[0].enabled ? circuits[0].target->scale : 1, circuits[1].enabled ? circuits[1].target->scale : 1);
    u32 width = static_cast<u32>(endX - originX) * n;
    u32 height = static_cast<u32>(endY - originY) * n;
    CircuitPicture merged[2] = {};
    for (u32 i = 0; i < 2; i++)
    {
        const Circuit& c = circuits[i];
        CircuitPicture& m = merged[i];
        m.enabled = c.enabled;
        m.target = c.target;
        m.place[0] = static_cast<f32>((c.x - originX) * static_cast<s32>(n));
        m.place[1] = static_cast<f32>((c.y - originY) * static_cast<s32>(n));
        m.place[2] = static_cast<f32>(c.width * n);
        m.place[3] = static_cast<f32>(c.height * n);
        m.corner[0] = static_cast<f32>(gs.dispfb[i].dbx * n);
        m.corner[1] = static_cast<f32>(gs.dispfb[i].dby * n);
        m.size[0] = c.enabled ? static_cast<f32>(c.target->width * n) : 1.0f;
        m.size[1] = c.enabled ? static_cast<f32>(c.target->height * n) : 1.0f;
    }

    display->texture = device_->MergeCircuits(merged, width, height, static_cast<f32>(gs.pmode.alp) / 255.0f, gs.pmode.mmod == 0,
                                              static_cast<u32>(gs.bgcolor & 0xFFFFFF));
    display->u0 = 0.0f;
    display->v0 = 0.0f;
    display->u1 = 1.0f;
    display->v1 = 1.0f;
    display->width = width;
    display->height = height;
    *picture = {nullptr, 0, 0, width, height};
    return true;
}

bool HwGs::ReadDisplay(Gs::DisplayImage& image)
{
    NativeGraphics::DisplayTexture display;
    Picture picture;
    if (!ShownPicture(&display, &picture))
    {
        return false;
    }

    image.width = picture.width;
    image.height = picture.height;
    device_->ReadPicture(picture, image.pixels);
    for (u32& pixel : image.pixels)
    {
        pixel |= 0xFF000000u;
    }

    return true;
}

bool HwGs::CopyDisplay(u32 slot, void* drawnFence, NativeGraphics::ShownTexture* shown)
{
    NativeGraphics::DisplayTexture display;
    Picture picture;
    if (!ShownPicture(&display, &picture))
    {
        return false;
    }

    return device_->CopyPicture(picture, slot, drawnFence, shown);
}
}

bool StartHardwareRenderer(u32 scale, GpuApi api)
{
    if (g_Hw != nullptr)
    {
        g_Hw->SetScale(scale);
        return true;
    }

    auto* hw = new HwGs(*GetHardware().gs, std::clamp<u32>(scale, 1, 8), api == GpuApi::Metal ? MakeMetalDevice : MakeGlDevice);
    if (!hw->Init())
    {
        delete hw;
        return false;
    }

    g_Hw = hw;
    g_HwActive = true;
    return true;
}

void StopHardwareRenderer()
{
    if (g_Hw == nullptr)
    {
        return;
    }

    g_Hw->MakeCurrent();
    g_Hw->Flush();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        Gs::PageSet all;
        all.set();
        g_Hw->BeforeCpuRead(all);
        break;
    }

    delete g_Hw;
    g_Hw = nullptr;
    g_HwActive = false;
}

bool HardwareRendererActive()
{
    return g_HwActive.load(std::memory_order_relaxed);
}

void HardwareSetScale(u32 scale)
{
    if (g_Hw != nullptr)
    {
        g_Hw->SetScale(scale);
    }
}

u32 HardwareScale()
{
    return g_Hw != nullptr ? g_Hw->scale() : 1;
}

bool HardwareDisplayTexture(DisplayTexture* display)
{
    Picture picture;
    return g_Hw != nullptr && g_Hw->ShownPicture(display, &picture);
}

bool HardwareReadDisplay(Gs::DisplayImage& image)
{
    return g_Hw != nullptr && g_Hw->ReadDisplay(image);
}

bool HardwareCopyDisplay(u32 slot, void* drawnFence, ShownTexture* shown)
{
    return g_Hw != nullptr && g_Hw->CopyDisplay(slot, drawnFence, shown);
}

void HardwareSetDither(int mode)
{
    if (g_Hw != nullptr)
    {
        g_Hw->Flush();
    }

    g_DitherMode = std::clamp(mode, 0, 2);
}

void HardwareSetFiltering(int mode)
{
    if (g_Hw != nullptr)
    {
        g_Hw->Flush();
    }

    g_FilterMode = std::clamp(mode, 0, 2);
}

void HardwareSetUpscale(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == g_TextureUpscale)
    {
        return;
    }

    if (g_Hw != nullptr)
    {
        g_Hw->Flush();
    }

    g_TextureUpscale = mode;
    if (g_Hw != nullptr)
    {
        g_Hw->DropUpscaled();
    }
}

HardwareStats GetHardwareStats()
{
    return g_Hw != nullptr ? g_Hw->stats : HardwareStats{};
}
}
