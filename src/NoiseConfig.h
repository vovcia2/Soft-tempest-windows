#pragma once

// Shared, user-controllable state of the overlay.
// Written by the tray UI / hotkey handler, read by the renderer every frame.

enum class NoiseMode : int
{
    White           = 0,  // independent white noise, uniform alpha
    HFreqHorizontal = 1,  // energy concentrated at the horizontal pixel-clock frequency
    Temporal        = 2,  // white noise + per-pixel, per-frame random alpha
};

struct NoiseConfig
{
    float     amplitude    = 0.25f;            // overlay opacity 0..1
    NoiseMode mode         = NoiseMode::White;
    int       syncInterval = 1;                // Present() sync interval: 1 = every vblank, 2 = every 2nd, ...
    bool      running      = true;             // overlay visible and rendering
};

inline int AmplitudeToPercent(float amplitude)
{
    int p = static_cast<int>(amplitude * 100.0f + 0.5f);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

inline float PercentToAmplitude(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return static_cast<float>(percent) / 100.0f;
}
