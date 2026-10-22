// The window's drawing on one graphics interface: the seam between display.cpp (the window, the settings, the hand-over's three
// slots, which picture is drawn and where) and OpenGL's presenter (presentgl.cpp) or Metal's (presentmetal.mm). A presenter
// draws the picture (plain, FXAA or the CRT filter) letterboxed into the window or into an image, the PC menus' overlay over
// it, and keeps what the GPU renderer of its interface needs from the main thread. Main thread only, but where said.
#pragma once

#include "runtime.h"

#include "../../hw/hw/hwgs.h"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace Ps2
{
namespace Ui
{
class OverlayDevice;
}

// The picture's way to the window
enum class PictureFilter
{
    Plain,
    Fxaa,
    Crt,
};

// The picture's scaling into its place: smooth (bilinear), sharp (whole pixels, bilinear only at their edges), pixelated
enum class PictureScaling
{
    Smooth,
    Sharp,
    Pixelated,
};

// What's done to the picture's colours after its filter: brightness (a factor), gamma (the power the colours are raised to:
// 1/gamma), a colour filter for colour blindness (daltonization: 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia) and Reduce
// Flashing: a pixel brightens by at most flashStep a frame shown (1: no limit), so a flash fades in instead of striking
struct PictureLook
{
    float brightness = 1.0f;
    float power = 1.0f;
    int colourFilter = 0;
    float flashStep = 1.0f;

    bool Plain() const { return brightness == 1.0f && power == 1.0f && colourFilter == 0 && flashStep >= 1.0f; }
};

// A frame of the window: its pixels and the window's pixels a point, the picture's place in them, its filter, scaling and look,
// and the picture's size in the PS2's pixels (the CRT filter's lines)
struct WindowFrame
{
    int width;
    int height;
    float pixelScale;
    SDL_FRect place;
    PictureFilter filter;
    PictureScaling scaling;
    PictureLook look;
    float pictureLines[2];
};

class Presenter
{
public:
    virtual ~Presenter() = default;

    // The GPU renderer that draws for this presenter's interface
    virtual NativeGraphics::GpuApi RendererApi() const = 0;
    // Before the window is made: what the interface needs set, and the window's flags for it
    virtual SDL_WindowFlags PrepareWindow() = 0;
    // Ready on the window (null: headless, nothing shown): false when the interface can't draw it
    virtual bool Open(SDL_Window* window, bool vsync) = 0;
    // What the GPU renderer needs from the main thread (OpenGL: the render thread's context, sharing the window's); false: it
    // can't have it. And on the render thread at its start (its context made current there)
    virtual bool OpenRenderer() = 0;
    virtual void RenderThreadStart() = 0;
    virtual void SetVsync(bool on) = 0;

    // The picture drawn from now on: the render thread's copy in a slot (its fence waited for and let go of), or the software
    // GS's pixels
    virtual void UseShown(NativeGraphics::ShownTexture& shown) = 0;
    virtual void UsePixels(const Gs::DisplayImage& image) = 0;
    // The picture in use's size in pixels
    virtual void PictureSize(u32* width, u32* height) const = 0;
    // A frame drawn (with the picture in use, or none: black) into the window and shown; a fence for the render thread to wait
    // for before it copies into the slot drawn again (null: none needed)
    virtual void* Present(const WindowFrame& frame, bool picture) = 0;
    // A fence Present made that the render thread didn't wait for, let go of
    virtual void DropFence(void* fence) = 0;
    // A frame drawn into an image of its pixels (RGBA, top row first)
    virtual bool Capture(const WindowFrame& frame, bool picture, std::vector<u32>& pixels) = 0;
    // The PC menus' overlay drawn with this interface (null: it couldn't be made, logged)
    virtual Ui::OverlayDevice* Overlay() = 0;
};

// OpenGL's (always there: a missing OpenGL 3.3 shows as Open or OpenRenderer failing), Metal's (null without an Apple GPU)
std::unique_ptr<Presenter> MakeGlPresenter();
std::unique_ptr<Presenter> MakeMetalPresenter();
}
