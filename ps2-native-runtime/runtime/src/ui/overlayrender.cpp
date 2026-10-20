// The overlay's drawing, whatever the graphics interface (overlaydevice.h): the frame's commands in the 1280 x 720 reference
// space scaled to the window, made into a list of draws for the presenter's device. One shader: rounded boxes as a signed
// distance (a vertical gradient, a border, a soft edge for shadows), glyphs from atlases baked per font and pixel size
// (stb_truetype), the options' backdrop worked out per pixel (its gradient, the ring target, the Twinsanity spiral), images.
// The shader's constants are kept from draw to draw as one program's uniforms are, and each draw records them all. From the
// Crash Twinsanity native port's ui/overlayrender.cpp (same author): its disc art, character preview and offscreen shots left
// out
#include "overlaydevice.h"

#include "../runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

#include "../../third_party/stb/stb_truetype.h"

namespace Ps2::Ui
{
struct Atlas
{
    OverlayTexture texture = 0;
    s32 size = 0;
    std::vector<stbtt_packedchar> latin;
    std::vector<stbtt_packedchar> punctuation;
    std::vector<stbtt_packedchar> extras;
};

// What the overlay made with a device: the atlases by font and pixel size, the images' textures (0 none: drawn instead)
struct OverlayResources
{
    std::map<std::pair<u32, s32>, Atlas> atlases;
    OverlayTexture images[static_cast<u32>(OverlayImage::Count)] = {};
    bool imagesTried[static_cast<u32>(OverlayImage::Count)] = {};
};

OverlayDevice::OverlayDevice() : resources(std::make_unique<OverlayResources>()) {}
OverlayDevice::~OverlayDevice() = default;

namespace
{
// The codepoints baked: Latin-1's, and the quotes, the ellipsis and the angle quotes
constexpr int LatinFirst = 32;
constexpr int LatinCount = 224;
constexpr int PunctuationFirst = 0x2018;
constexpr int PunctuationCount = 0x203B - 0x2018;
// And Windows-1252's others (the game's texts' codes 0x80 to 0x9F outside those)
int ExtraCodepoints[] = {0x0152, 0x0153, 0x0160, 0x0161, 0x0178, 0x017D, 0x017E, 0x0192, 0x02C6, 0x02DC, 0x20AC, 0x2122};
constexpr int ExtraCount = static_cast<int>(sizeof(ExtraCodepoints) / sizeof(ExtraCodepoints[0]));

// A frame being made into draws: the device, the shader's constants as they stand, the clip as it stands, the window's mapping
struct Builder
{
    OverlayDevice& device;
    OverlayDrawList& list;
    OverlayUniforms uniforms = {};
    OverlayTexture texture = 0;
    bool clipped = false;
    f32 clip[4] = {};
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 scale = 1.0f;
    f32 offsetX = 0.0f;
    f32 offsetY = 0.0f;
};

void SetColour(f32* out, const Rgba32& c)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

void AddQuad(std::vector<OverlayVertex>& vertexes, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0 = 0, f32 v0 = 0, f32 u1 = 1,
             f32 v1 = 1)
{
    vertexes.push_back({x0, y0, u0, v0});
    vertexes.push_back({x1, y0, u1, v0});
    vertexes.push_back({x0, y1, u0, v1});
    vertexes.push_back({x1, y0, u1, v0});
    vertexes.push_back({x1, y1, u1, v1});
    vertexes.push_back({x0, y1, u0, v1});
}

// The vertexes added since first drawn with the constants, texture and clip as they stand
void Emit(Builder& b, size_t first)
{
    if (b.list.vertexes.size() == first)
    {
        return;
    }

    OverlayDraw draw;
    draw.uniforms = b.uniforms;
    draw.texture = b.texture;
    draw.first = static_cast<u32>(first);
    draw.count = static_cast<u32>(b.list.vertexes.size() - first);
    draw.clipped = b.clipped;
    std::memcpy(draw.clip, b.clip, sizeof(draw.clip));
    b.list.draws.push_back(draw);
}

// A rounded box in pixels
void DrawBox(Builder& b, f32 x, f32 y, f32 w, f32 h, f32 radius, const Rgba32& top, const Rgba32& bottom, f32 border,
             const Rgba32& borderColour, f32 blur)
{
    OverlayUniforms& u = b.uniforms;
    u.mode = 0;
    u.rect[0] = x;
    u.rect[1] = y;
    u.rect[2] = w;
    u.rect[3] = h;
    u.radius = std::min(radius, std::min(w, h) * 0.5f);
    SetColour(u.fillTop, top);
    SetColour(u.fillBottom, bottom);
    u.border = border;
    SetColour(u.borderColour, borderColour);
    u.blur = blur;
    f32 grow = blur * 2.0f + 1.0f;
    size_t first = b.list.vertexes.size();
    AddQuad(b.list.vertexes, x - grow, y - grow, x + w + grow, y + h + grow);
    Emit(b, first);
}

Atlas& AtlasFor(Builder& b, OverlayFont face, s32 pixelSize)
{
    Atlas& atlas = b.device.resources->atlases[{static_cast<u32>(face), pixelSize}];
    if (atlas.texture != 0)
    {
        return atlas;
    }

    const stbtt_fontinfo* info = OverlayFontInfo(face);
    if (info == nullptr)
    {
        return atlas;
    }

    for (s32 size = 512; size <= 4096; size *= 2)
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(size) * size);
        stbtt_pack_context pack;
        if (!stbtt_PackBegin(&pack, pixels.data(), size, size, 0, 1, nullptr))
        {
            continue;
        }

