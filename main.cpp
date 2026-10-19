// shader_viewer.cpp
// Shadertoy-style viewer. SDL2 + OpenGL (4.6 with DSA, 3.3 fallback) + Dear ImGui.
//
// Threads:
//   main    : events, ImGui frame building, shader rendering, compositing, present
//   worker  : GL drawing of ImGui's output, on a context shared with main. The worker
//             renders the UI into a transparent offscreen target, and main composites
//             the newest finished target over the shader output.
//
// Usage: shader_viewer [shader_dir]      (default: ./shaders)
// Keys:  F1 toggles the UI

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef APIENTRY
#define APIENTRY
#endif

// ---------------------------------------------------------------------------
// GL tokens and types that SDL_opengl.h (GL 1.1) doesn't provide
// ---------------------------------------------------------------------------

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif

struct GLsyncObject;
using GLsyncPtr = GLsyncObject*;
static constexpr unsigned long long kTimeoutIgnored = 0xFFFFFFFFFFFFFFFFull;

// ---------------------------------------------------------------------------
// Function loader.
// GL_CORE_FUNCS exist on any GL 3.3 core context.
// GL_DSA_FUNCS need GL 4.5 and are only loaded when the context has them.
// ---------------------------------------------------------------------------

#define GL_CORE_FUNCS(X)                                                                         \
    X(GLuint,    glCreateShader,           (GLenum))                                             \
    X(void,      glShaderSource,           (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void,      glCompileShader,          (GLuint))                                             \
    X(void,      glGetShaderiv,            (GLuint, GLenum, GLint*))                             \
    X(void,      glGetShaderInfoLog,       (GLuint, GLsizei, GLsizei*, GLchar*))                 \
    X(void,      glDeleteShader,           (GLuint))                                             \
    X(GLuint,    glCreateProgram,          (void))                                               \
    X(void,      glAttachShader,           (GLuint, GLuint))                                     \
    X(void,      glLinkProgram,            (GLuint))                                             \
    X(void,      glGetProgramiv,           (GLuint, GLenum, GLint*))                             \
    X(void,      glGetProgramInfoLog,      (GLuint, GLsizei, GLsizei*, GLchar*))                 \
    X(void,      glDeleteProgram,          (GLuint))                                             \
    X(void,      glUseProgram,             (GLuint))                                             \
    X(GLint,     glGetUniformLocation,     (GLuint, const GLchar*))                              \
    X(void,      glUniform1i,              (GLint, GLint))                                       \
    X(void,      glUniform1f,              (GLint, GLfloat))                                     \
    X(void,      glUniform3f,              (GLint, GLfloat, GLfloat, GLfloat))                   \
    X(void,      glUniform4f,              (GLint, GLfloat, GLfloat, GLfloat, GLfloat))          \
    X(void,      glGenVertexArrays,        (GLsizei, GLuint*))                                   \
    X(void,      glBindVertexArray,        (GLuint))                                             \
    X(void,      glDeleteVertexArrays,     (GLsizei, const GLuint*))                             \
    X(void,      glGenFramebuffers,        (GLsizei, GLuint*))                                   \
    X(void,      glDeleteFramebuffers,     (GLsizei, const GLuint*))                             \
    X(void,      glBindFramebuffer,        (GLenum, GLuint))                                     \
    X(void,      glFramebufferTexture2D,   (GLenum, GLenum, GLenum, GLuint, GLint))              \
    X(GLenum,    glCheckFramebufferStatus, (GLenum))                                             \
    X(void,      glBlitFramebuffer,        (GLint, GLint, GLint, GLint, GLint, GLint, GLint,     \
                                            GLint, GLbitfield, GLenum))                          \
    X(void,      glActiveTexture,          (GLenum))                                             \
    X(void,      glGenQueries,             (GLsizei, GLuint*))                                   \
    X(void,      glDeleteQueries,          (GLsizei, const GLuint*))                             \
    X(void,      glBeginQuery,             (GLenum, GLuint))                                     \
    X(void,      glEndQuery,               (GLenum))                                             \
    X(void,      glGetQueryObjectiv,       (GLuint, GLenum, GLint*))                             \
    X(void,      glGetQueryObjectui64v,    (GLuint, GLenum, unsigned long long*))                \
    X(GLsyncPtr, glFenceSync,              (GLenum, GLbitfield))                                 \
    X(void,      glDeleteSync,             (GLsyncPtr))                                          \
    X(void,      glWaitSync,               (GLsyncPtr, GLbitfield, unsigned long long))

#define GL_DSA_FUNCS(X)                                                                          \
    X(void,   glCreateVertexArrays,         (GLsizei, GLuint*))                                  \
    X(void,   glCreateTextures,             (GLenum, GLsizei, GLuint*))                          \
    X(void,   glTextureStorage2D,           (GLuint, GLsizei, GLenum, GLsizei, GLsizei))         \
    X(void,   glTextureParameteri,          (GLuint, GLenum, GLint))                             \
    X(void,   glBindTextureUnit,            (GLuint, GLuint))                                    \
    X(void,   glCreateFramebuffers,         (GLsizei, GLuint*))                                  \
    X(void,   glNamedFramebufferTexture,    (GLuint, GLenum, GLuint, GLint))                     \
    X(GLenum, glCheckNamedFramebufferStatus,(GLuint, GLenum))                                    \
    X(void,   glBlitNamedFramebuffer,       (GLuint, GLuint, GLint, GLint, GLint, GLint, GLint,  \
                                             GLint, GLint, GLint, GLbitfield, GLenum))           \
    X(void,   glProgramUniform1i,           (GLuint, GLint, GLint))                              \
    X(void,   glProgramUniform1f,           (GLuint, GLint, GLfloat))                            \
    X(void,   glProgramUniform3f,           (GLuint, GLint, GLfloat, GLfloat, GLfloat))          \
    X(void,   glProgramUniform4f,           (GLuint, GLint, GLfloat, GLfloat, GLfloat, GLfloat))

#define DECLARE_GL(ret, name, args) static ret(APIENTRY* p_##name) args = nullptr;
GL_CORE_FUNCS(DECLARE_GL)
GL_DSA_FUNCS(DECLARE_GL)
#undef DECLARE_GL

static bool gDSA = false;   // true when GL 4.5+ DSA entry points are loaded

#define LOAD_GL(ret, name, args)                                                                 \
    p_##name = reinterpret_cast<decltype(p_##name)>(SDL_GL_GetProcAddress(#name));               \
    if (!p_##name) {                                                                             \
        std::fprintf(stderr, "missing GL function: %s\n", #name);                                \
        ok = false;                                                                              \
    }

static bool loadCoreGL()
{
    bool ok = true;
    GL_CORE_FUNCS(LOAD_GL)
    return ok;
}

static bool loadDsaGL()
{
    bool ok = true;
    GL_DSA_FUNCS(LOAD_GL)
    return ok;
}
#undef LOAD_GL

// ---------------------------------------------------------------------------
// State cache for the main thread's GL context. The worker thread has its own
// context and never touches this.
// ---------------------------------------------------------------------------

struct GlState {
    static constexpr GLuint kUnknown = ~0u;
    GLuint program = kUnknown;
    GLuint framebuffer = kUnknown;
    int vx = -1, vy = -1, vw = -1, vh = -1;

    // Call this after any raw bind that bypasses the helpers below.
    void invalidate()
    {
        program = framebuffer = kUnknown;
        vx = vy = vw = vh = -1;
    }
    void useProgram(GLuint p)
    {
        if (program != p) { p_glUseProgram(p); program = p; }
    }
    void bindFramebuffer(GLuint f)
    {
        if (framebuffer != f) { p_glBindFramebuffer(GL_FRAMEBUFFER, f); framebuffer = f; }
    }
    void viewport(int x, int y, int w, int h)
    {
        if (vx != x || vy != y || vw != w || vh != h) {
            glViewport(x, y, w, h);
            vx = x; vy = y; vw = w; vh = h;
        }
    }
};
static GlState gState;

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

// Fullscreen triangle from gl_VertexID, so no vertex buffer is needed.
static const char* kVertexSrc = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Draws a texture over the window. Used to composite the UI with alpha blending.
static const char* kOverlayFragSrc = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
out vec4 fragColor;
void main() {
    fragColor = texture(uSrc, vUV);
}
)";

// Prepended to every Shadertoy shader. Uniform names match Shadertoy's.
static const char* kFragHeader = R"(#version 330 core
uniform vec3      iResolution;
uniform float     iTime;
uniform float     iTimeDelta;
uniform int       iFrame;
uniform vec4      iMouse;
uniform vec4      iCustom;      // extra sliders from the UI (not part of Shadertoy)
uniform sampler2D iChannel0;    // textures/buffers are not wired up in this viewer
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
out vec4 _fragColor;
)";

