// Widescreen, anamorphic (gs.h's SetWidescreen): which primitives are the game's 2D, and their squeeze into the middle 4:3 of
// the 16:9 picture (ps2-native-runtime's addition)

#include "drawstate.h"

#include <algorithm>

namespace Gs
{
namespace
{
// 4:3 in the middle of 16:9: three quarters of the width
constexpr s32 SqueezeNumerator = 3;
constexpr s32 SqueezeDenominator = 4;

s32 FloorDivide(s32 a, s32 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// A value put first in a list of the newest (the oldest dropped), or taken out of it
template <size_t N>
void PutFirst(std::array<u32, N>& list, u32 value)
{
    auto at = std::find(list.begin(), list.end(), value);
    if (at == list.begin())
    {
        return;
    }
    std::rotate(list.begin(), at == list.end() ? list.end() - 1 : at, at == list.end() ? list.end() : at + 1);
    list[0] = value;
}

// A primitive's texture is a picture drawn at about its own size: not repeated past its width, nor stretched to four times it
// and more (a gradient or a band made from a few texels). For a primitive spanning the picture's whole width, a picture (a
// movie's frame, a menu's background) keeps its shape in the middle; a pattern stays as wide as the 16:9 picture
bool PictureTexture(const Gs::DrawState& state, const Vertex* vertexes, u32 count)
{
    const RegTex0& tex0 = state.context->tex0;
    const float textureWidth = static_cast<float>(1u << tex0.tw);
    float u0 = 1e30f, u1 = -1e30f;
    s32 x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF;
    for (u32 i = 0; i < count; i++)
    {
        float u = 0.0f;
        if (state.uv)
        {
            u = static_cast<float>(vertexes[i].u) / 16.0f;
        }
        else if (vertexes[i].q != 0.0f)
        {
            u = vertexes[i].s / vertexes[i].q * textureWidth;
        }
        else
        {
            return false;
        }
        u0 = std::min(u0, u);
        u1 = std::max(u1, u);
        x0 = std::min(x0, static_cast<s32>(vertexes[i].x));
        x1 = std::max(x1, static_cast<s32>(vertexes[i].x));
    }
    const float texels = u1 - u0;
    const float pixels = static_cast<float>(x1 - x0) / 16.0f;
    return texels <= textureWidth + 1.0f && texels * 4.0f >= pixels;
}
}

void Gs::NoteWidescreenTarget(u32 bp, bool target)
{
    if (target)
    {
        PutFirst(targets_, bp);
        return;
    }
    std::replace(targets_.begin(), targets_.end(), bp, ~0u);
}

bool Gs::SqueezeForWidescreen(DrawState& state, const Vertex* vertexes, u32 count, Vertex* squeezed)
{
    // The circuit shown (the second's buffer when both are: the first is blended over it at the same place)
    const int circuit = pmode.en2 != 0 ? 1 : pmode.en1 != 0 ? 0 : -1;
    if (circuit < 0)
    {
        return false;
    }
    const RegDispfb& shown = dispfb[circuit];
    PutFirst(shownBuffers_, static_cast<u32>(shown.fbp) * 32);
    const bool texturedFromTarget = state.textured &&
        std::find(targets_.begin(), targets_.end(), static_cast<u32>(state.context->tex0.tbp0)) != targets_.end();
    if (state.frameWritten)
    {
        NoteWidescreenTarget(state.fbp, true);
    }
    // The 2D: the EE's packets drawn into a buffer the PCRTC shows (double buffered: the one drawn isn't shown yet)
    if (sourcePath == 0 || !state.frameWritten || state.fbw != shown.fbw ||
        std::find(shownBuffers_.begin(), shownBuffers_.end(), state.fbp) == shownBuffers_.end())
    {
        return false;
    }

    // The picture's middle in the buffer, in pixels and in the primitive's coordinates (12.4, before XYOFFSET)
    const RegDisplay& area = display[circuit];
    const s32 width = static_cast<s32>((area.dw + 1) / (area.magh + 1));
    const s32 middle = static_cast<s32>(shown.dbx) + width / 2;
    const s32 middleAt = state.offsetX + middle * 16;
    const s32 height = static_cast<s32>((area.dh + 1) / (area.magv + 1));
    const s32 topAt = state.offsetY + static_cast<s32>(shown.dby) * 16;
    s32 x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
    for (u32 i = 0; i < count; i++)
    {
        x0 = std::min(x0, static_cast<s32>(vertexes[i].x));
        x1 = std::max(x1, static_cast<s32>(vertexes[i].x));
        y0 = std::min(y0, static_cast<s32>(vertexes[i].y));
        y1 = std::max(y1, static_cast<s32>(vertexes[i].y));
    }
    // Spanning the picture's width (to a pixel): a clear, a fade or a bar, a pattern or a copy of the frame stay as wide as the
    // 16:9 picture; a picture keeps its shape. Untextured and spanning its height: a border at an edge (Biohazard Outbreak's
    // overscan borders), kept at the 16:9 picture's edge
    const bool wholeWidth = x0 <= middleAt - width * 8 + 16 && x1 >= middleAt + width * 8 - 16;
    const bool wholeHeight = y0 <= topAt + 16 && y1 >= topAt + height * 16 - 16;
    if ((wholeWidth && (!state.textured || texturedFromTarget || !PictureTexture(state, vertexes, count))) ||
        (wholeHeight && !state.textured))
    {
        return false;
    }

    for (u32 i = 0; i < count; i++)
    {
        squeezed[i] = vertexes[i];
        const s32 x = static_cast<s32>(vertexes[i].x);
        squeezed[i].x = static_cast<u32>(middleAt + FloorDivide((x - middleAt) * SqueezeNumerator, SqueezeDenominator));
    }
    // The scissor too (inclusive pixels): what it cut off at the picture's edges is cut off at the middle 4:3's
    state.scissorLeft = middle + FloorDivide((state.scissorLeft - middle) * SqueezeNumerator + SqueezeDenominator - 1,
                                             SqueezeDenominator);
    state.scissorRight = middle + FloorDivide((state.scissorRight + 1 - middle) * SqueezeNumerator, SqueezeDenominator) - 1;
    return true;
}
}
