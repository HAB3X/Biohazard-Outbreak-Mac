// The overlay's OpenGL device (overlaydevice.h): its list of draws drawn with OpenGL 3.3 core over the framebuffer bound, on the
// presenter's thread with the window's context current (the OpenGL presenter's, presentgl.cpp). The shader is overlaymetal.mm's
// in GLSL
#include "overlaydevice.h"

#include "../runtime.h"

#include <SDL3/SDL.h>

#include <map>
#include <string>

namespace Ps2::Ui
{
namespace
{
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLchar = char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLsizeiptr = long;

constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;

struct Gl
{
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*ActiveTexture)(GLenum);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*PixelStorei)(GLenum, GLint);
    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*EnableVertexAttribArray)(GLuint);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar*);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform1f)(GLint, GLfloat);
    void (*Uniform2f)(GLint, GLfloat, GLfloat);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
};

template <typename T> void Load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
}

bool LoadGl(Gl& gl)
{
    Load(gl.Enable, "glEnable");
    Load(gl.Disable, "glDisable");
    Load(gl.BlendFuncSeparate, "glBlendFuncSeparate");
    Load(gl.Scissor, "glScissor");
    Load(gl.Viewport, "glViewport");
    Load(gl.GenTextures, "glGenTextures");
    Load(gl.BindTexture, "glBindTexture");
    Load(gl.ActiveTexture, "glActiveTexture");
    Load(gl.TexParameteri, "glTexParameteri");
    Load(gl.TexImage2D, "glTexImage2D");
    Load(gl.PixelStorei, "glPixelStorei");
    Load(gl.GenBuffers, "glGenBuffers");
    Load(gl.BindBuffer, "glBindBuffer");
    Load(gl.BufferData, "glBufferData");
    Load(gl.GenVertexArrays, "glGenVertexArrays");
    Load(gl.BindVertexArray, "glBindVertexArray");
    Load(gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
    Load(gl.VertexAttribPointer, "glVertexAttribPointer");
    Load(gl.CreateShader, "glCreateShader");
    Load(gl.ShaderSource, "glShaderSource");
    Load(gl.CompileShader, "glCompileShader");
    Load(gl.GetShaderiv, "glGetShaderiv");
    Load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
    Load(gl.CreateProgram, "glCreateProgram");
    Load(gl.AttachShader, "glAttachShader");
    Load(gl.BindAttribLocation, "glBindAttribLocation");
    Load(gl.LinkProgram, "glLinkProgram");
    Load(gl.GetProgramiv, "glGetProgramiv");
    Load(gl.GetProgramInfoLog, "glGetProgramInfoLog");
    Load(gl.UseProgram, "glUseProgram");
    Load(gl.GetUniformLocation, "glGetUniformLocation");
    Load(gl.Uniform1i, "glUniform1i");
    Load(gl.Uniform1f, "glUniform1f");
    Load(gl.Uniform2f, "glUniform2f");
    Load(gl.Uniform4f, "glUniform4f");
    Load(gl.DrawArrays, "glDrawArrays");
    Load(gl.GenFramebuffers, "glGenFramebuffers");
    Load(gl.BindFramebuffer, "glBindFramebuffer");
    Load(gl.FramebufferTexture2D, "glFramebufferTexture2D");
    Load(gl.ClearColor, "glClearColor");
    Load(gl.Clear, "glClear");
    Load(gl.ReadPixels, "glReadPixels");
    Load(gl.DeleteTextures, "glDeleteTextures");
    Load(gl.DeleteFramebuffers, "glDeleteFramebuffers");
    return gl.Enable != nullptr && gl.CreateProgram != nullptr && gl.DrawArrays != nullptr && gl.GenVertexArrays != nullptr;
}


constexpr const char* VertexSource = R"(#version 330 core
in vec2 position;
in vec2 uv;
uniform vec2 viewport;
out vec2 pixel;
out vec2 texel;
void main()
{
    pixel = position;
    texel = uv;
    gl_Position = vec4(position.x / viewport.x * 2.0 - 1.0, 1.0 - position.y / viewport.y * 2.0, 0.0, 1.0);
}
)";