// Appended after the user's code. Calls their mainImage with pixel coordinates.
static const char* kFragFooter = R"(
void main() {
    mainImage(_fragColor, gl_FragCoord.xy);
}
)";

static const char* kDefaultShader = R"(// Plasma. iCustom.x controls frequency, iCustom.y controls palette.
// Hold the left mouse button to draw a ring at the cursor.
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float freq = mix(2.0, 12.0, iCustom.x);

    float v = sin(freq * uv.x + iTime)
            + sin(freq * uv.y + iTime * 0.7)
            + sin(freq * (uv.x + uv.y) + iTime * 0.5)
            + sin(length(uv) * freq - iTime);

    vec3 col = 0.5 + 0.5 * cos(v * 1.5 + vec3(0.0, 2.1, 4.2) + iCustom.y * 6.0);

    vec2 m = (iMouse.xy * 2.0 - iResolution.xy) / iResolution.y;
    float ring = 1.0 - smoothstep(0.0, 0.02, abs(length(uv - m) - 0.1));
    col += 0.5 * ring * step(0.0, iMouse.z);

    fragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Program helpers
// ---------------------------------------------------------------------------

struct ShaderProgram {
    GLuint id = 0;
    GLint uResolution = -1, uTime = -1, uTimeDelta = -1, uFrame = -1, uMouse = -1, uCustom = -1;
};

