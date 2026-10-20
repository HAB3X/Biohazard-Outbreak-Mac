// The window and the picture's way to it. The GS draws on the GPU (the hardware layer's renderer, hw/hw/hwgs.h) at a multiple of
// the PS2's resolution, on the render thread; at each vblank the render thread copies the shown picture into one of three slots
// for the window, and the main thread draws the newest one over the window when it can. The drawing is a presenter's
// (presenter.h): OpenGL's for the OpenGL renderer and the software GS, Metal's for the Metal renderer. Snapshots read the
// picture back. The software GS (the PS2's own rules at its resolution) is still there: --gs software or $PS2_GS=software, and
// whenever there's no GPU renderer or no render thread.
//
// The PC menus' display settings are kept here and in graphics.txt: the window's size or fullscreen, the GPU's internal
// resolution, the textures' filtering and upscale, anti-aliasing (FXAA on the picture shown, or supersampling: the internal
// resolution 2 or 4 times higher, up to 8 times the PS2's), the CRT filter and v-sync (the presenter's, from the Twinsanity
// port's graphics/display.cpp and ui/crtfilter.cpp). The menus' overlay is drawn over the picture (ui/menus.h).
#include "runtime.h"

#include "presenter.h"
#include "ps2/runtime.h"

#include "../../hw/gs/gs.h"
#include "../../hw/hw/hwgs.h"
#include "../../hw/native.h"
#include "../../hw/png.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <map>
#include <mutex>

namespace NativeGraphics
{
Hardware& GetHardware() { return Ps2::Hw(); }
}

namespace Native
{
std::string SettingsFolder() { return Ps2::g_options.settingsDir; }
}

