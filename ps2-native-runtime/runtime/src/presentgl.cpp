// The window drawn by OpenGL (presenter.h): the window's context, and the render thread's context the OpenGL renderer draws
// with (made here, on the main thread: SDL's windows are the main thread's, sharing the window's textures, with a hidden window
// of its own to be current with). The render thread copies the shown picture into one of three textures and fences it; the
// window waits for the fence, draws the copy at 4:3 letterboxed (plain, FXAA or the CRT filter) with the PC menus over it, and
// fences its drawing so the render thread doesn't copy into a texture still being drawn (the Crash Twinsanity native port's
// hand-over, graphics/display.cpp there; FXAA and the CRT filter from its graphics/display.cpp and ui/crtfilter.cpp).
#include "presenter.h"

#include "ui/menus.h"
#include "ui/overlaydevice.h"

#include "../../hw/gs/gs.h"
#include "../../hw/hw/gl.h"

#include <algorithm>

namespace NativeGraphics
{
bool PresenterContext(SDL_Window** window, SDL_GLContext* context);
bool RenderContext(SDL_Window** window, SDL_GLContext* context);
}

namespace Ps2
{
namespace
{
using namespace Hw;

constexpr const char* VertexSource = R"(#version 330 core
out vec2 place;
uniform vec4 uvRect;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    place = mix(uvRect.xy, uvRect.zw, vec2(corner.x, 1.0 - corner.y));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* FragmentSource = R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
void main()
{
    colour = vec4(texture(picture, place).rgb, 1.0);
}
)";

// FXAA (a short version of Timothy Lottes' FXAA 3 console algorithm): edges found by luma contrast with their neighbours and
// blended along them, on the shown picture (the Twinsanity port's)
constexpr const char* FxaaSource = R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
uniform vec2 texel;
float Luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}
void main()
{
    vec3 m = texture(picture, place).rgb;
    vec3 nw = texture(picture, place + vec2(-1.0, -1.0) * texel).rgb;
    vec3 ne = texture(picture, place + vec2(1.0, -1.0) * texel).rgb;
    vec3 sw = texture(picture, place + vec2(-1.0, 1.0) * texel).rgb;
    vec3 se = texture(picture, place + vec2(1.0, 1.0) * texel).rgb;
    float lm = Luma(m);
    float lnw = Luma(nw);
    float lne = Luma(ne);
    float lsw = Luma(sw);
    float lse = Luma(se);
    float lowest = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float highest = max(lm, max(max(lnw, lne), max(lsw, lse)));
    vec2 direction = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduce = max((lnw + lne + lsw + lse) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float smallest = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * smallest, vec2(-8.0), vec2(8.0)) * texel;
    vec3 a = 0.5 * (texture(picture, place + direction * (1.0 / 3.0 - 0.5)).rgb + texture(picture, place + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(picture, place - direction * 0.5).rgb + texture(picture, place + direction * 0.5).rgb);
    float lb = Luma(b);
    colour = vec4((lb < lowest || lb > highest) ? a : b, 1.0);
}
)";

// The CRT filter: the picture as a period television showed it, kept subtle (the Twinsanity port's ui/crtfilter.cpp): the
// beam's horizontal softness, a dark gap between the picture's lines (less where the line is bright, as a beam blooms), an
// aperture grille's faint stripes at the screen's own pixels, the brightness the gaps take given back, the corners a touch
// darker. pictureSize is the PS2's lines (the texture's at the internal resolution divided down)
constexpr const char* CrtSource = R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
uniform vec2 pictureSize;
uniform vec2 viewSize;
vec3 Sample(vec2 at)
{
    return texture(picture, at).rgb;
}
void main()
{
    vec2 texel = 1.0 / pictureSize;
    vec3 beam = Sample(place) * 0.5 + Sample(place - vec2(texel.x, 0.0)) * 0.25 + Sample(place + vec2(texel.x, 0.0)) * 0.25;
    float row = fract(place.y * pictureSize.y);
    float brightness = max(max(beam.r, beam.g), beam.b);
    float depth = mix(0.35, 0.12, brightness);
    float line = 1.0 - depth * pow(abs(row - 0.5) * 2.0, 2.0);
    float column = mod(floor(place.x * viewSize.x), 3.0);
    vec3 grille = viewSize.x >= pictureSize.x * 2.0 ? (column == 0.0 ? vec3(1.0, 0.94, 0.94)
                                                     : column == 1.0 ? vec3(0.94, 1.0, 0.94) : vec3(0.94, 0.94, 1.0))
                                                   : vec3(1.0);
    vec2 centred = place * 2.0 - 1.0;
    float vignette = 1.0 - 0.08 * dot(centred * centred, vec2(0.5, 0.5));
    colour = vec4(clamp(beam * line * grille * 1.12 * vignette, 0.0, 1.0), 1.0);
}
)";

void SetGlAttributes()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
}

GLuint Compile(Gl& gl, GLenum kind, const char* source)
{
    GLuint shader = gl.CreateShader(kind);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Log("graphics: the window's shader: %s", log);
    }
    return shader;
}

