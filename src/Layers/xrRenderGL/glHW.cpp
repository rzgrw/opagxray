// glHW.cpp: implementation of the OpenGL specialisation of CHW.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "glHW.h"
#include "xrEngine/XR_IOConsole.h"

#ifdef XR_PLATFORM_APPLE
#include <SDL_syswm.h>
// Declared directly: including <objc/message.h> would clash with the
// engine-wide BOOL typedef.
extern "C" void* objc_msgSend(void);
extern "C" void* sel_registerName(const char*);
#endif

namespace xray::render::RENDER_NAMESPACE
{
CHW HW;

void CALLBACK OnDebugCallback(GLenum /*source*/, GLenum /*type*/, GLuint id, GLenum severity, GLsizei /*length*/,
    const GLchar* message, const void* /*userParam*/)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        Log(message, id);
}

static_assert(std::is_same_v<decltype(&OnDebugCallback), GLDEBUGPROC>);

void UpdateVSync()
{
    if (psDeviceFlags.test(rsVSync))
    {
        // Try adaptive vsync first
        if (SDL_GL_SetSwapInterval(-1) == -1)
            SDL_GL_SetSwapInterval(1);
    }
    else
    {
        SDL_GL_SetSwapInterval(0);
    }
    if (Device.FrameTime.Running())
        Device.FrameTime.ObserveVsync(SDL_GL_GetSwapInterval());
}

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::OnAppActivate()
{
    if (m_window)
    {
        SDL_RestoreWindow(m_window);
    }
}

void CHW::OnAppDeactivate()
{
    if (m_window)
    {
        if (psDeviceMode.WindowStyle == rsFullscreen || psDeviceMode.WindowStyle == rsFullscreenBorderless)
            SDL_MinimizeWindow(m_window);
    }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CHW::CreateDevice(SDL_Window* hWnd)
{
    ZoneScoped;

    m_window = hWnd;

    R_ASSERT(m_window);

    // Choose the closest pixel format
    SDL_DisplayMode mode;
    SDL_GetWindowDisplayMode(m_window, &mode);
    mode.format = SDL_PIXELFORMAT_RGBA8888;
    // Apply the pixel format to the device context
    SDL_SetWindowDisplayMode(m_window, &mode);

    Caps.fTarget = D3DFMT_A8R8G8B8;
    Caps.fDepth = D3DFMT_D24S8;

    // Create the context
    m_context = SDL_GL_CreateContext(m_window);
    if (m_context == nullptr)
    {
        Log("! OpenGL: could not create drawing context:", SDL_GetError());
        return;
    }

    if (MakeContextCurrent(IRender::PrimaryContext) != 0)
    {
        Log("! OpenGL: could not make context current:", SDL_GetError());
        return;
    }

    int version;
    {
        ZoneScopedN("gladLoadGL");
        version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    }
    if (version == 0)
    {
        Log("! OpenGL: could not initialize GLAD.");
        if (const auto err = SDL_GetError())
            Log("SDL Error:", err);
        return;
    }

    if (ThisInstanceIsGlobal())
    {
        UpdateVSync();

#ifdef DEBUG
        if (glDebugMessageCallback)
        {
            CHK_GL(glEnable(GL_DEBUG_OUTPUT));
            CHK_GL(glDebugMessageCallback((GLDEBUGPROC)OnDebugCallback, nullptr));
        }
#endif // DEBUG
    }

    int iMaxVTFUnits, iMaxCTIUnits;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &iMaxVTFUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &iMaxCTIUnits);

    AdapterName = reinterpret_cast<pcstr>(glGetString(GL_RENDERER));
    OpenGLVersionString = reinterpret_cast<pcstr>(glGetString(GL_VERSION));
    ShadingVersion = reinterpret_cast<pcstr>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    Msg("* GPU vendor: [%s] device: [%s]", glGetString(GL_VENDOR), AdapterName);
    Msg("* GPU OpenGL version: %s", OpenGLVersionString);
    Msg("* GPU OpenGL shading language version: %s", ShadingVersion);
    Msg("* GPU OpenGL VTF units: [%d] CTI units: [%d]", iMaxVTFUnits, iMaxCTIUnits);

    ComputeShadersSupported = false; // XXX: Implement compute shaders support

    if (glGenFramebuffers && glBindFramebuffer)
        UpdateViews();
}

void CHW::DestroyDevice()
{
    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;

    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        SDL_GL_MakeCurrent(nullptr, nullptr);

    SDL_GL_DeleteContext(m_context);
    m_context = nullptr;
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////
void CHW::Reset()
{
    ZoneScoped;

    CHK_GL(glDeleteFramebuffers(1, &pFB));
    pFB = 0;
    UpdateViews();

    UpdateVSync();
}

void CHW::SetPrimaryAttributes(u32& windowFlags)
{
    windowFlags |= SDL_WINDOW_OPENGL;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    if (!strstr(Core.Params, "-no_gl_context"))
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    }
}

