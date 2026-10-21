#include "text_map.h"

#include "ps2/iop.h"

#include <cstring>

namespace TextMap
{
namespace
{
constexpr uint32_t LineHeight = 0x6C, X = 0x70, Y = 0x74;
constexpr int MaxDrawn = 384;

// Two frames' strings: the one being drawn and the one before it, by the game's frame count
struct Frame
{
    uint32_t count = ~0u;
    int drawn = 0;
    Drawn text[MaxDrawn];
};

Layout g_layout = {};
Frame g_frames[2];
int g_current = 0;

int32_t Signed(uint32_t address) { return static_cast<int32_t>(Pointer::Word(address)); }

// A string's first bytes (to its end or KeptBytes - 1), zero ended
void Copy(uint32_t text, char* out)
{
    int n = 0;
    for (; n < KeptBytes - 1; n++)
    {
        uint8_t b = Pointer::Byte(text + static_cast<uint32_t>(n));
        out[n] = static_cast<char>(b);
        if (b == 0)
        {
            return;
        }
    }
    out[n] = 0;
}
}

const Layout File2 = {0x2ac540, 0x24adb4};
const Layout File1 = {0x29cd80, 0x23bba4};

void Use(const Layout& layout) { g_layout = layout; }

void Draw(Ps2Cpu* c, void (*draw)(Ps2Cpu*))
{
    const uint32_t text = c->r[4].uw[0];
    const uint32_t font = g_layout.font;
    const float x0 = static_cast<float>(Signed(font + X)), y0 = static_cast<float>(Signed(font + Y));
    draw(c);
    const float x1 = static_cast<float>(Signed(font + X)), y1 = static_cast<float>(Signed(font + Y));
    const float line = static_cast<float>(Signed(font + LineHeight));
    const uint32_t count = Pointer::Word(g_layout.frameCount);
    if (g_frames[g_current].count != count)
    {
        g_current ^= 1;
        g_frames[g_current].count = count;
        g_frames[g_current].drawn = 0;
    }
    Frame& frame = g_frames[g_current];
    if (frame.drawn >= MaxDrawn)
    {
        if (frame.drawn == MaxDrawn)
        {
            Ps2::Log("text map: more than %d strings in a frame: the rest aren't kept", MaxDrawn);
            frame.drawn++;
        }
        return;
    }
    // A string of several lines ends on a later line: its width is unknown (the font's), so it spans the picture's
    Pointer::Rect place = y1 > y0 ? Pointer::Rect{0, y0, 640, y1 + line} : Pointer::Rect{x0, y0, x1 > x0 ? x1 : x0 + 1, y0 + line};
    Drawn& d = frame.text[frame.drawn++];
    d.text = text;
    d.place = place;
    Copy(text, d.bytes);
}

int Last(const Drawn*& drawn)
{
    // The frame before the one counted now (that one may be part drawn): the other buffer once this frame has drawn text,
    // the current one before it has
    const uint32_t count = Pointer::Word(g_layout.frameCount);
    const Frame& last = g_frames[g_current].count == count ? g_frames[g_current ^ 1] : g_frames[g_current];
    if (last.count != count - 1)
    {
        drawn = nullptr;
        return 0; // no text in the last frame (the buffers are older)
    }
    drawn = last.text;
    return last.drawn < MaxDrawn ? last.drawn : MaxDrawn;
}

bool FindBytes(const char* bytes, Pointer::Rect& place)
{
    const Drawn* drawn;
    int count = Last(drawn);
    for (int n = 0; n < count; n++)
    {
        if (std::strcmp(drawn[n].bytes, bytes) == 0)
        {
            place = drawn[n].place;
            return true;
        }
    }
    return false;
}

bool FindYesNo(Pointer::Rect& yes, Pointer::Rect& no)
{
    static const char* const Words[][2] = {{"Yes", "No"}, {"YES", "NO"}, {"\x82\xcd\x82\xa2", "\x82\xa2\x82\xa2\x82\xa6"}};
    for (const auto& words : Words)
    {
        if (FindBytes(words[0], yes) && FindBytes(words[1], no))
        {
            return true;
        }
    }
    // One string "Yes/No" (File 1's pause: the game draws the chosen answer's bytes in red as a second string, which with
    // the English words splits the line elsewhere): the line from the first piece to the end of the pieces after it on the
    // same line, the answers its characters' share either side of the slash
    const Drawn* drawn;
    int count = Last(drawn);
    for (int n = 0; n < count; n++)
    {
        const char* text = drawn[n].bytes;
        if (std::strncmp(text, "Yes/", 4) != 0 && std::strncmp(text, "YES/", 4) != 0)
        {
            continue;
        }
        Pointer::Rect line = drawn[n].place;
        int characters = static_cast<int>(std::strlen(text));
        for (int k = n + 1; k < count && drawn[k].place.y0 == line.y0 && drawn[k].place.x0 == line.x1; k++)
        {
            line.x1 = drawn[k].place.x1;
            characters += static_cast<int>(std::strlen(drawn[k].bytes));
        }
        float width = (line.x1 - line.x0) / static_cast<float>(characters);
        yes = {line.x0, line.y0, line.x0 + 3 * width, line.y1};
        no = {line.x0 + 4 * width, line.y0, line.x1, line.y1};
        return true;
    }
    return false;
}

bool Find(uint32_t text, Pointer::Rect& place)
{
    const Drawn* drawn;
    int count = Last(drawn);
    char want[KeptBytes];
    Copy(text, want);
    for (int n = 0; n < count; n++)
    {
        if (drawn[n].text == text || std::strcmp(drawn[n].bytes, want) == 0)
        {
            place = drawn[n].place;
            return true;
        }
    }
    return false;
}
}