constexpr const char* FragmentSource = R"(#version 330 core
in vec2 pixel;
in vec2 texel;
out vec4 colour;
uniform int mode;
uniform sampler2D atlas;
uniform vec4 rect;
uniform float radius;
uniform vec4 fillTop;
uniform vec4 fillBottom;
uniform float border;
uniform vec4 borderColour;
uniform float blur;
uniform vec4 tint;
uniform float scale;
uniform vec2 offset;
uniform float time;

float RoundBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

vec4 Over(vec4 under, vec3 c, float a)
{
    return vec4(mix(under.rgb, c, a), under.a);
}

// The distance (in the spiral's own units) to an arm r = a + b * theta, theta from 0 to its end, its phase
float ArmDistance(float r, float angle, float a, float b, float phase, float end)
{
    const float tau = 6.28318530718;
    float base = mod(angle - phase, tau);
    float best = 1e9;
    for (int k = 0; k < 4; k++)
    {
        float theta = base + tau * float(k);
        if (theta <= end)
        {
            best = min(best, abs(r - (a + b * theta)));
        }
    }

    return best;
}

void main()
{
    if (mode == 0)
    {
        // A rounded box: its fill (a vertical gradient), its border, an edge softened by blur (a shadow)
        vec2 centre = rect.xy + rect.zw * 0.5;
        float d = RoundBox(pixel - centre, rect.zw * 0.5, radius);
        float t = clamp((pixel.y - rect.y) / max(rect.w, 1.0), 0.0, 1.0);
        vec4 fill = fillBottom.a < 0.0 ? fillTop : mix(fillTop, fillBottom, t);
        if (blur > 0.0)
        {
            float a = 1.0 - smoothstep(-blur, blur, d);
            colour = vec4(fill.rgb, fill.a * a);
            return;
        }

        float coverage = clamp(0.5 - d, 0.0, 1.0);
        vec4 c = fill;
        if (border > 0.0)
        {
            float edge = clamp(d + border + 0.5, 0.0, 1.0);
            c = mix(fill, borderColour, edge);
        }

        colour = vec4(c.rgb, c.a * coverage);
    }
    else if (mode == 1)
    {
        colour = vec4(tint.rgb, tint.a * texture(atlas, texel).r);
    }
    else if (mode == 2)
    {
        // The options' backdrop in the reference space
        vec2 q = (pixel - offset) / scale;
        vec2 direction = normalize(vec2(1.0, 1.0));
        float lengthAlong = 1280.0 * 0.70710678 + 720.0 * 0.70710678;
        float g = clamp(dot(q - vec2(640.0, 360.0), direction) / lengthAlong + 0.5, 0.0, 1.0);
        vec3 c0 = vec3(14.0, 95.0, 184.0) / 255.0;
        vec3 c1 = vec3(10.0, 59.0, 122.0) / 255.0;
        vec3 c2 = vec3(7.0, 31.0, 69.0) / 255.0;
        vec3 base = g < 0.55 ? mix(c0, c1, g / 0.55) : mix(c1, c2, (g - 0.55) / 0.45);
        vec4 c = vec4(base, 1.0);
        // The ring target (bands of 70)
        float ringDistance = length(q - vec2(100.0, 460.0));
        if (ringDistance < 320.0)
        {
            bool light = mod(floor(ringDistance / 70.0), 2.0) < 1.0;
            vec3 band = light ? vec3(232.0, 238.0, 246.0) / 255.0 : vec3(22.0, 118.0, 210.0) / 255.0;
            float edge = clamp(320.0 - ringDistance, 0.0, 1.0);
            c = Over(c, band, 0.18 * edge);
        }

        // The spiral: the svg's 1000 units over 1300 reference pixels, its middle at (270, 350), turning a turn in 90 seconds
        vec2 s = (q - vec2(270.0, 350.0)) / 1.3;
        float r = length(s);
        float angle = atan(s.y, s.x) - time * 6.28318530718 / 90.0;
        float pixelInSvg = 1.0 / (scale * 1.3);
        float end = 2.2 * 6.28318530718;
        float white = min(ArmDistance(r, angle, 10.0, 35.4475, 0.0, end), ArmDistance(r, angle, 10.0, 35.4475, 3.14159265, end));
        float whiteCover = clamp((35.0 - white) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, vec3(1.0), 0.18 * 0.7 * whiteCover);
        float blue = min(ArmDistance(r, angle, 9.854, 35.4477, 0.4456, end), ArmDistance(r, angle, 9.854, 35.4477, 3.5872, end));
        float blueCover = clamp((11.0 - blue) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, vec3(159.0, 182.0, 216.0) / 255.0, 0.25 * 0.7 * blueCover);
        colour = c;
    }
    else if (mode == 3)
    {
        vec4 t = texture(atlas, texel);
        colour = vec4(t.rgb, t.a * tint.a);
    }
    else if (mode == 5)
    {
        // A PlayStation face button: a dark round button, its shape in its colour (radius: 0 cross, 1 circle, 2 square, 3 triangle)
        vec2 p = (texel - vec2(0.5)) * 2.0;
        float d = length(p);
        float aa = 2.0 / max(fwidth(p.x) * 100.0, 1.0);
        float button = clamp((0.96 - d) / fwidth(d), 0.0, 1.0);
        vec3 c = mix(vec3(0.07, 0.08, 0.12), vec3(0.18, 0.20, 0.26), clamp(0.6 - p.y * 0.5, 0.0, 1.0));
        int shape = int(radius + 0.5);
        float stroke;
        vec3 ink;
        if (shape == 0)
        {
            vec2 q = abs(p);
            stroke = min(abs(q.x - q.y), 1e9) * 0.7071 - 0.09;
            stroke = max(stroke, max(q.x, q.y) - 0.48);
            ink = vec3(0.49, 0.71, 0.95);
        }
        else if (shape == 1)
        {
            stroke = abs(d - 0.42) - 0.09;
            ink = vec3(1.0, 0.40, 0.42);
        }
        else if (shape == 2)
        {
            vec2 q = abs(p);
            stroke = abs(max(q.x, q.y) - 0.38) - 0.08;
            ink = vec3(0.98, 0.55, 0.85);
        }
        else
        {
            vec2 q = vec2(abs(p.x), p.y + 0.08);
            float tri = max(q.x * 0.866 + q.y * 0.5, -q.y) - 0.27;
            stroke = abs(tri) - 0.08;
            ink = vec3(0.30, 0.86, 0.70);
        }

        float inkCover = clamp(-stroke / fwidth(stroke), 0.0, 1.0);
        c = mix(c, ink, inkCover);
        colour = vec4(c, button * tint.a);
    }
    else
    {
        // A fruit drawn when there's no art: an orange ball, its dark edge and a shine
        vec2 p = (texel - vec2(0.5)) * 2.0;
        float d = length(p);
        float edge = clamp((1.0 - d) * 24.0, 0.0, 1.0);
        vec3 orange = mix(vec3(1.0, 0.72, 0.25), vec3(0.85, 0.32, 0.02), clamp(length(p - vec2(-0.35, -0.35)) * 0.8, 0.0, 1.0));
        vec3 c = mix(vec3(0.23, 0.07, 0.0), orange, clamp((0.88 - d) * 24.0, 0.0, 1.0));
        float shine = clamp((0.22 - length(p - vec2(-0.35, -0.4))) * 20.0, 0.0, 1.0);
        c = mix(c, vec3(1.0), shine * 0.8);
        colour = vec4(c, edge * tint.a);
    }
}
)";

