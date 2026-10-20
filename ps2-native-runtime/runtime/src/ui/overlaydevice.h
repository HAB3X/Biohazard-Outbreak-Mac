// The overlay's frame as GPU work, and the seam to the graphics interface that draws it. overlayrender.cpp turns a frame's
// commands into a list of draws (quads with the shader's constants, a texture, a clip); the presenter's own interface draws the
// list: OpenGL's (overlaygl.cpp) or Metal's (presentmetal.mm). The two shaders are the same program in two languages.
#pragma once

#include "overlayinternal.h"

#include <memory>
#include <vector>

namespace Ps2::Ui
{
struct OverlayVertex
{
    f32 x;
    f32 y;
    f32 u;
    f32 v;
};

// The shader's constants for one draw (the GLSL program's uniforms, the Metal shader's buffer; this layout is the Metal
// shader's: float4s first, then float2s, then scalars)
struct OverlayUniforms
{
    f32 rect[4];
    f32 fillTop[4];
    f32 fillBottom[4];
    f32 borderColour[4];
    f32 tint[4];
    f32 viewport[2];
    f32 offset[2];
    f32 radius;
    f32 border;
    f32 blur;
    f32 scale;
    f32 time;
    // 0 a rounded box, 1 glyphs, 2 the options' backdrop, 3 an image, 4 the fruit (drawn), 5 a PlayStation button (drawn)
    s32 mode;
    s32 pad[2];
};
static_assert(sizeof(OverlayUniforms) == 128, "OverlayUniforms is the Metal shader's layout");

// A texture the device made for the overlay (0: none)
using OverlayTexture = u32;

struct OverlayDraw
{
    OverlayUniforms uniforms;
    OverlayTexture texture;
    // The list's vertexes it draws (triangles)
    u32 first;
    u32 count;
    // Clipped to a rectangle of the window's pixels (left, top, width, height; top row first)
    bool clipped;
    f32 clip[4];
};

struct OverlayDrawList
{
    std::vector<OverlayVertex> vertexes;
    std::vector<OverlayDraw> draws;
    // The window's pixels it's drawn over
    int width = 0;
    int height = 0;
};

struct OverlayResources;

// A graphics interface's side of the overlay. The overlay keeps what it made with a device (its glyph atlases and images) with
// the device
class OverlayDevice
{
public:
    OverlayDevice();
    virtual ~OverlayDevice();
    OverlayDevice(const OverlayDevice&) = delete;
    OverlayDevice& operator=(const OverlayDevice&) = delete;

    // A texture, linearly filtered and clamped at its edges: one channel (glyph coverage, a byte a texel) or RGBA (bytes, red
    // first, straight alpha). 0 when it can't be made
    virtual OverlayTexture MakeTexture(s32 width, s32 height, bool oneChannel, const void* texels) = 0;
    // The list drawn over the framebuffer or render pass the presenter has ready (of the list's pixels), blended over it
    virtual void Draw(const OverlayDrawList& list) = 0;

    // The overlay's own (overlayrender.cpp)
    std::unique_ptr<OverlayResources> resources;
};

// The OpenGL device, made with the window's context current (null: no OpenGL 3.3 or its program)
std::unique_ptr<OverlayDevice> MakeGlOverlayDevice();
}
