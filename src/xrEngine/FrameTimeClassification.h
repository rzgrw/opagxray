#pragma once
#include "FrameTimeSession.h"

namespace xray::frametime
{
enum class FrameTimeWindowVisibility : uint8_t
{
    Visible,
    Unavailable,
    Unknown,
};

// Observe once before BeforeFrame can return for loading/not-ready. The callback
// is never invoked with telemetry disabled; unknown is not evidence of occlusion.
template <typename Observer>
void ClassifyFrameTimeWindow(CFrameTimeSession& session, Observer observe)
{
    if (!session.Running())
        return;
    switch (observe())
    {
    case FrameTimeWindowVisibility::Visible:
        break;
    case FrameTimeWindowVisibility::Unavailable:
        session.Mark(FrameTimeLoopFlags::DeviceUnavailable);
        break;
    case FrameTimeWindowVisibility::Unknown:
        session.Mark(FrameTimeLoopFlags::WindowVisibilityUnknown);
        break;
    }
}
}