static GLuint compileStage(GLenum type, const char* src, std::string& log)
{
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, nullptr);
    p_glCompileShader(s);

    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        p_glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(len > 1 ? len : 1, '\0');
        p_glGetShaderInfoLog(s, (GLsizei)buf.size(), nullptr, buf.data());
        log = buf.data();
        p_glDeleteShader(s);
        return 0;
    }
    return s;
}

// Links a vertex shader with a fragment source. Returns 0 and fills log on failure.
static GLuint linkProgram(GLuint vs, const char* fragSrc, std::string& log)
{
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc, log);
    if (!fs) return 0;

    GLuint prog = p_glCreateProgram();
    p_glAttachShader(prog, vs);
    p_glAttachShader(prog, fs);
    p_glLinkProgram(prog);
    p_glDeleteShader(fs);

    GLint ok = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        p_glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(len > 1 ? len : 1, '\0');
        p_glGetProgramInfoLog(prog, (GLsizei)buf.size(), nullptr, buf.data());
        log = buf.data();
        p_glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Compiles the user's mainImage code. The caller only swaps programs on success.
static bool buildShader(GLuint vs, const std::string& userSrc, ShaderProgram& out, std::string& log)
{
    // "#line 1" makes compiler error line numbers match the user's code.
    std::string full = std::string(kFragHeader) + "#line 1\n" + userSrc + "\n" + kFragFooter;

    GLuint prog = linkProgram(vs, full.c_str(), log);
    if (!prog) return false;

    out.id = prog;
    out.uResolution = p_glGetUniformLocation(prog, "iResolution");
    out.uTime       = p_glGetUniformLocation(prog, "iTime");
    out.uTimeDelta  = p_glGetUniformLocation(prog, "iTimeDelta");
    out.uFrame      = p_glGetUniformLocation(prog, "iFrame");
    out.uMouse      = p_glGetUniformLocation(prog, "iMouse");
    out.uCustom     = p_glGetUniformLocation(prog, "iCustom");
    return true;
}

// Uniform setters. With DSA, glProgramUniform* writes the value without binding.
// The legacy path binds through the state cache first.
static void setU1i(GLuint prog, GLint loc, GLint v)
{
    if (gDSA) p_glProgramUniform1i(prog, loc, v);
    else      { gState.useProgram(prog); p_glUniform1i(loc, v); }
}

static void setU1f(GLuint prog, GLint loc, GLfloat v)
{
    if (gDSA) p_glProgramUniform1f(prog, loc, v);
    else      { gState.useProgram(prog); p_glUniform1f(loc, v); }
}

static void setU3f(GLuint prog, GLint loc, GLfloat a, GLfloat b, GLfloat c)
{
    if (gDSA) p_glProgramUniform3f(prog, loc, a, b, c);
    else      { gState.useProgram(prog); p_glUniform3f(loc, a, b, c); }
}