        stbtt_PackSetOversampling(&pack, 2, 2);
        atlas.latin.assign(LatinCount, {});
        atlas.punctuation.assign(PunctuationCount, {});
        atlas.extras.assign(ExtraCount, {});
        stbtt_pack_range ranges[3] = {};
        f32 scale = stbtt_ScaleForMappingEmToPixels(info, static_cast<f32>(pixelSize));
        int ascent;
        int descent;
        int gap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &gap);
        // PackFontRanges takes a pixel height (ascent to descent): the em's size given as that
        f32 height = scale * static_cast<f32>(ascent - descent);
        ranges[0] = {height, LatinFirst, nullptr, LatinCount, atlas.latin.data(), 0, 0};
        ranges[1] = {height, PunctuationFirst, nullptr, PunctuationCount, atlas.punctuation.data(), 0, 0};
        ranges[2] = {height, 0, ExtraCodepoints, ExtraCount, atlas.extras.data(), 0, 0};
        const unsigned char* data = info->data;
        bool packed = stbtt_PackFontRanges(&pack, data, info->fontstart, ranges, 3) != 0;
        stbtt_PackEnd(&pack);
        if (!packed)
        {
            continue;
        }

        atlas.texture = b.device.MakeTexture(size, size, true, pixels.data());
        atlas.size = size;
        break;
    }

    return atlas;
}

// A run of text's glyphs in pixels at a place (its left, its baseline), drawn in a colour
void DrawGlyphs(Builder& b, const std::u32string& text, Atlas& atlas, f32 left, f32 baseline, f32 letterSpacing, const Rgba32& colour)
{
    if (atlas.texture == 0)
    {
        return;
    }

    size_t first = b.list.vertexes.size();
    f32 x = left;
    f32 y = baseline;
    for (char32_t c : text)
    {
        const stbtt_packedchar* chars = nullptr;
        int index = 0;
        if (c >= static_cast<char32_t>(LatinFirst) && c < static_cast<char32_t>(LatinFirst + LatinCount))
        {
            chars = atlas.latin.data();
            index = static_cast<int>(c) - LatinFirst;
        }
        else if (c >= static_cast<char32_t>(PunctuationFirst) && c < static_cast<char32_t>(PunctuationFirst + PunctuationCount))
        {
            chars = atlas.punctuation.data();
            index = static_cast<int>(c) - PunctuationFirst;
        }
        else
        {
            int* end = ExtraCodepoints + ExtraCount;
            int* found = std::find(ExtraCodepoints, end, static_cast<int>(c));
            if (found == end)
            {
                continue;
            }
            chars = atlas.extras.data();
            index = static_cast<int>(found - ExtraCodepoints);
        }

        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(chars, atlas.size, atlas.size, index, &x, &y, &q, 0);
        AddQuad(b.list.vertexes, q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1);
        x += letterSpacing;
    }

    b.uniforms.mode = 1;
    SetColour(b.uniforms.tint, colour);
    b.texture = atlas.texture;
    Emit(b, first);
}

void DrawText(Builder& b, const OverlayCommand& command)
{
    const TextStyle& style = command.textStyle;
    f32 pixelSize = style.size * b.scale;
    Atlas& atlas = AtlasFor(b, style.font, static_cast<s32>(std::lround(pixelSize)));
    f32 ascent;
    f32 descent;
    f32 gap;
    OverlayFontMetrics(style.font, style.size, &ascent, &descent, &gap);
    // The baseline from the line's middle (a CSS line box of the normal height)
    f32 left = b.offsetX + command.x * b.scale;
    f32 baseline = b.offsetY + (command.y + (ascent + descent) * 0.5f) * b.scale;
    f32 spacing = style.letterSpacing * b.scale;
    auto layer = [&](f32 dx, f32 dy, const Rgba32& colour, bool outlined)
    {
        if (outlined && style.outline > 0.0f)
        {
            // The outline's visible half all round, then the fill over it
            f32 o = style.outline * 0.5f * b.scale;
            for (s32 i = 0; i < 12; i++)
            {
                f32 a = static_cast<f32>(i) * 6.28318530718f / 12.0f;
                DrawGlyphs(b, command.text, atlas, left + dx + std::cos(a) * o, baseline + dy + std::sin(a) * o, spacing,
                           outlined && dx == 0.0f && dy == 0.0f ? style.outlineColour : colour);
            }
        }

        DrawGlyphs(b, command.text, atlas, left + dx, baseline + dy, spacing, colour);
    };

    if (style.dropColour.a > 0.0f && (style.dropX != 0.0f || style.dropY != 0.0f))
    {
        layer(style.dropX * b.scale, style.dropY * b.scale, style.dropColour, true);
    }

    layer(0.0f, 0.0f, style.colour, true);
}