class GlOverlayDevice : public OverlayDevice
{
public:
    // Made with the window's context current: false when OpenGL or the program isn't there
    bool Init();
    OverlayTexture MakeTexture(s32 width, s32 height, bool oneChannel, const void* texels) override;
    void Draw(const OverlayDrawList& list) override;

private:
    GLint Uniform(const char* name);

    Gl gl_{};
    GLuint program_ = 0;
    GLuint vertexArray_ = 0;
    GLuint buffer_ = 0;
    std::map<std::string, GLint> uniforms_;
};

GLuint Compile(Gl& gl, GLenum kind, const char* source)
{
    GLuint shader = gl.CreateShader(kind);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Log("overlay: a shader didn't compile: %s", log);
        return 0;
    }

    return shader;
}

bool GlOverlayDevice::Init()
{
    if (!LoadGl(gl_))
    {
        Log("overlay: no OpenGL functions");
        return false;
    }

    Gl& gl = gl_;
    GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, FragmentSource);
    if (vertex == 0 || fragment == 0)
    {
        return false;
    }

    program_ = gl.CreateProgram();
    gl.AttachShader(program_, vertex);
    gl.AttachShader(program_, fragment);
    gl.BindAttribLocation(program_, 0, "position");
    gl.BindAttribLocation(program_, 1, "uv");
    gl.LinkProgram(program_);
    GLint linked = 0;
    gl.GetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048] = {};
        gl.GetProgramInfoLog(program_, sizeof(log), nullptr, log);
        Log("overlay: the program didn't link: %s", log);
        return false;
    }

    gl.GenVertexArrays(1, &vertexArray_);
    gl.BindVertexArray(vertexArray_);
    gl.GenBuffers(1, &buffer_);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer_);
    gl.EnableVertexAttribArray(0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(OverlayVertex), reinterpret_cast<const void*>(0));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(OverlayVertex), reinterpret_cast<const void*>(8));
    return true;
}