static void setU4f(GLuint prog, GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{
    if (gDSA) p_glProgramUniform4f(prog, loc, a, b, c, d);
    else      { gState.useProgram(prog); p_glUniform4f(loc, a, b, c, d); }
}

// Per-frame uniform values, and the values last uploaded to the current program.
struct FrameUniforms {
    float res[3];
    float time, timeDelta;
    int frame;
    float mouse[4];
    float custom[4];
};

struct UniformCache {
    bool valid = false;     // false after a new program is built: upload everything
    FrameUniforms last{};
};

// Uniform values stay set on the program object, so only changed values are uploaded.
static void uploadUniforms(const ShaderProgram& p, UniformCache& c, const FrameUniforms& u)
{
    const bool all = !c.valid;
    const FrameUniforms& old = c.last;

    if (p.uResolution >= 0 && (all || std::memcmp(old.res, u.res, sizeof u.res) != 0))
        setU3f(p.id, p.uResolution, u.res[0], u.res[1], u.res[2]);
    if (p.uTime >= 0 && (all || old.time != u.time))
        setU1f(p.id, p.uTime, u.time);
    if (p.uTimeDelta >= 0 && (all || old.timeDelta != u.timeDelta))
        setU1f(p.id, p.uTimeDelta, u.timeDelta);
    if (p.uFrame >= 0 && (all || old.frame != u.frame))
        setU1i(p.id, p.uFrame, u.frame);
    if (p.uMouse >= 0 && (all || std::memcmp(old.mouse, u.mouse, sizeof u.mouse) != 0))
        setU4f(p.id, p.uMouse, u.mouse[0], u.mouse[1], u.mouse[2], u.mouse[3]);
    if (p.uCustom >= 0 && (all || std::memcmp(old.custom, u.custom, sizeof u.custom) != 0))
        setU4f(p.id, p.uCustom, u.custom[0], u.custom[1], u.custom[2], u.custom[3]);

    c.last = u;
    c.valid = true;
}

// ---------------------------------------------------------------------------
// Offscreen render targets
// ---------------------------------------------------------------------------

struct RenderTarget {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
};

static void destroyTarget(RenderTarget& rt)
{
    if (rt.tex) glDeleteTextures(1, &rt.tex);
    if (rt.fbo) p_glDeleteFramebuffers(1, &rt.fbo);
    rt = RenderTarget{};
}

// Does not touch gState, so the worker thread can call it too. A caller on the
// main thread must call gState.invalidate() afterwards.
static bool createTarget(RenderTarget& rt, int w, int h)
{
    destroyTarget(rt);
    bool ok = false;

    if (gDSA) {
        p_glCreateTextures(GL_TEXTURE_2D, 1, &rt.tex);
        p_glTextureStorage2D(rt.tex, 1, GL_RGBA8, w, h);
        p_glTextureParameteri(rt.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        p_glTextureParameteri(rt.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p_glTextureParameteri(rt.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        p_glTextureParameteri(rt.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        p_glCreateFramebuffers(1, &rt.fbo);
        p_glNamedFramebufferTexture(rt.fbo, GL_COLOR_ATTACHMENT0, rt.tex, 0);
        ok = p_glCheckNamedFramebufferStatus(rt.fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    } else {
        glGenTextures(1, &rt.tex);
        glBindTexture(GL_TEXTURE_2D, rt.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        p_glGenFramebuffers(1, &rt.fbo);
        p_glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.tex, 0);
        ok = p_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (ok) { rt.w = w; rt.h = h; }
    else    destroyTarget(rt);
    return ok;
}

static void bindTexture0(GLuint tex)
{
    if (gDSA) {
        p_glBindTextureUnit(0, tex);
    } else {
        p_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
}

// Scales a finished render target into the window, with linear filtering.
// Main thread only.
static void blitToWindow(GLuint src, int sw, int sh, int dw, int dh)
{
    if (gDSA) {
        p_glBlitNamedFramebuffer(src, 0, 0, 0, sw, sh, 0, 0, dw, dh,
                                 GL_COLOR_BUFFER_BIT, GL_LINEAR);
    } else {
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        p_glBlitFramebuffer(0, 0, sw, sh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gState.invalidate();
    }
}

// ---------------------------------------------------------------------------
// Graphs and frame timing
// ---------------------------------------------------------------------------

// Ring buffer of recent samples, shown with ImGui::PlotLines.
struct FrameGraph {
    static constexpr int N = 240;
    float v[N] = {};
    int head = 0;      // next write position, which is also the oldest sample once full
    int count = 0;

    void push(float x)
    {
        v[head] = x;
        head = (head + 1) % N;
        if (count < N) ++count;
    }

    float average() const
    {
        if (count == 0) return 0.0f;
        double sum = 0.0;
        for (int i = 0; i < count; ++i) sum += v[i];
        return (float)(sum / count);
    }
};

static void drawGraph(const char* label, const FrameGraph& g)
{
    char overlay[64];
    std::snprintf(overlay, sizeof overlay, "avg %.2f ms", g.average());
    ImGui::PlotLines(label, g.v, FrameGraph::N, g.head, overlay, FLT_MAX, FLT_MAX,
                     ImVec2(-FLT_MIN, 50.0f));
}

// Keeps the shader's time in [0, period). The wrap happens in double precision on the
// CPU, so the float sent to the GPU stays precise however long the viewer runs.
// Period 0 means no wrap. Shaders that aren't periodic with the period will show a seam.
static double wrapTime(double t, float period)
{
    return period > 0.0f ? std::fmod(t, (double)period) : t;
}

enum FrameLimit { kVSync = 0, kUncapped = 1, kTarget = 2 };
static const char* const kFrameLimitNames[] = {"VSync", "Uncapped", "Target FPS"};

// Sleeps until the next frame deadline. Sleeps coarsely, then spins the last ~1 ms
// because SDL_Delay usually overshoots.
static void limitFrame(Uint64& deadline, double freq, double fps)
{
    const Uint64 step = (Uint64)(freq / fps);
    deadline += step;

    Uint64 now = SDL_GetPerformanceCounter();
    if (deadline > now) {
        double waitMs = (double)(deadline - now) * 1000.0 / freq;
        if (waitMs > 1.5) SDL_Delay((Uint32)(waitMs - 1.0));
        while (SDL_GetPerformanceCounter() < deadline) { /* spin */ }
    } else if (now - deadline > step) {
        deadline = now;   // we fell far behind (e.g. after a stall): resync
    }
}

// ---------------------------------------------------------------------------
// File helpers and context creation
// ---------------------------------------------------------------------------

static std::vector<std::string> listShaders(const std::string& dir)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glsl")
            out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static SDL_GLContext createContext(SDL_Window* win, int major, int minor)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    return SDL_GL_CreateContext(win);
}

// ---------------------------------------------------------------------------
// UiWorker: renders ImGui's draw data on a second thread.
//
// The main thread builds the ImGui frame and hands over its draw data. It waits for
// the worker to finish before the next NewFrame, since that is when ImGui frees the data.
// The worker draws it into one of three transparent targets. The main thread
// composites the newest finished target over the shader output. Fences make sure
// neither thread touches a texture while the other still uses it.
//
// The UI is still built on the main thread. ImGui's input handling and the SDL
// backend aren't thread-safe, and building the UI is cheap. Drawing it is where
// the driver work is, and that moves to the worker.
// ---------------------------------------------------------------------------

class UiWorker {
public:
    UiWorker(SDL_Window* window, SDL_GLContext ctx, const char* glslVersion)
        : window_(window), ctx_(ctx)
    {
        std::promise<bool> started;
        std::future<bool> ready = started.get_future();
        thread_ = std::thread([this, glslVersion, &started] { run(glslVersion, started); });
        ok_ = ready.get();
    }

    ~UiWorker()
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    UiWorker(const UiWorker&) = delete;
    UiWorker& operator=(const UiWorker&) = delete;

    bool ok() const { return ok_; }

    // Main thread. The draw data must stay valid until the worker is done, so call
    // waitIdle() before the next ImGui::NewFrame(), which is when ImGui frees it.
    void submit(ImDrawData* draw, int w, int h)
    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_ = Job{draw, w, h};
        busy_ = true;
        cv_.notify_all();
    }

    // Main thread. Blocks until the worker has finished the last submitted frame.
    void waitIdle()
    {
        std::unique_lock<std::mutex> lk(mu_);
        idleCv_.wait(lk, [this] { return !busy_; });
    }

    // Main thread, once per drawn frame. Returns the texture to composite, or 0.
    GLuint acquire()
    {
        GLsyncPtr fence = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (ready_ >= 0) {
                shown_ = ready_;
                ready_ = -1;
                fence = readyFence_;
                readyFence_ = nullptr;
            }
        }
        if (fence) {
            p_glWaitSync(fence, 0, kTimeoutIgnored);   // GPU-side wait, the CPU doesn't block
            p_glDeleteSync(fence);
        }
        return shown_ >= 0 ? slots_[shown_].tex : 0;
    }

    // Main thread, after compositing the texture from acquire(). Tells the worker
    // when it's safe to write into that slot again.
    void release()
    {
        GLsyncPtr f = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        std::lock_guard<std::mutex> lk(mu_);
        if (slotFence_[shown_]) p_glDeleteSync(slotFence_[shown_]);
        slotFence_[shown_] = f;
    }

private:
    struct Job {
        ImDrawData* draw = nullptr;
        int w = 0, h = 0;
    };

    void run(const char* glslVersion, std::promise<bool>& started)
    {
        if (!SDL_GL_MakeCurrent(window_, ctx_)) {
            std::fprintf(stderr, "UI worker: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
            started.set_value(false);
            return;
        }
        ImGui_ImplOpenGL3_Init(glslVersion);   // creates the backend's GL objects here
        started.set_value(true);

        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return quit_ || pending_.draw != nullptr; });
                if (!pending_.draw) break;     // quit requested with nothing left to draw
                job = pending_;
                pending_ = Job{};
            }

            // Pick a slot that main isn't reading, then wait until main is done with it.
            int s = 0;
            GLsyncPtr waitFence = nullptr;
            {
                std::lock_guard<std::mutex> lk(mu_);
                while (s == shown_ || s == ready_) ++s;
                waitFence = slotFence_[s];
                slotFence_[s] = nullptr;
            }
            if (waitFence) {
                p_glWaitSync(waitFence, 0, kTimeoutIgnored);
                p_glDeleteSync(waitFence);
            }

            RenderTarget& rt = slots_[s];
            if (rt.fbo == 0 || rt.w != job.w || rt.h != job.h) {
                if (!createTarget(rt, job.w, job.h)) {
                    std::lock_guard<std::mutex> lk(mu_);
                    busy_ = false;
                    idleCv_.notify_all();
                    continue;
                }
            }

            p_glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
            glViewport(0, 0, job.w, job.h);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);     // transparent: the UI is an overlay
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(job.draw);
            p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

            GLsyncPtr done = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();

            std::lock_guard<std::mutex> lk(mu_);
            if (readyFence_) p_glDeleteSync(readyFence_);   // an unused frame gets replaced
            readyFence_ = done;
            ready_ = s;
            busy_ = false;
            idleCv_.notify_all();
        }

        // Shutdown runs on the worker's own context, where these objects were created.
        for (auto& rt : slots_) destroyTarget(rt);
        for (auto& f : slotFence_) {
            if (f) p_glDeleteSync(f);
            f = nullptr;
        }
        if (readyFence_) p_glDeleteSync(readyFence_);
        ImGui_ImplOpenGL3_Shutdown();
        SDL_GL_MakeCurrent(window_, nullptr);
    }

    SDL_Window* window_;
    SDL_GLContext ctx_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;        // worker waits here for a job
    std::condition_variable idleCv_;    // main waits here for the worker to finish
    bool busy_ = false;                 // a submitted job hasn't finished yet

    // Everything below is guarded by mu_, except slots_[shown_], which only main reads.
    Job pending_;
    bool quit_ = false;
    bool ok_ = false;
    RenderTarget slots_[3];
    GLsyncPtr slotFence_[3] = {nullptr, nullptr, nullptr};
    int shown_ = -1;                      // slot main composites
    int ready_ = -1;                      // finished slot not yet taken by main
    GLsyncPtr readyFence_ = nullptr;      // worker's "finished rendering" fence for ready_
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static char gEditor[1 << 16];  // 64 KB editor buffer

int main(int argc, char** argv)
{
    const std::string shaderDir = argc > 1 ? argv[1] : "shaders";
    std::filesystem::create_directories(shaderDir);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Shadertoy Viewer",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    // Prefer GL 4.6 core, fall back to 3.3 core.
    SDL_GLContext glctx = createContext(window, 4, 6);
    if (!glctx) {
        SDL_ClearError();
        glctx = createContext(window, 3, 3);
    }
    if (!glctx) {
        std::fprintf(stderr, "could not create a GL 3.3+ context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(1);

    if (!loadCoreGL()) return 1;

    int glMajor = 0, glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    gDSA = (glMajor > 4 || (glMajor == 4 && glMinor >= 5)) && loadDsaGL();
    const std::string glInfo = "GL " + std::to_string(glMajor) + "." + std::to_string(glMinor) +
                               (gDSA ? " (DSA paths)" : " (legacy paths)");

    // ImGui: the context and input live on the main thread.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);

    // The UI worker gets its own hidden window and a context that shares objects with
    // the main one. SDL doesn't let the visible window's context be current on two
    // threads, so the worker never touches the visible window.
    SDL_Window* uiWindow = SDL_CreateWindow("ui worker", 0, 0, 1, 1,
                                            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!uiWindow) {
        std::fprintf(stderr, "could not create the UI worker window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GLContext uiCtx = createContext(uiWindow, glMajor, glMinor);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    SDL_GL_MakeCurrent(window, glctx);
    if (!uiCtx) {
        std::fprintf(stderr, "could not create a shared UI context: %s\n", SDL_GetError());
        return 1;
    }

    // Shared vertex shader, overlay program, and the VAO. The VAO is bound once and
    // never changes on the main context.
    std::string log;
    GLuint vs = compileStage(GL_VERTEX_SHADER, kVertexSrc, log);
    if (!vs) {
        std::fprintf(stderr, "vertex shader failed:\n%s\n", log.c_str());
        return 1;
    }
    GLuint overlayProg = linkProgram(vs, kOverlayFragSrc, log);
    if (!overlayProg) {
        std::fprintf(stderr, "overlay shader failed:\n%s\n", log.c_str());
        return 1;
    }
    setU1i(overlayProg, p_glGetUniformLocation(overlayProg, "uSrc"), 0);

    GLuint vao = 0;
    if (gDSA) p_glCreateVertexArrays(1, &vao);
    else      p_glGenVertexArrays(1, &vao);
    p_glBindVertexArray(vao);

    // ---- app state ----
    ShaderProgram prog;
    UniformCache cache;
    RenderTarget rt;

    std::string selected;
    std::vector<std::string> files = listShaders(shaderDir);
    std::string status = glInfo + " ready";
    bool statusError = false;

    float timeScale = 1.0f;
    bool paused = false;
    bool stepRequest = false;
    double simTime = 0.0;
    int frame = 0;
    float custom[4] = {0.5f, 0.5f, 0.5f, 0.5f};

    float renderScale = 1.0f;
    float timeWrap = 0.0f;      // seconds before iTime wraps to 0; 0 = never
    int frameLimitIdx = kVSync;
    float targetFps = 60.0f;
    bool skipIdle = true;       // don't render while paused and nothing changed
    bool uiVisible = true;      // F1 toggles this

    FrameGraph gFrameMs, gCpuMs, gGpuMs;
    GLuint gpuQ[2] = {0, 0};    // GL_TIME_ELAPSED queries, used in ping-pong
    bool qPending[2] = {false, false};
    int qIdx = 0;
    p_glGenQueries(2, gpuQ);

    float mouse[4] = {0, 0, 0, 0};   // xy = current, zw = click (negative when released)
    bool mouseWasDown = false;
    float clickX = 0, clickY = 0;

    int winW = 1, winH = 1, dw = 1, dh = 1;
    bool redraw = true;
    bool running = true;

    auto refreshSizes = [&]() {
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        winW = std::max(winW, 1);
        winH = std::max(winH, 1);
        dw = std::max(dw, 1);
        dh = std::max(dh, 1);
    };
    refreshSizes();

    auto applyFrameLimit = [&]() {
        SDL_GL_SetSwapInterval(frameLimitIdx == kVSync ? 1 : 0);
    };
    applyFrameLimit();

    auto compile = [&]() {
        ShaderProgram next;
        std::string err;
        if (buildShader(vs, std::string(gEditor), next, err)) {
            if (prog.id) p_glDeleteProgram(prog.id);
            gState.invalidate();      // the deleted program may have been cached as bound
            prog = next;
            cache.valid = false;
            status = "Compiled OK";
            statusError = false;
        } else {
            status = err;
            statusError = true;
        }
    };

    auto loadFile = [&](const std::string& name) {
        std::string text;
        if (readFile(shaderDir + "/" + name, text)) {
            std::strncpy(gEditor, text.c_str(), sizeof(gEditor) - 1);
            gEditor[sizeof(gEditor) - 1] = '\0';
            selected = name;
            compile();
        } else {
            status = "could not read " + name;
            statusError = true;
        }
    };

    auto saveFile = [&]() {
        if (selected.empty()) selected = "untitled.glsl";
        std::ofstream f(shaderDir + "/" + selected);
        if (f) {
            f << gEditor;
            status = "saved " + selected;
            statusError = false;
            files = listShaders(shaderDir);
        } else {
            status = "could not write " + selected;
            statusError = true;
        }
    };

    auto handleEvent = [&](const SDL_Event& e) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        redraw = true;   // any input or window event means the frame may have changed
        switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(window))
                    running = false;
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED)
                    refreshSizes();
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_F1 && !e.key.repeat) uiVisible = !uiVisible;
                break;
        }
    };

    // Start the UI worker. This waits for its ImGui backend setup to finish.
    std::unique_ptr<UiWorker> ui = std::make_unique<UiWorker>(uiWindow, uiCtx, "#version 330 core");
    if (!ui->ok()) {
        // Fall back to drawing the UI on the main thread so the viewer still runs.
        std::fprintf(stderr, "UI worker unavailable; drawing the UI on the main thread\n");
        ui.reset();
        SDL_GL_MakeCurrent(window, glctx);
        ImGui_ImplOpenGL3_Init("#version 330 core");
    }

    // Start with the built-in shader
    std::strncpy(gEditor, kDefaultShader, sizeof(gEditor) - 1);
    compile();

    Uint64 last = SDL_GetPerformanceCounter();
    const double perfFreq = (double)SDL_GetPerformanceFrequency();
    Uint64 nextFrame = last;
    Uint64 prevDrawn = last;

    while (running) {
        // ---- input. When paused and nothing changed, block instead of spinning. ----
        if (skipIdle && paused && !stepRequest && !redraw) {
            SDL_Event e;
            if (SDL_WaitEventTimeout(&e, 250)) handleEvent(e);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running) break;

        if (skipIdle && paused && !stepRequest && !redraw) {
            last = SDL_GetPerformanceCounter();
            nextFrame = last;
            continue;   // nothing to draw: the last image stays on screen
        }
        redraw = false;

        // ---- time ----
        const Uint64 frameStart = SDL_GetPerformanceCounter();
        const float dt = std::min((float)((double)(frameStart - last) / perfFreq), 0.1f);
        last = frameStart;

        const bool stepNow = stepRequest;
        stepRequest = false;
        const bool advance = !paused || stepNow;
        const float timeDelta = !advance ? 0.0f : (paused ? 1.0f / 60.0f : dt * timeScale);
        simTime += timeDelta;
        if (advance) ++frame;

        // ---- UI: built here on the main thread, drawn by the worker ----
        ImGuiIO& io = ImGui::GetIO();
        if (uiVisible) {
            if (ui) ui->waitIdle();   // the worker may still be reading last frame's draw data
            else    ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("Controls");
            ImGui::Text("%.1f FPS   frame %d   time %.2f", io.Framerate, frame, simTime);
            ImGui::TextDisabled("%s   (F1 hides this UI)", glInfo.c_str());

            if (ImGui::Button(paused ? "Play" : "Pause")) paused = !paused;
            ImGui::SameLine();
            if (ImGui::Button("Step")) { paused = true; stepRequest = true; }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { simTime = 0.0; frame = 0; }

            ImGui::SliderFloat("time scale", &timeScale, 0.0f, 4.0f);
            ImGui::SliderFloat("render scale", &renderScale, 0.25f, 1.0f, "%.2f");
            ImGui::SliderFloat("wrap time (s, 0 = off)", &timeWrap, 0.0f, 600.0f, "%.1f");
            ImGui::SliderFloat4("iCustom", custom, 0.0f, 1.0f);

            ImGui::Separator();
            if (ImGui::Combo("frame limit", &frameLimitIdx, kFrameLimitNames,
                             (int)(sizeof(kFrameLimitNames) / sizeof(kFrameLimitNames[0]))))
                applyFrameLimit();
            if (frameLimitIdx == kTarget)
                ImGui::SliderFloat("target FPS", &targetFps, 10.0f, 360.0f, "%.0f");
            ImGui::Checkbox("skip redraw when paused", &skipIdle);
            if (ImGui::CollapsingHeader("Frame graph", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawGraph("frame interval (ms)", gFrameMs);
                drawGraph("cpu time (ms)", gCpuMs);
                drawGraph("gpu shader time (ms)", gGpuMs);
            }

            ImGui::Separator();
            if (ImGui::BeginCombo("shader", selected.empty() ? "(built-in)" : selected.c_str())) {
                for (const auto& f : files) {
                    if (ImGui::Selectable(f.c_str(), f == selected)) loadFile(f);
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Rescan folder")) files = listShaders(shaderDir);
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(600, 420), ImGuiCond_FirstUseEver);
            ImGui::Begin("Shader editor");
            const float footer = ImGui::GetFrameHeightWithSpacing() +
                                 ImGui::GetTextLineHeightWithSpacing() * 4;
            ImGui::InputTextMultiline("##src", gEditor, sizeof(gEditor),
                                      ImVec2(-FLT_MIN, -footer), ImGuiInputTextFlags_AllowTabInput);
            if (ImGui::Button("Compile")) compile();
            ImGui::SameLine();
            if (ImGui::Button("Save")) saveFile();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, statusError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                                             : ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::PopStyleColor();
            ImGui::End();

            ImGui::Render();
            // The worker draws this while the main thread does the shader pass.
            if (ui) ui->submit(ImGui::GetDrawData(), dw, dh);
        }

        // ---- render target: sized to renderScale of the drawable ----
        int rw = dw, rh = dh;
        bool useTarget = false;
        if (renderScale < 0.999f) {
            rw = std::max(1, (int)std::lround(dw * renderScale));
            rh = std::max(1, (int)std::lround(dh * renderScale));
            useTarget = (rw != dw || rh != dh);
        }
        if (useTarget && (rt.w != rw || rt.h != rh || !rt.fbo)) {
            if (createTarget(rt, rw, rh)) {
                gState.invalidate();
            } else {
                useTarget = false;
                rw = dw;
                rh = dh;
                renderScale = 1.0f;
                status = "render target failed; using full resolution";
                statusError = true;
            }
        }

        // ---- mouse, in render-target pixels with origin bottom-left ----
        int mx = 0, my = 0;
        const Uint32 buttons = SDL_GetMouseState(&mx, &my);
        const float kx = (float)rw / (float)winW;
        const float ky = (float)rh / (float)winH;
        const float fx = mx * kx;
        const float fy = (float)rh - my * ky;

        const bool down = (buttons & SDL_BUTTON_LMASK) && !(uiVisible && io.WantCaptureMouse);
        if (down) {
            if (!mouseWasDown) { clickX = fx; clickY = fy; }
            mouse[0] = fx;     mouse[1] = fy;
            mouse[2] = clickX; mouse[3] = clickY;
        } else if (mouseWasDown) {
            mouse[2] = -std::fabs(mouse[2]);
            mouse[3] = -std::fabs(mouse[3]);
        }
        mouseWasDown = down;

        // ---- shader pass ----
        gState.bindFramebuffer(useTarget ? rt.fbo : 0);
        gState.viewport(0, 0, rw, rh);

        // The user's shader writes every pixel, so the clear is only needed without one.
        if (!prog.id) {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // Read the GPU time from two frames ago, but only if it's ready. Waiting here would stall.
        if (qPending[qIdx]) {
            GLint avail = 0;
            p_glGetQueryObjectiv(gpuQ[qIdx], GL_QUERY_RESULT_AVAILABLE, &avail);
            if (avail) {
                unsigned long long ns = 0;
                p_glGetQueryObjectui64v(gpuQ[qIdx], GL_QUERY_RESULT, &ns);
                gGpuMs.push((float)((double)ns / 1.0e6));
                qPending[qIdx] = false;
            }
        }
        const bool timeThisFrame = !qPending[qIdx] && prog.id;

        if (prog.id) {
            FrameUniforms u{};
            u.res[0] = (float)rw;
            u.res[1] = (float)rh;
            u.res[2] = 1.0f;
            u.time = (float)wrapTime(simTime, timeWrap);
            u.timeDelta = timeDelta;
            u.frame = frame;
            std::memcpy(u.mouse, mouse, sizeof mouse);
            std::memcpy(u.custom, custom, sizeof custom);

            gState.useProgram(prog.id);
            if (timeThisFrame) p_glBeginQuery(GL_TIME_ELAPSED, gpuQ[qIdx]);
            uploadUniforms(prog, cache, u);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (timeThisFrame) {
            p_glEndQuery(GL_TIME_ELAPSED);
            qPending[qIdx] = true;
        }
        qIdx ^= 1;

        if (useTarget) blitToWindow(rt.fbo, rw, rh, dw, dh);

        // ---- UI over the shader output ----
        if (uiVisible && !ui) {
            // Fallback: no worker, so draw the UI directly on the main thread.
            gState.bindFramebuffer(0);
            gState.viewport(0, 0, dw, dh);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            gState.invalidate();   // the backend changes GL state
        }
        if (uiVisible && ui) {
            const GLuint overlay = ui->acquire();
            if (overlay) {
                gState.bindFramebuffer(0);
                gState.viewport(0, 0, dw, dh);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // the UI target is premultiplied
                gState.useProgram(overlayProg);
                bindTexture0(overlay);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glDisable(GL_BLEND);
                ui->release();
            }
        }

        // ---- graphs. Measured before the swap so VSync waits don't count as CPU time. ----
        {
            const double msPerTick = 1000.0 / perfFreq;
            gCpuMs.push((float)((double)(SDL_GetPerformanceCounter() - frameStart) * msPerTick));
            const double interval = (double)(frameStart - prevDrawn) * msPerTick;
            if (interval < 250.0) gFrameMs.push((float)interval);   // ignore gaps while idle
            prevDrawn = frameStart;
        }

        SDL_GL_SwapWindow(window);

        // ---- frame limiter ----
        if (frameLimitIdx == kTarget) limitFrame(nextFrame, perfFreq, targetFps);
        else                          nextFrame = SDL_GetPerformanceCounter();
    }

    // ---- cleanup. Stop the worker first, then free the main context's objects. ----
    if (prog.id) p_glDeleteProgram(prog.id);
    p_glDeleteProgram(overlayProg);
    destroyTarget(rt);
    p_glDeleteQueries(2, gpuQ);
    p_glDeleteVertexArrays(1, &vao);

    if (ui) ui.reset();               // joins the worker, which frees its own GL objects
    else    ImGui_ImplOpenGL3_Shutdown();
    SDL_GL_DeleteContext(uiCtx);
    SDL_DestroyWindow(uiWindow);

    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}