void DrawBoxCommand(Builder& b, const OverlayCommand& command)
{
    const BoxStyle& s = command.box;
    f32 x = b.offsetX + command.x * b.scale;
    f32 y = b.offsetY + command.y * b.scale;
    f32 w = command.width * b.scale;
    f32 h = command.height * b.scale;
    f32 radius = s.radius * b.scale;
    Rgba32 none = {0, 0, 0, -1};
    if (s.shadowColour.a > 0.0f)
    {
        f32 blur = s.shadowBlur * 0.5f * b.scale;
        DrawBox(b, x, y + s.shadowOffset * b.scale, w, h, radius, s.shadowColour, none, 0.0f, none, std::max(blur, 0.5f));
    }

    if (s.lip > 0.0f && s.lipColour.a > 0.0f)
    {
        DrawBox(b, x, y + s.lip * b.scale, w, h, radius, s.lipColour, none, 0.0f, none, 0.0f);
    }

    DrawBox(b, x, y, w, h, radius, s.fill, s.fillBottom, s.border * b.scale, s.borderColour, 0.0f);
}

OverlayTexture ImageTexture(Builder& b, OverlayImage image)
{
    OverlayResources& r = *b.device.resources;
    u32 index = static_cast<u32>(image);
    if (r.imagesTried[index])
    {
        return r.images[index];
    }

    r.imagesTried[index] = true;
    std::vector<u32> pixels;
    s32 width = 0;
    s32 height = 0;
    if (!MouseArt(image, &pixels, &width, &height))
    {
        return 0;
    }

    r.images[index] = b.device.MakeTexture(width, height, false, pixels.data());
    return r.images[index];
}

void DrawImage(Builder& b, const OverlayCommand& command)
{
    OverlayTexture texture = ImageTexture(b, command.image);
    f32 x = b.offsetX + command.x * b.scale;
    f32 y = b.offsetY + command.y * b.scale;
    size_t first = b.list.vertexes.size();
    AddQuad(b.list.vertexes, x, y, x + command.width * b.scale, y + command.height * b.scale);
    s32 mode = 3;
    if (texture == 0)
    {
        // Drawn: the fruit, or a PlayStation button's shape (5 + its shape)
        u32 shape = static_cast<u32>(command.image) - static_cast<u32>(OverlayImage::PlayStationCross);
        mode = command.image == OverlayImage::Cursor ? 4 : shape < 4 ? 5 : 4;
        b.uniforms.radius = static_cast<f32>(shape);
    }

    b.uniforms.mode = mode;
    SetColour(b.uniforms.tint, {1, 1, 1, command.alpha});
    b.texture = texture;
    Emit(b, first);
}

// A frame's commands made into draws over a window of a size
void BuildFrame(Builder& b, const std::vector<OverlayCommand>& commands)
{
    for (const OverlayCommand& command : commands)
    {
        switch (command.kind)
        {
        case CommandKind::Backdrop:
        {
            b.uniforms.mode = 2;
            b.uniforms.scale = b.scale;
            b.uniforms.offset[0] = b.offsetX;
            b.uniforms.offset[1] = b.offsetY;
            b.uniforms.time = command.seconds;
            size_t first = b.list.vertexes.size();
            AddQuad(b.list.vertexes, 0.0f, 0.0f, b.width, b.height);
            Emit(b, first);
            break;
        }
        case CommandKind::Box:
            DrawBoxCommand(b, command);
            break;
        case CommandKind::Text:
            DrawText(b, command);
            break;
        case CommandKind::Image:
            DrawImage(b, command);
            break;
        case CommandKind::Clip:
            b.clipped = true;
            b.clip[0] = b.offsetX + command.x * b.scale;
            b.clip[1] = b.offsetY + command.y * b.scale;
            b.clip[2] = command.width * b.scale;
            b.clip[3] = command.height * b.scale;
            break;
        case CommandKind::NoClip:
            b.clipped = false;
            break;
        }
    }
}
}

void DrawOverlay(OverlayFrame& frame, OverlayDevice& device, int pixelWidth, int pixelHeight, float pixelScale)
{
    NoteWindow(pixelWidth, pixelHeight, pixelScale);
    const std::vector<OverlayCommand>& commands = frame.Commands();
    if (commands.empty())
    {
        return;
    }

    static OverlayDrawList list;
    list.vertexes.clear();
    list.draws.clear();
    list.width = pixelWidth;
    list.height = pixelHeight;
    Builder b{device, list};
    b.width = static_cast<f32>(pixelWidth);
    b.height = static_cast<f32>(pixelHeight);
    ReferenceMapping(b.width, b.height, &b.scale, &b.offsetX, &b.offsetY);
    b.uniforms.viewport[0] = b.width;
    b.uniforms.viewport[1] = b.height;
    BuildFrame(b, commands);
    device.Draw(list);
}
}