IRender::RenderContext CHW::GetCurrentContext() const
{
    const auto context = SDL_GL_GetCurrentContext();
    if (context == m_context)
        return IRender::PrimaryContext;
    return IRender::NoContext;
}

int CHW::MakeContextCurrent(IRender::RenderContext context) const
{
    switch (context)
    {
    case IRender::NoContext:
        return SDL_GL_MakeCurrent(nullptr, nullptr);

    case IRender::PrimaryContext:
        return SDL_GL_MakeCurrent(m_window, m_context);

    default:
        NODEFAULT;
    }
    return -1;
}

void CHW::UpdateViews()
{
    // Create the default framebuffer
    glGenFramebuffers(1, &pFB);
    CHK_GL(glBindFramebuffer(GL_FRAMEBUFFER, pFB));

    BackBufferCount = 1;
}

void CHW::BeginScene() { }
void CHW::EndScene() { }

void CHW::Present()
{
    xray::frametime::FrameTimeScope span(Device.FrameTime, xray::frametime::FrameTimePhase::Present);
    // Surface GL errors raised during the frame close to where they happened.
    // CHK_GL is compiled out in Release, so without this drain errors pool in
    // the queue and get blamed on unrelated code (e.g. the next texture load).
    static u32 frame_errors_logged = 0;
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
    {
        if (frame_errors_logged < 100)
        {
            ++frame_errors_logged;
            Msg("! OpenGL: 0x%x: error raised during frame %u%s", err, Device.dwFrame,
                frame_errors_logged == 100 ? " (limit reached, further frame errors muted)" : "");
        }
    }

#if 0 // kept for historical reasons
    RImplementation.Target->phase_flip();
#else
    // The window drawable is not guaranteed to match the render resolution:
    // macOS clamps windows larger than the desktop, borderless windows track
    // the desktop size, and HiDPI drawables differ from window points.
    // Blit with aspect preservation into whatever the drawable really is.
    int drawableW = int(Device.dwWidth), drawableH = int(Device.dwHeight);
    SDL_GL_GetDrawableSize(m_window, &drawableW, &drawableH);
    Device.FrameTime.ObserveDimensions(drawableW, drawableH, true);

    int dstX = 0, dstY = 0, dstW = drawableW, dstH = drawableH;
    if (drawableW != int(Device.dwWidth) || drawableH != int(Device.dwHeight))
    {
        const float srcAspect = float(Device.dwWidth) / float(Device.dwHeight);
        const float dstAspect = float(drawableW) / float(drawableH);
        if (dstAspect > srcAspect) // window wider than render — pillarbox
        {
            dstW = int(float(drawableH) * srcAspect);
            dstX = (drawableW - dstW) / 2;
        }
        else if (dstAspect < srcAspect) // window taller than render — letterbox
        {
            dstH = int(float(drawableW) / srcAspect);
            dstY = (drawableH - dstH) / 2;
        }
    }

    glDisable(GL_SCISSOR_TEST); // scissor clips both the clear and the blit
    glBindFramebuffer(GL_READ_FRAMEBUFFER, pFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    if (dstX != 0 || dstY != 0)
    {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBlitFramebuffer(
        0, 0, Device.dwWidth, Device.dwHeight,
        dstX, dstY, dstX + dstW, dstY + dstH,
        GL_COLOR_BUFFER_BIT,
        (dstW == int(Device.dwWidth) && dstH == int(Device.dwHeight)) ? GL_NEAREST : GL_LINEAR);
#endif

    SDL_GL_SwapWindow(m_window);
    CurrentBackBuffer = (CurrentBackBuffer + 1) % BackBufferCount;
}

DeviceState CHW::GetDeviceState() const
{
#ifdef XR_PLATFORM_APPLE
    // Submitting GL work while the window cannot present can wedge the
    // process inside Apple's GL-on-Metal layer: the Metal command submit
    // blocks in an uninterruptible kernel trap and the process becomes
    // unkillable (not even SIGKILL). Report the device lost while the window
    // is not presentable; the engine skips rendering and polls again.
    const Uint32 flags = SDL_GetWindowFlags(m_window);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))
        return DeviceState::Lost;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(m_window, &info) && info.subsystem == SDL_SYSWM_COCOA)
    {
        if (void* nswindow = info.info.cocoa.window)
        {
            constexpr unsigned long NSWindowOcclusionStateVisible = 1ul << 1;
            const auto occlusion = reinterpret_cast<unsigned long (*)(void*, void*)>(&objc_msgSend)(
                nswindow, sel_registerName("occlusionState"));
            if (!(occlusion & NSWindowOcclusionStateVisible))
                return DeviceState::Lost;
        }
    }
#endif
    return DeviceState::Normal;
}

std::pair<u32, u32> CHW::GetSurfaceSize()
{
    return
    {
        psDeviceMode.Width,
        psDeviceMode.Height
    };
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

void CHW::BeginPixEvent(pcstr name) const
{
    if (glPushDebugGroup)
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void CHW::EndPixEvent() const
{
    if (glPushDebugGroup)
        glPopDebugGroup();
}
} // namespace xray::render::RENDER_NAMESPACE
