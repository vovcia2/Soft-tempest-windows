#pragma once

#include "NoiseConfig.h"

// Persistent settings in HKCU\Software\SoftTempest and the HKCU Run key.
namespace settings
{
    // Loads saved values, falling back to NoiseConfig defaults for anything missing.
    NoiseConfig Load();

    // Persists all fields of the config.
    void Save(const NoiseConfig& config);

    // Autostart via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
    bool IsAutostartEnabled();
    bool SetAutostart(bool enable);
}