GLint GlOverlayDevice::Uniform(const char* name)
{
    auto found = uniforms_.find(name);
    if (found != uniforms_.end())
    {
        return found->second;
    }

    GLint location = gl_.GetUniformLocation(program_, name);
    uniforms_[name] = location;
    return location;
}

OverlayTexture GlOverlayDevice::MakeTexture(s32 width, s32 height, bool oneChannel, const void* texels)
{
    Gl& gl = gl_;
    GLuint texture = 0;
    gl.GenTextures(1, &texture);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, oneChannel ? 1 : 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, oneChannel ? GL_R8 : GL_RGBA8, width, height, 0, oneChannel ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE,
                  texels);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void GlOverlayDevice::Draw(const OverlayDrawList& list)
{
    Gl& gl = gl_;
    gl.Viewport(0, 0, list.width, list.height);
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.Disable(GL_SCISSOR_TEST);
    gl.UseProgram(program_);
    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer_);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(list.vertexes.size() * sizeof(OverlayVertex)), list.vertexes.data(),
                  GL_STREAM_DRAW);
    gl.Uniform1i(Uniform("atlas"), 0);
    gl.ActiveTexture(GL_TEXTURE0);
    for (const OverlayDraw& draw : list.draws)
    {
        const OverlayUniforms& u = draw.uniforms;
        gl.Uniform1i(Uniform("mode"), u.mode);
        gl.Uniform2f(Uniform("viewport"), u.viewport[0], u.viewport[1]);
        gl.Uniform4f(Uniform("rect"), u.rect[0], u.rect[1], u.rect[2], u.rect[3]);
        gl.Uniform1f(Uniform("radius"), u.radius);
        gl.Uniform4f(Uniform("fillTop"), u.fillTop[0], u.fillTop[1], u.fillTop[2], u.fillTop[3]);
        gl.Uniform4f(Uniform("fillBottom"), u.fillBottom[0], u.fillBottom[1], u.fillBottom[2], u.fillBottom[3]);
        gl.Uniform1f(Uniform("border"), u.border);
        gl.Uniform4f(Uniform("borderColour"), u.borderColour[0], u.borderColour[1], u.borderColour[2], u.borderColour[3]);
        gl.Uniform1f(Uniform("blur"), u.blur);
        gl.Uniform4f(Uniform("tint"), u.tint[0], u.tint[1], u.tint[2], u.tint[3]);
        gl.Uniform1f(Uniform("scale"), u.scale);
        gl.Uniform2f(Uniform("offset"), u.offset[0], u.offset[1]);
        gl.Uniform1f(Uniform("time"), u.time);
        gl.BindTexture(GL_TEXTURE_2D, draw.texture);
        if (draw.clipped)
        {
            gl.Enable(GL_SCISSOR_TEST);
            gl.Scissor(static_cast<GLint>(draw.clip[0]), static_cast<GLint>(static_cast<f32>(list.height) - draw.clip[1] - draw.clip[3]),
                       static_cast<GLsizei>(draw.clip[2]), static_cast<GLsizei>(draw.clip[3]));
        }
        else
        {
            gl.Disable(GL_SCISSOR_TEST);
        }

        gl.DrawArrays(GL_TRIANGLES, static_cast<GLint>(draw.first), static_cast<GLsizei>(draw.count));
    }

    gl.Disable(GL_SCISSOR_TEST);
}
}

std::unique_ptr<OverlayDevice> MakeGlOverlayDevice()
{
    auto device = std::make_unique<GlOverlayDevice>();
    if (!device->Init())
    {
        return nullptr;
    }

    return device;
}
}