namespace Ps2
{
extern const Ps2GameInfo* g_game;

namespace
{
SDL_Window* g_window = nullptr;
// The window's drawing (null: no window and no GPU renderer)
std::unique_ptr<Presenter> g_presenter;
// The picture the presenter has in use: none, a slot's copy, or the software GS's pixels
enum class InUse
{
    None,
    Slot,
    Pixels,
};
InUse g_inUse = InUse::None;

// The display settings (graphics.txt): the window's size in points, fullscreen, the textures' filtering (0 the game's, 1
// bilinear, 2 trilinear), anti-aliasing (0 off, 1 FXAA, 2 and 3 supersampling 2x and 4x), the CRT filter, v-sync
int g_windowWidth = 1280, g_windowHeight = 960;
bool g_fullscreen = false;
int g_filtering = 0;
int g_antiAliasing = 0;
bool g_crt = false;
bool g_vsync = true;
// The window's sizes the menus offer
constexpr int WindowSizes[][2] = {{960, 720}, {1280, 960}, {1600, 1200}, {1920, 1440}, {1280, 720}, {1920, 1080}};

// The three copies of the shown picture the render thread hands over: the one the window draws (busy), the newest one made
// (ready) and one to make the next into. A copy's fences: the render thread's after its copy (the window waits for it), the
// window's after drawing it (the render thread waits for it before it copies into it again)
struct Slot
{
    NativeGraphics::ShownTexture shown = {};
    void* drawn = nullptr;
};
std::mutex g_slotLock;
Slot g_slots[3];
int g_ready = -1;
// The copy the window drew last (kept for drawing again under the PC menus while the game is paused: the render thread
// copies into the third)
int g_shown = -1;

// The renderer and scale a game has unless its player chooses others: its own (Ps2GameInfo::gsRenderer, gsScale), else the
// runtime's (OpenGL at 3x)
std::string DefaultRenderer()
{
    if (g_game == nullptr || g_game->gsRenderer == nullptr)
    {
        return "hardware";
    }
    const std::string renderer = g_game->gsRenderer;
    if (renderer != "hardware" && renderer != "metal" && renderer != "software")
    {
        Fatal("Ps2GameInfo::gsRenderer is \"%s\": hardware, metal or software", renderer.c_str());
    }
    return renderer;
}

int DefaultScale() { return g_game != nullptr && g_game->gsScale > 0 ? std::clamp(g_game->gsScale, 1, 8) : 3; }

// graphics.txt's renderer and scale as read (the options may say otherwise for this run: those are written back)
std::string g_fileRenderer;
int g_fileScale = 0;
int g_fileUpscale = 0;
// graphics.txt's form: 2 since a game can have defaults of its own (a file without it is older)
constexpr int GraphicsVersion = 2;

void WriteGraphicsSettings()
{
    std::error_code ec;
    std::filesystem::create_directories(g_options.settingsDir, ec);
    std::ofstream out(g_options.settingsDir + "/graphics.txt");
    out << "# The picture (the PC menus' Display tab changes these). renderer: metal (the GPU draws the PS2's graphics through\n"
           "# Metal), hardware (the same through OpenGL) or software (the PS2's own rules, at its resolution; slower). This game's\n"
           "# own: " << DefaultRenderer() << ". --gs, $PS2_GS override it\n"
           "renderer = " << g_fileRenderer << "\n"
           "# The GPU's resolution, 1-8 times the PS2's (this game's own: " << DefaultScale() << "; --scale, $PS2_GS_SCALE)\n"
           "scale = " << g_fileScale << "\n"
           "# The game's textures made smoother on the GPU: 0 (off), 2 or 4 times their size (--texture-upscale, "
           "$PS2_TEXTURE_UPSCALE)\n"
           "texture_upscale = " << g_fileUpscale << "\n"
           "# The textures' filtering: 0 the game's own, 1 bilinear, 2 trilinear (and anisotropic)\n"
           "filtering = " << g_filtering << "\n"
           "# Anti-aliasing: 0 off, 1 FXAA, 2 or 3 supersampling (the GPU's resolution 2 or 4 times higher, up to 8x)\n"
           "anti_aliasing = " << g_antiAliasing << "\n"
           "# The CRT filter (scanlines): 0 off, 1 on\n"
           "crt = " << (g_crt ? 1 : 0) << "\n"
           "# V-sync: 0 off, 1 on\n"
           "vsync = " << (g_vsync ? 1 : 0) << "\n"
           "# The window: its size in points, and fullscreen (0 or 1; Cmd+F or Alt+Enter switch)\n"
           "window = " << g_windowWidth << "x" << g_windowHeight << "\n"
           "fullscreen = " << (g_fullscreen ? 1 : 0) << "\n"
           "# This file's form\n"
           "version = " << GraphicsVersion << "\n";
}

// A file from before games had defaults of their own wrote the runtime's renderer and scale whatever the player chose (the
// menus have no renderer, and 3x was the default): those are taken as not chosen, so the game's own apply (logged)
void ForgetOldDefaults(std::map<std::string, std::string>& file)
{
    auto version = file.find("version");
    if (file.empty() || (version != file.end() && atoi(version->second.c_str()) >= GraphicsVersion))
    {
        return;
    }
    for (const auto& [key, old] : {std::pair<const char*, const char*>{"renderer", "hardware"}, {"scale", "3"}})
    {
        auto found = file.find(key);
        if (found != file.end() && found->second == old)
        {
            file.erase(found);
            Log("graphics: graphics.txt's %s = %s was the old default: the game's own now", key, old);
        }
    }
}

// The settings folder's graphics.txt (written with the defaults the first time): name = value lines
std::map<std::string, std::string> ReadGraphicsSettings()
{
    std::map<std::string, std::string> values;
    std::string path = g_options.settingsDir + "/graphics.txt";
    std::ifstream in(path);
    if (!in)
    {
        WriteGraphicsSettings();
        return values;
    }
    std::string line;
    while (std::getline(in, line))
    {
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos)
        {
            continue;
        }
        auto trim = [](std::string text) {
            text.erase(0, text.find_first_not_of(" \t\r"));
            text.erase(text.find_last_not_of(" \t\r") + 1);
            return text;
        };
        values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return values;
}

// The option's value, or $NAME's, or graphics.txt's, or the default
std::string Setting(const char* name, const std::map<std::string, std::string>& file, const char* key, const char* otherwise)
{
    const char* value = getenv(name);
    if (value != nullptr && *value != 0)
    {
        return value;
    }
    auto found = file.find(key);
    return found != file.end() ? found->second : otherwise;
}

// The GPU's resolution with supersampling's
int EffectiveScale() { return std::min(8, g_options.gsScale * (g_antiAliasing == 2 ? 2 : g_antiAliasing == 3 ? 4 : 1)); }

// The picture's place in a window of these pixels: the TV's 4:3, letterboxed
SDL_FRect PictureRect(int width, int height)
{
    float scale = std::min(width / 640.0f, height / 480.0f);
    return {(width - 640 * scale) / 2, (height - 480 * scale) / 2, 640 * scale, 480 * scale};
}

// The picture to draw made the presenter's: the newest copy (it becomes the shown one), else the shown one again when asked to
// redraw, else the pixels given, else the pixels drawn last when asked to redraw. False when there's nothing (new) to draw; the
// slot drawn (-1: none)
bool TakePicture(const Gs::DisplayImage* image, bool redraw, int* slot)
{
    *slot = -1;
    {
        std::lock_guard<std::mutex> lock(g_slotLock);
        if (g_ready >= 0)
        {
            g_shown = g_ready;
            g_ready = -1;
            *slot = g_shown;
        }
        else if (redraw && g_shown >= 0 && (image == nullptr || image->width == 0))
        {
            *slot = g_shown;
        }
    }
    if (*slot >= 0)
    {
        g_presenter->UseShown(g_slots[*slot].shown);
        g_inUse = InUse::Slot;
        return true;
    }
    if (image != nullptr && image->width != 0)
    {
        {
            // A picture of the GS's memory now: the copies are older
            std::lock_guard<std::mutex> lock(g_slotLock);
            g_shown = -1;
        }
        g_presenter->UsePixels(*image);
        g_inUse = InUse::Pixels;
        return true;
    }
    return redraw && g_inUse == InUse::Pixels;
}

// A frame of a window of these pixels: the picture's place, its filter (the CRT filter, else FXAA, else none) and its lines
WindowFrame FrameOf(int width, int height, float pixelScale)
{
    WindowFrame frame = {};
    frame.width = width;
    frame.height = height;
    frame.pixelScale = pixelScale;
    frame.place = PictureRect(width, height);
    frame.filter = g_crt ? PictureFilter::Crt : g_antiAliasing == 1 ? PictureFilter::Fxaa : PictureFilter::Plain;
    // The PS2's lines: the picture's pixels less the GPU's scale (the software GS's are the PS2's own)
    u32 pictureWidth = 0, pictureHeight = 0;
    g_presenter->PictureSize(&pictureWidth, &pictureHeight);
    float scale = g_inUse == InUse::Pixels ? 1.0f : static_cast<float>(NativeGraphics::HardwareScale());
    frame.pictureLines[0] = static_cast<float>(pictureWidth) / scale;
    frame.pictureLines[1] = static_cast<float>(pictureHeight) / scale;
    return frame;
}

void ApplyWindow()
{
    if (g_window == nullptr || g_options.headless)
    {
        return;
    }
    if (!g_fullscreen)
    {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_SetWindowSize(g_window, g_windowWidth, g_windowHeight);
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    else
    {
        SDL_SetWindowFullscreen(g_window, true);
    }
}

// The GS's settings changed on the GS's side (in their place among its work: from the game's side)
void ApplyRenderer()
{
    int scale = EffectiveScale();
    int filtering = g_filtering;
    int upscale = g_options.textureUpscale == 4 ? 2 : g_options.textureUpscale == 2 ? 1 : 0;
    KernelOnGameSide([=] {
        RenderCall([=] {
            NativeGraphics::HardwareSetScale(static_cast<u32>(scale));
            NativeGraphics::HardwareSetFiltering(filtering);
            NativeGraphics::HardwareSetUpscale(upscale);
        });
    });
}

// The window made for the presenter and its drawing opened (headless without --ui: no window); false when the presenter
// can't draw it
bool OpenWindow(bool hiddenWindow)
{
    if (g_options.headless && !hiddenWindow)
    {
        return g_presenter->Open(nullptr, g_vsync);
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | g_presenter->PrepareWindow() | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (hiddenWindow)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    else if (g_fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    g_window = SDL_CreateWindow(WindowTitle().c_str(), hiddenWindow ? 1280 : g_windowWidth, hiddenWindow ? 960 : g_windowHeight, flags);
    return g_window != nullptr && g_presenter->Open(g_window, g_vsync);
}
}

SDL_Window* DisplayOpen()
{
    // The renderer: the GPU unless asked for the software GS
    g_fileRenderer = DefaultRenderer();
    g_fileScale = DefaultScale();
    std::map<std::string, std::string> file = ReadGraphicsSettings();
    const bool old = !file.empty() && file.find("version") == file.end();
    ForgetOldDefaults(file);
    const std::string scaleText = std::to_string(DefaultScale());
    if (g_options.gs < 0)
    {
        const std::string renderer = Setting("PS2_GS", file, "renderer", DefaultRenderer().c_str());
        g_options.gs = renderer == "software" ? 0 : renderer == "metal" ? 2 : 1;
    }
    if (g_options.gsScale < 0)
    {
        g_options.gsScale = atoi(Setting("PS2_GS_SCALE", file, "scale", scaleText.c_str()).c_str());
    }
    g_options.gsScale = std::clamp(g_options.gsScale, 1, 8);
    if (g_options.textureUpscale < 0)
    {
        g_options.textureUpscale = atoi(Setting("PS2_TEXTURE_UPSCALE", file, "texture_upscale", "0").c_str());
    }
    g_options.textureUpscale = g_options.textureUpscale >= 4 ? 4 : g_options.textureUpscale >= 2 ? 2 : 0;
    const std::string fileRenderer = Setting("", file, "renderer", DefaultRenderer().c_str());
    g_fileRenderer = fileRenderer == "software" || fileRenderer == "metal" ? fileRenderer : "hardware";
    g_fileScale = std::clamp(atoi(Setting("", file, "scale", scaleText.c_str()).c_str()), 1, 8);
    g_fileUpscale = atoi(Setting("", file, "texture_upscale", "0").c_str());
    g_filtering = std::clamp(atoi(Setting("", file, "filtering", "0").c_str()), 0, 2);
    g_antiAliasing = std::clamp(atoi(Setting("", file, "anti_aliasing", "0").c_str()), 0, 3);
    g_crt = atoi(Setting("", file, "crt", "0").c_str()) != 0;
    g_vsync = atoi(Setting("", file, "vsync", "1").c_str()) != 0;
    g_fullscreen = atoi(Setting("", file, "fullscreen", "0").c_str()) != 0;
    if (sscanf(Setting("", file, "window", "1280x960").c_str(), "%dx%d", &g_windowWidth, &g_windowHeight) != 2 ||
        g_windowWidth < 320 || g_windowHeight < 240)
    {
        g_windowWidth = 1280;
        g_windowHeight = 960;
    }
    // Settings added since the file was written are written in with their defaults
    if (old || (!file.empty() && file.find("vsync") == file.end()))
    {
        WriteGraphicsSettings();
    }

    // Headless, the menus' scripted pictures (--ui) are drawn in a hidden window's context
    bool hiddenWindow = g_options.headless && !g_options.ui.empty();
    if (g_options.headless && g_options.gs == 0 && !hiddenWindow)
    {
        return nullptr;
    }
    if (g_options.headless)
    {
        // Only a hidden window for the GPU's context: no icon in the Dock, no menu bar
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | (g_options.headless ? 0 : SDL_INIT_GAMEPAD)))
    {
        Log("SDL_Init: %s (running headless, the software GS drawing)", SDL_GetError());
        g_options.headless = true;
        g_options.gs = 0;
        return nullptr;
    }
    // Metal's presenter for the Metal renderer, OpenGL's for the rest; OpenGL's in Metal's place when there's no Apple GPU or
    // Metal's can't draw the window
    if (g_options.gs == 2)
    {
        g_presenter = MakeMetalPresenter();
        if (g_presenter == nullptr || !OpenWindow(hiddenWindow))
        {
            Log("graphics: no Metal presenter: OpenGL draws");
            g_options.gs = 1;
            g_presenter.reset();
            if (g_window != nullptr)
            {
                SDL_DestroyWindow(g_window);
                g_window = nullptr;
            }
        }
    }
    if (g_presenter == nullptr)
    {
        g_presenter = MakeGlPresenter();
        if (!OpenWindow(hiddenWindow))
        {
            Log("graphics: no window: %s", SDL_GetError());
            Fatal("no window to draw in");
        }
    }
    if (g_options.gs != 0 && !g_presenter->OpenRenderer())
    {
        g_options.gs = 0;
    }
    return g_window;
}

void DisplayRenderThreadStart()
{
    if (g_presenter == nullptr)
    {
        return;
    }
    g_presenter->RenderThreadStart();
    if (g_options.gs == 0)
    {
        return;
    }
    // The presenter's own interface's renderer (Metal's for --gs metal, else OpenGL's)
    const NativeGraphics::GpuApi api = g_presenter->RendererApi();
    if (!NativeGraphics::StartHardwareRenderer(static_cast<u32>(EffectiveScale()), api))
    {
        Log("graphics: no %s renderer: the software GS draws", api == NativeGraphics::GpuApi::Metal ? "Metal" : "OpenGL");
        g_options.gs = 0;
        return;
    }
    NativeGraphics::HardwareSetUpscale(g_options.textureUpscale == 4 ? 2 : g_options.textureUpscale == 2 ? 1 : 0);
    NativeGraphics::HardwareSetFiltering(g_filtering);
    Log("graphics: the GS draws on the GPU (%s) at %dx the PS2's resolution%s", g_options.gs == 2 ? "Metal" : "OpenGL", g_options.gsScale,
        g_options.textureUpscale != 0 ? (", textures upscaled " + std::to_string(g_options.textureUpscale) + "x").c_str() : "");
}

void DisplayNoRenderThread()
{
    if (g_options.gs != 0)
    {
        Log("graphics: the hardware renderer needs the render thread: the software GS draws");
        g_options.gs = 0;
    }
}

bool DisplayOnGpu() { return NativeGraphics::HardwareRendererActive(); }

int DisplayCopyShown()
{
    // A slot neither the window's shown one nor the newest one
    int slot = 0;
    void* drawn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_slotLock);
        while (slot == g_shown || slot == g_ready)
        {
            slot++;
        }
        drawn = g_slots[slot].drawn;
        g_slots[slot].drawn = nullptr;
    }
    NativeGraphics::ShownTexture shown = {};
    if (!NativeGraphics::HardwareCopyDisplay(static_cast<u32>(slot), drawn, &shown))
    {
        // (the copy's fence wasn't waited for: kept for the next copy into it)
        std::lock_guard<std::mutex> lock(g_slotLock);
        g_slots[slot].drawn = drawn;
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_slotLock);
    g_slots[slot].shown = shown;
    g_ready = slot;
    return slot;
}

void DisplayDropShown()
{
    std::lock_guard<std::mutex> lock(g_slotLock);
    g_ready = -1;
}

bool DisplayReadShown(Gs::DisplayImage& image)
{
    if (NativeGraphics::HardwareRendererActive() && NativeGraphics::HardwareReadDisplay(image))
    {
        return true;
    }
    Hw().gs->ReadDisplay(image);
    return false;
}

void DisplayPresent(const Gs::DisplayImage* image, bool redraw, SDL_FRect* pictureRect)
{
    if (g_window == nullptr || g_options.headless)
    {
        // A headless run's hidden window (--ui, --control's events) has the picture over all of it
        if (g_window != nullptr && pictureRect != nullptr)
        {
            *pictureRect = {0, 0, 1280, 960};
        }
        return;
    }
    int slot = -1;
    bool picture = TakePicture(image, redraw, &slot);
    if (!picture && !redraw)
    {
        return;
    }
    // The TV's 4:3 in the window, letterboxed; the picture scaled smoothly (the TV's picture has no pixels of its own)
    int width = 0, height = 0, points = 0, pointsHigh = 0;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);
    SDL_GetWindowSize(g_window, &points, &pointsHigh);
    float density = points > 0 ? static_cast<float>(width) / static_cast<float>(points) : 1.0f;
    void* drawn = g_presenter->Present(FrameOf(width, height, density), picture);
    if (slot >= 0 && drawn != nullptr)
    {
        void* old = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_slotLock);
            old = g_slots[slot].drawn;
            g_slots[slot].drawn = drawn;
        }
        if (old != nullptr)
        {
            g_presenter->DropFence(old);
        }
    }
    // The mouse's place over the picture goes by the window's points
    SDL_FRect dst = PictureRect(width, height);
    *pictureRect = {dst.x / density, dst.y / density, dst.w / density, dst.h / density};
}