GLuint Program(Gl& gl, const char* fragmentSource)
{
    GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, fragmentSource);
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    gl.LinkProgram(program);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Log("graphics: a window's program doesn't link");
        return 0;
    }
    gl.UseProgram(program);
    gl.Uniform1i(gl.GetUniformLocation(program, "picture"), 0);
    return program;
}

class GlPresenter : public Presenter
{
public:
    ~GlPresenter() override;
    NativeGraphics::GpuApi RendererApi() const override { return NativeGraphics::GpuApi::OpenGl; }
    SDL_WindowFlags PrepareWindow() override;
    bool Open(SDL_Window* window, bool vsync) override;
    bool OpenRenderer() override;
    void RenderThreadStart() override;
    void SetVsync(bool on) override
    {
        vsync_ = on;
        vsyncSet_ = false;
    }
    void UseShown(NativeGraphics::ShownTexture& shown) override;
    void UsePixels(const Gs::DisplayImage& image) override;
    void PictureSize(u32* width, u32* height) const override
    {
        *width = width_;
        *height = height_;
    }
    void* Present(const WindowFrame& frame, bool picture) override;
    void DropFence(void* fence) override;
    bool Capture(const WindowFrame& frame, bool picture, std::vector<u32>& pixels) override;
    Ui::OverlayDevice* Overlay() override { return overlay_.get(); }

    SDL_Window* window_ = nullptr;
    SDL_GLContext windowContext_ = nullptr;
    // The render thread's context, with a hidden window of its own to be current with
    SDL_Window* renderWindow_ = nullptr;
    SDL_GLContext renderContext_ = nullptr;

private:
    // The picture (and the menus' overlay) drawn into the framebuffer bound, of the frame's pixels
    void Draw(const WindowFrame& frame, bool picture, GLuint framebuffer);

    // The window's drawing: a quad over the viewport from the vertex number alone, rows top first; FXAA's and the CRT filter's
    // programs in its place when they're on
    GLuint program_ = 0;
    GLuint fxaaProgram_ = 0;
    GLuint crtProgram_ = 0;
    GLuint vertexArray_ = 0;
    // The software GS's pictures (and the hardware renderer's when the shown buffer isn't one of its targets) go up into this
    GLuint cpuTexture_ = 0;
    // The picture in use: a slot's copy or the CPU's texture, and its size
    GLuint texture_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
    bool vsync_ = true;
    bool vsyncSet_ = false;
    std::unique_ptr<Ui::OverlayDevice> overlay_;
};

GlPresenter* g_presenter = nullptr;
thread_local bool t_onRenderThread = false;

GlPresenter::~GlPresenter()
{
    g_presenter = nullptr;
}

SDL_WindowFlags GlPresenter::PrepareWindow()
{
    SetGlAttributes();
    return SDL_WINDOW_OPENGL;
}

