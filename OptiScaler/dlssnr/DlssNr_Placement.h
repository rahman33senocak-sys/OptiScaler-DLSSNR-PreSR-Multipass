#pragma once

namespace DlssNr
{
struct Placement
{
    bool beforeUpscale;
    bool deferred;
    bool finished;
};

// A display swapchain Present is not an app-frame boundary while frame generation is active.
// Applying an RGB edit captured from one real frame to a generated/different frame produces
// severe colour contamination. Only the native Streamline app-buffer handoff has an exact
// game-frame identity; otherwise keep composition at the matched SR seam.
constexpr bool AllowFinishedPicture(bool requested, bool frameGenerationActive, bool gameFrameHandoff)
{
    return requested && (!frameGenerationActive || gameFrameHandoff);
}

// The old across-RR key is accepted as an alias for the unified private-upscaler route.
constexpr Placement ResolvePlacement(bool before, bool deferred, bool legacyAcrossRr, bool finished)
{
    deferred = deferred || (before && legacyAcrossRr) || (finished && before);
    return { before || deferred, deferred, finished };
}
} // namespace DlssNr