bool DisplayShot(const std::string& path, const Gs::DisplayImage* image)
{
    if (g_window == nullptr)
    {
        return false;
    }
    // The window's own size (headless, its hidden one's points: a 1280 x 960 window as on a screen of one pixel a point)
    int width = 0, height = 0;
    if (g_options.headless)
    {
        SDL_GetWindowSize(g_window, &width, &height);
    }
    else
    {
        SDL_GetWindowSizeInPixels(g_window, &width, &height);
    }
    int points = 0, pointsHigh = 0;
    SDL_GetWindowSize(g_window, &points, &pointsHigh);
    float density = points > 0 && !g_options.headless ? static_cast<float>(width) / static_cast<float>(points) : 1.0f;
    int slot = -1;
    bool picture = TakePicture(image, true, &slot);
    std::vector<u32> pixels;
    if (!g_presenter->Capture(FrameOf(width, height, density), picture, pixels))
    {
        return false;
    }
    for (u32& pixel : pixels)
    {
        pixel |= 0xFF000000u;
    }
    if (!WritePng(path, pixels.data(), static_cast<u32>(width), static_cast<u32>(height)))
    {
        return false;
    }
    Log("saved %s (%dx%d, the window with the PC menus)", path.c_str(), width, height);
    return true;
}

int DisplayGet(DisplaySetting setting)
{
    switch (setting)
    {
    case DisplaySetting::Fullscreen: return g_fullscreen ? 1 : 0;
    case DisplaySetting::WindowSize:
    {
        if (g_window != nullptr && !g_options.headless && !g_fullscreen)
        {
            SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
        }
        for (int i = 0; i < static_cast<int>(std::size(WindowSizes)); i++)
        {
            if (WindowSizes[i][0] == g_windowWidth && WindowSizes[i][1] == g_windowHeight)
            {
                return i;
            }
        }
        return -1;
    }
    case DisplaySetting::Scale: return g_options.gsScale;
    case DisplaySetting::TextureUpscale: return g_options.textureUpscale == 4 ? 2 : g_options.textureUpscale == 2 ? 1 : 0;
    case DisplaySetting::Filtering: return g_filtering;
    case DisplaySetting::AntiAliasing: return g_antiAliasing;
    case DisplaySetting::Crt: return g_crt ? 1 : 0;
    case DisplaySetting::VSync: return g_vsync ? 1 : 0;
    case DisplaySetting::OnGpu: return g_options.gs != 0 ? 1 : 0;
    }
    return 0;
}

