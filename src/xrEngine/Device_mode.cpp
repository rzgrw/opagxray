#include "stdafx.h"

#include "xrCore/xr_token.h"
#include "xr_input.h"
#include "AgentBridge.h"

namespace
{
// -agent_background: park the window just left of the leftmost display. It stays
// a real, shown, non-minimized window (GL keeps rendering and presenting) but
// never covers any part of the user's desktop. SDL moves it with SWP_NOACTIVATE.
void PlaceBackgroundWindow(SDL_Window* window)
{
    int minX = 0, minY = 0;
    for (int i = 0, n = SDL_GetNumVideoDisplays(); i < n; ++i)
    {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(i, &r) == 0)
        {
            minX = std::min(minX, r.x);
            minY = std::min(minY, r.y);
        }
    }
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_SetWindowPosition(window, minX - w - 64, minY);
}
} // namespace

xr_vector<xr_token> vid_monitor_token;
xr_map<u32, xr_vector<xr_token>> vid_mode_token;

void FillResolutionsForMonitor(const int monitorID)
{
    const int modeCount = SDL_GetNumDisplayModes(monitorID);
    R_ASSERT3(modeCount > 0, "Failed to find display modes", SDL_GetError());

    for (int i = modeCount - 1; i >= 0; --i)
    {
        SDL_DisplayMode mode;
        const int result = SDL_GetDisplayMode(monitorID, i, &mode);
        R_ASSERT3(result == 0, "Failed to find specified display mode", SDL_GetError());

        string256 buf;
        xr_sprintf(buf, sizeof(buf), "%ux%u (%dHz)", mode.w, mode.h, mode.refresh_rate);
        vid_mode_token[monitorID].emplace_back(xr_strdup(buf), i);
    }

    vid_mode_token[monitorID].emplace_back(nullptr, -1);
}

void FillImGuiMonitorData(const int monitorID)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    // Warning: the validity of monitor DPI information on Windows
    // depends on the application DPI awareness settings,
    // which generally needs to be set in the manifest or at runtime.
    ImGuiPlatformMonitor monitor;
    SDL_Rect r;
    SDL_GetDisplayBounds(monitorID, &r);
    monitor.MainPos = monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
    monitor.MainSize = monitor.WorkSize = ImVec2((float)r.w, (float)r.h);

    SDL_GetDisplayUsableBounds(monitorID, &r);
    monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
    monitor.WorkSize = ImVec2((float)r.w, (float)r.h);

    // FIXME-VIEWPORT: On MacOS SDL reports actual monitor DPI scale, ignoring OS configuration. We may want to set
    //  DpiScale to cocoa_window.backingScaleFactor here.
    float dpi = 0.0f;
    if (!SDL_GetDisplayDPI(monitorID, &dpi, nullptr, nullptr))
        monitor.DpiScale = dpi / 96.0f;

    monitor.PlatformHandle = (void*)(intptr_t)monitorID;
    platform_io.Monitors.push_back(monitor);
}

void CRenderDevice::FillVideoModes()
{
    ZoneScoped;

    const int displayCount = SDL_GetNumVideoDisplays();
    R_ASSERT3(displayCount > 0, "Failed to find display", SDL_GetError());

    for (int i = 0; i < displayCount; ++i)
    {
        string256 buf;
        xr_sprintf(buf, "%d. %s", i, SDL_GetDisplayName(i));
        vid_monitor_token.emplace_back(xr_strdup(buf), i);

        FillResolutionsForMonitor(i);
        FillImGuiMonitorData(i);
    }
    vid_monitor_token.emplace_back(nullptr, -1);
}

void CRenderDevice::CleanupVideoModes()
{
    ZoneScoped;

    for (auto& [monitor_id, tokens] : vid_mode_token)
    {
        for (auto& token : tokens)
        {
            auto tokenName = const_cast<pstr>(token.name);
            xr_free(tokenName);
        }
        tokens.clear();
    }
    vid_mode_token.clear();

    for (auto& token : vid_monitor_token)
    {
        pstr tokenName = const_cast<pstr>(token.name);
        xr_free(tokenName);
    }
    vid_monitor_token.clear();

    ImGui::GetPlatformIO().Monitors.resize(0);
}

void CRenderDevice::SetWindowDraggable(bool draggable)
{
    // Only draggable if resizable too
    const bool windowed = psDeviceMode.WindowStyle == rsWindowed || psDeviceMode.WindowStyle == rsWindowedBorderless;
    const bool resizable = SDL_GetWindowFlags(Device.m_sdlWnd) & SDL_WINDOW_RESIZABLE;
    m_allowWindowDrag = draggable && windowed && resizable;

    SDL_SetWindowOpacity(Device.m_sdlWnd, m_allowWindowDrag ? 0.95f : 1.0f);
}