bool GlPresenter::Open(SDL_Window* window, bool vsync)
{
    g_presenter = this;
    if (window == nullptr)
    {
        return true;
    }

    window_ = window;
    windowContext_ = SDL_GL_CreateContext(window_);
    if (windowContext_ == nullptr)
    {
        Log("graphics: no OpenGL 3.3 context for the window: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, windowContext_);
    if (!LoadGl())
    {
        return false;
    }
    vsync_ = vsync;
    SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
    vsyncSet_ = true;
    Gl& gl = GetGl();
    program_ = Program(gl, FragmentSource);
    if (program_ == 0)
    {
        return false;
    }
    fxaaProgram_ = Program(gl, FxaaSource);
    crtProgram_ = Program(gl, CrtSource);
    overlay_ = Ui::MakeGlOverlayDevice();
    gl.GenVertexArrays(1, &vertexArray_);
    gl.GenTextures(1, &cpuTexture_);
    gl.BindTexture(GL_TEXTURE_2D, cpuTexture_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    return true;
}

bool GlPresenter::OpenRenderer()
{
    SetGlAttributes();
    if (windowContext_ != nullptr)
    {
        SDL_GL_MakeCurrent(window_, windowContext_);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    }
    renderWindow_ = SDL_CreateWindow("graphics", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (renderWindow_ != nullptr)
    {
        renderContext_ = SDL_GL_CreateContext(renderWindow_);
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (renderContext_ == nullptr)
    {
        Log("graphics: no OpenGL 3.3 context for the render thread: %s", SDL_GetError());
        if (renderWindow_ != nullptr)
        {
            SDL_DestroyWindow(renderWindow_);
            renderWindow_ = nullptr;
        }
        return false;
    }
    // Making it made it current here: the window's again (the render thread makes its own current there)
    if (windowContext_ != nullptr)
    {
        SDL_GL_MakeCurrent(window_, windowContext_);
    }
    else
    {
        SDL_GL_MakeCurrent(renderWindow_, nullptr);
    }
    return true;
}

void GlPresenter::RenderThreadStart()
{
    t_onRenderThread = true;
    if (renderContext_ != nullptr)
    {
        SDL_GL_MakeCurrent(renderWindow_, renderContext_);
    }
}

void GlPresenter::UseShown(NativeGraphics::ShownTexture& shown)
{
    Gl& gl = GetGl();
    if (shown.fence != nullptr)
    {
        gl.WaitSync(static_cast<GLsync>(shown.fence), 0, GL_TIMEOUT_IGNORED);
        gl.DeleteSync(static_cast<GLsync>(shown.fence));
        shown.fence = nullptr;
    }
    texture_ = shown.texture;
    width_ = shown.width;
    height_ = shown.height;
}

void GlPresenter::UsePixels(const Gs::DisplayImage& image)
{
    Gl& gl = GetGl();
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, cpuTexture_);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(image.width),
                  static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    texture_ = cpuTexture_;
    width_ = image.width;
    height_ = image.height;
}

void GlPresenter::Draw(const WindowFrame& frame, bool picture, GLuint framebuffer)
{
    Gl& gl = GetGl();
    const SDL_FRect& dst = frame.place;
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_BLEND);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.Viewport(0, 0, frame.width, frame.height);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    if (picture && texture_ != 0)
    {
        gl.Viewport(static_cast<GLint>(dst.x), static_cast<GLint>(frame.height - dst.y - dst.h), static_cast<GLsizei>(dst.w),
                    static_cast<GLsizei>(dst.h));
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, texture_);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        // The CRT filter, else FXAA, else the plain picture (a filter whose program didn't build: the plain picture)
        GLuint program = frame.filter == PictureFilter::Crt && crtProgram_ != 0    ? crtProgram_
                         : frame.filter == PictureFilter::Fxaa && fxaaProgram_ != 0 ? fxaaProgram_
                                                                                    : program_;
        gl.UseProgram(program);
        gl.Uniform4f(gl.GetUniformLocation(program, "uvRect"), 0, 0, 1, 1);
        if (program == fxaaProgram_)
        {
            gl.Uniform2f(gl.GetUniformLocation(program, "texel"), 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
        }
        else if (program == crtProgram_)
        {
            gl.Uniform2f(gl.GetUniformLocation(program, "pictureSize"), frame.pictureLines[0], frame.pictureLines[1]);
            gl.Uniform2f(gl.GetUniformLocation(program, "viewSize"), dst.w, dst.h);
        }
        gl.BindVertexArray(vertexArray_);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    gl.Viewport(0, 0, frame.width, frame.height);
    if (overlay_ != nullptr)
    {
        Ui::MenusDraw(*overlay_, frame.width, frame.height, frame.pixelScale, dst);
    }
}

void* GlPresenter::Present(const WindowFrame& frame, bool picture)
{
    Gl& gl = GetGl();
    if (!vsyncSet_)
    {
        SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
        vsyncSet_ = true;
    }
    Draw(frame, picture, 0);
    void* drawn = nullptr;
    if (picture && texture_ != cpuTexture_)
    {
        drawn = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl.Flush();
    }
    SDL_GL_SwapWindow(window_);
    return drawn;
}

void GlPresenter::DropFence(void* fence)
{
    GetGl().DeleteSync(static_cast<GLsync>(fence));
}

bool GlPresenter::Capture(const WindowFrame& frame, bool picture, std::vector<u32>& pixels)
{
    Gl& gl = GetGl();
    SDL_GL_MakeCurrent(window_, windowContext_);
    const int width = frame.width, height = frame.height;
    GLuint target = 0, framebuffer = 0;
    gl.GenTextures(1, &target);
    gl.BindTexture(GL_TEXTURE_2D, target);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.GenFramebuffers(1, &framebuffer);
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    Draw(frame, picture, framebuffer);
    std::vector<u32> rows(static_cast<size_t>(width) * height);
    pixels.resize(rows.size());
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    // (OpenGL's rows are bottom first)
    for (int y = 0; y < height; y++)
    {
        std::copy_n(&rows[static_cast<size_t>(height - 1 - y) * width], width, &pixels[static_cast<size_t>(y) * width]);
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.DeleteFramebuffers(1, &framebuffer);
    gl.DeleteTextures(1, &target);
    return true;
}
}

std::unique_ptr<Presenter> MakeGlPresenter() { return std::make_unique<GlPresenter>(); }
}

namespace NativeGraphics
{
// The OpenGL renderer's contexts (gldevice.cpp): the render thread's, or the window's where there's no render thread's
bool PresenterContext(SDL_Window** window, SDL_GLContext* context)
{
    Ps2::GlPresenter* p = Ps2::g_presenter;
    if (p == nullptr || p->window_ == nullptr || p->windowContext_ == nullptr)
    {
        return false;
    }
    *window = p->window_;
    *context = p->windowContext_;
    return true;
}

bool RenderContext(SDL_Window** window, SDL_GLContext* context)
{
    Ps2::GlPresenter* p = Ps2::g_presenter;
    if (!Ps2::t_onRenderThread || p == nullptr || p->renderContext_ == nullptr)
    {
        return false;
    }
    *window = p->renderWindow_;
    *context = p->renderContext_;
    return true;
}
}