int DisplayDefault(DisplaySetting setting)
{
    switch (setting)
    {
    case DisplaySetting::Scale: return DefaultScale();
    case DisplaySetting::VSync: return 1;
    case DisplaySetting::Fullscreen:
    case DisplaySetting::WindowSize:
    case DisplaySetting::TextureUpscale:
    case DisplaySetting::Filtering:
    case DisplaySetting::AntiAliasing:
    case DisplaySetting::Crt:
    case DisplaySetting::OnGpu: return 0;
    }
    return 0;
}

void DisplaySet(DisplaySetting setting, int value)
{
    switch (setting)
    {
    case DisplaySetting::Fullscreen:
        g_fullscreen = value != 0;
        ApplyWindow();
        break;
    case DisplaySetting::WindowSize:
        if (value >= 0 && value < static_cast<int>(std::size(WindowSizes)))
        {
            g_windowWidth = WindowSizes[value][0];
            g_windowHeight = WindowSizes[value][1];
            g_fullscreen = false;
            ApplyWindow();
        }
        break;
    case DisplaySetting::Scale:
        g_options.gsScale = std::clamp(value, 1, 8);
        g_fileScale = g_options.gsScale;
        ApplyRenderer();
        break;
    case DisplaySetting::TextureUpscale:
        g_options.textureUpscale = value == 2 ? 4 : value == 1 ? 2 : 0;
        g_fileUpscale = g_options.textureUpscale;
        ApplyRenderer();
        break;
    case DisplaySetting::Filtering:
        g_filtering = std::clamp(value, 0, 2);
        ApplyRenderer();
        break;
    case DisplaySetting::AntiAliasing:
        g_antiAliasing = std::clamp(value, 0, 3);
        ApplyRenderer();
        break;
    case DisplaySetting::Crt: g_crt = value != 0; break;
    case DisplaySetting::VSync:
        g_vsync = value != 0;
        if (g_presenter != nullptr)
        {
            g_presenter->SetVsync(g_vsync);
        }
        break;
    case DisplaySetting::OnGpu: return;
    }
    WriteGraphicsSettings();
}

std::string DisplayWindowSizeText(int choice)
{
    int w = g_windowWidth, h = g_windowHeight;
    if (choice >= 0 && choice < static_cast<int>(std::size(WindowSizes)))
    {
        w = WindowSizes[choice][0];
        h = WindowSizes[choice][1];
    }
    return std::to_string(w) + " \u00d7 " + std::to_string(h);
}

int DisplayWindowSizeCount() { return static_cast<int>(std::size(WindowSizes)); }

void DisplayWindowPoints(int* width, int* height)
{
    DisplayGet(DisplaySetting::WindowSize);
    *width = g_windowWidth;
    *height = g_windowHeight;
}

void DisplaySetWindowPoints(int width, int height)
{
    g_windowWidth = width;
    g_windowHeight = height;
    ApplyWindow();
    WriteGraphicsSettings();
}

void DisplayClose()
{
    g_presenter.reset();
    if (g_window != nullptr)
    {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}
}