void CRenderDevice::UpdateWindowProps()
{
    ZoneScoped;

    // Displays can disappear at any time (unplug, sleep, topology change) —
    // never index SDL display arrays with a stale monitor number.
    const int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount > 0 && static_cast<int>(psDeviceMode.Monitor) >= displayCount)
    {
        Msg("~ Monitor %u no longer exists (%d connected), falling back to the primary display",
            psDeviceMode.Monitor, displayCount);
        psDeviceMode.Monitor = 0;
    }

    if (AgentBackgroundMode() && psDeviceMode.WindowStyle != rsWindowed &&
        psDeviceMode.WindowStyle != rsWindowedBorderless)
    {
        Msg("* agent_background: forcing windowed mode (no fullscreen, no display mode change)");
        psDeviceMode.WindowStyle = rsWindowedBorderless;
    }

    const bool windowed = psDeviceMode.WindowStyle != rsFullscreen;
    SelectResolution(windowed);

    // Changing monitor, unset fullscreen for the previous monitor
    // and move the window to the new monitor
    const int windowDisplay = SDL_GetWindowDisplayIndex(m_sdlWnd);
    if (windowDisplay >= 0 && windowDisplay != static_cast<int>(psDeviceMode.Monitor))
    {
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_DISABLE);

        SDL_Rect rect;
        if (SDL_GetDisplayBounds(psDeviceMode.Monitor, &rect) == 0)
            SDL_SetWindowPosition(m_sdlWnd, rect.x, rect.y);
        else
            Msg("! SDL_GetDisplayBounds(%u) failed: %s", psDeviceMode.Monitor, SDL_GetError());
    }

    if (psDeviceMode.WindowStyle != rsFullscreenBorderless)
    {
        // A window larger than the desktop gets clamped by the OS (macOS
        // constrains it to the screen), leaving the drawable smaller than the
        // engine assumes. Scale the window down to fit, preserving aspect —
        // rendering stays at the requested resolution and the present blit
        // scales it into the actual drawable.
        int w = int(psDeviceMode.Width), h = int(psDeviceMode.Height);
        SDL_Rect usable;
#ifdef XR_PLATFORM_WINDOWS
        // Windows does not clamp windows to the work area (the title bar may
        // go off-screen, the taskbar may be covered), so only a window larger
        // than the whole display needs scaling. Measuring against the work
        // area turned the desktop resolution into an odd smaller mode
        // (2560x1440 -> 2453x1380 with a 60 px taskbar).
        const int boundsResult = SDL_GetDisplayBounds(psDeviceMode.Monitor, &usable);
#else
        const int boundsResult = SDL_GetDisplayUsableBounds(psDeviceMode.Monitor, &usable);
#endif
        m_scaledWindowSize = {};
        if (boundsResult == 0 && (w > usable.w || h > usable.h))
        {
            const float scale = std::min(float(usable.w) / float(w), float(usable.h) / float(h));
            w = int(float(w) * scale);
            h = int(float(h) * scale);
            m_scaledWindowSize = { w, h };
            Msg("~ Window %ux%u exceeds display %u area %dx%d, window scaled to %dx%d",
                psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.Monitor, usable.w, usable.h, w, h);
        }

        SDL_SetWindowSize(m_sdlWnd, w, h);

        if (w != int(psDeviceMode.Width)) // was clamped — keep it fully on screen
            SDL_SetWindowPosition(m_sdlWnd,
                SDL_WINDOWPOS_CENTERED_DISPLAY(psDeviceMode.Monitor),
                SDL_WINDOWPOS_CENTERED_DISPLAY(psDeviceMode.Monitor));
    }
    else
    {
        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &current);

        SDL_SetWindowSize(m_sdlWnd, current.w, current.h);
    }

    if (windowed)
    {
        const bool drawBorders = psDeviceMode.WindowStyle == rsWindowed;
        const bool useDesktopFullscreen = b_is_Ready && psDeviceMode.WindowStyle == rsFullscreenBorderless;

        SDL_SetWindowBordered(m_sdlWnd, drawBorders ? SDL_TRUE : SDL_FALSE);
        SDL_SetWindowResizable(m_sdlWnd, !useDesktopFullscreen ? SDL_TRUE : SDL_FALSE);
        SDL_SetWindowFullscreen(m_sdlWnd, useDesktopFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_DISABLE);
    }
    else if (b_is_Ready)
    {
        SDL_SetWindowResizable(m_sdlWnd, SDL_FALSE);
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_WINDOW_FULLSCREEN);

        SDL_DisplayMode mode;
        SDL_GetWindowDisplayMode(m_sdlWnd, &mode);
        mode.w = psDeviceMode.Width;
        mode.h = psDeviceMode.Height;
        mode.refresh_rate = psDeviceMode.RefreshRate;
        SDL_SetWindowDisplayMode(m_sdlWnd, &mode);
    }

    if (AgentBackgroundMode())
        PlaceBackgroundWindow(m_sdlWnd);

    SDL_PumpEvents();
    UpdateWindowRects();

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = { static_cast<float>(psDeviceMode.Width), static_cast<float>(psDeviceMode.Height) };
    if (m_rcWindowClient.w > 0 && m_rcWindowClient.h > 0)
        io.DisplayFramebufferScale = ImVec2{ float(dwWidth) / float(m_rcWindowClient.w), float(dwHeight) / float(m_rcWindowClient.h) };
}

void CRenderDevice::UpdateWindowRects()
{
    m_rcWindowClient.x = 0;
    m_rcWindowClient.y = 0;
    SDL_GetWindowSize(m_sdlWnd, &m_rcWindowClient.w, &m_rcWindowClient.h);

    SDL_GetWindowPosition(m_sdlWnd, &m_rcWindowBounds.x, &m_rcWindowBounds.y);
    SDL_GetWindowSize(m_sdlWnd, &m_rcWindowBounds.w, &m_rcWindowBounds.h);

    int top, left, bottom, right;
    SDL_GetWindowBordersSize(m_sdlWnd, &top, &left, &bottom, &right);
    m_rcWindowBounds.x -= left;
    m_rcWindowBounds.y -= top;
    m_rcWindowBounds.w += right;
    m_rcWindowBounds.h += bottom;
}

void CRenderDevice::SelectResolution(const bool windowed)
{
    // Dedicated server hardcoded resolution
    // XXX: to be removed
    if (GEnv.isDedicatedServer)
    {
        psDeviceMode.Width = 640;
        psDeviceMode.Height = 480;
    }
    else if (psDeviceMode.Width == 0 && psDeviceMode.Height == 0 && psDeviceMode.RefreshRate == 0)
    {
        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &current);
        psDeviceMode.Width = current.w;
        psDeviceMode.Height = current.h;
        psDeviceMode.RefreshRate = current.refresh_rate;
    }
    else if (!windowed) // check if safe for fullscreen
    {
        string256 buf;
        xr_sprintf(buf, "%ux%u (%dHz)", psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.RefreshRate);

        auto modes = vid_mode_token[psDeviceMode.Monitor];
        const auto it = std::find_if(modes.begin(), modes.end(), [&buf](const xr_token& token)
        {
            return token.name && xr_strcmp(token.name, buf) == 0;
        });

        if (it == modes.end()) // not found
        {
            SDL_DisplayMode current =
            {
                SDL_PIXELFORMAT_UNKNOWN,
                (int)psDeviceMode.Width,
                (int)psDeviceMode.Height,
                (int)psDeviceMode.RefreshRate,
                nullptr
            };

            SDL_DisplayMode closest; // try closest or fallback to desktop mode
            if (!SDL_GetClosestDisplayMode(psDeviceMode.Monitor, &current, &closest))
            {
                SDL_GetCurrentDisplayMode(psDeviceMode.Monitor, &closest);
            }

            psDeviceMode.Width = closest.w;
            psDeviceMode.Height = closest.h;
            psDeviceMode.RefreshRate = closest.refresh_rate;
        }
    }

    dwWidth = psDeviceMode.Width;
    dwHeight = psDeviceMode.Height;
}

SDL_Window* CRenderDevice::GetApplicationWindow()
{
    return m_sdlWnd;
}

void CRenderDevice::OnErrorDialog(bool beforeDialog)
{
    const bool restore = !beforeDialog;
    const bool needUpdateInput = pInput && pInput->IsExclusiveMode();

    if (restore)
    {
#ifdef XR_PLATFORM_APPLE
        SDL_RestoreWindow(m_sdlWnd);
#endif
        UpdateWindowProps();
    }
    else
    {
        SDL_SetWindowFullscreen(m_sdlWnd, SDL_FALSE);
#ifdef XR_PLATFORM_APPLE
        // Leaving fullscreen is asynchronous on macOS: the dead window keeps
        // covering the screen and the error dialog opens behind it, unreachable.
        // Get the window fully out of the way so the dialog can be seen.
        SDL_SetWindowAlwaysOnTop(m_sdlWnd, SDL_FALSE);
        SDL_MinimizeWindow(m_sdlWnd);
#endif
    }

    if (needUpdateInput)
        pInput->GrabInput(restore);
}

void CRenderDevice::OnFatalError()
{
    // The OS does not release a grabbed cursor for us — without this the
    // error dialog can be visible yet unclickable.
    if (pInput)
        pInput->GrabInput(false);

    // make it sure window will hide in any way
    SDL_SetWindowFullscreen(m_sdlWnd, SDL_FALSE);
    SDL_SetWindowAlwaysOnTop(m_sdlWnd, SDL_FALSE);
    SDL_ShowWindow(m_sdlWnd);
    SDL_MinimizeWindow(m_sdlWnd);
    SDL_HideWindow(m_sdlWnd);
}
