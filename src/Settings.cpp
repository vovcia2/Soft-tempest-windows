#include "Settings.h"

#include <windows.h>
#include <string>

namespace
{
    const wchar_t* const kSettingsKey = L"Software\\SoftTempest";
    const wchar_t* const kRunKey      = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* const kRunValue    = L"SoftTempest";

    DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback)
    {
        DWORD value = 0;
        DWORD size  = sizeof(value);
        DWORD type  = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(value))
        {
            return value;
        }
        return fallback;
    }

    void WriteDword(HKEY key, const wchar_t* name, DWORD value)
    {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    std::wstring QuotedExePath()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return L"\"" + std::wstring(path) + L"\"";
    }
}

namespace settings
{
    NoiseConfig Load()
    {
        NoiseConfig config;

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return config;

        config.amplitude = PercentToAmplitude(static_cast<int>(
            ReadDword(key, L"AmplitudePercent", static_cast<DWORD>(AmplitudeToPercent(config.amplitude)))));

        DWORD mode = ReadDword(key, L"Mode", static_cast<DWORD>(config.mode));
        if (mode <= static_cast<DWORD>(NoiseMode::Waves))
            config.mode = static_cast<NoiseMode>(mode);

        DWORD sync = ReadDword(key, L"SyncInterval", static_cast<DWORD>(config.syncInterval));
        if (sync >= 1 && sync <= 4)
            config.syncInterval = static_cast<int>(sync);

        config.running = ReadDword(key, L"Enabled", config.running ? 1 : 0) != 0;

        RegCloseKey(key);
        return config;
    }

    void Save(const NoiseConfig& config)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        {
            return;
        }

        WriteDword(key, L"AmplitudePercent", static_cast<DWORD>(AmplitudeToPercent(config.amplitude)));
        WriteDword(key, L"Mode",             static_cast<DWORD>(config.mode));
        WriteDword(key, L"SyncInterval",     static_cast<DWORD>(config.syncInterval));
        WriteDword(key, L"Enabled",          config.running ? 1u : 0u);

        RegCloseKey(key);
    }

    bool IsAutostartEnabled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;

        DWORD type = 0;
        DWORD size = 0;
        bool present = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
                       type == REG_SZ;
        RegCloseKey(key);
        return present;
    }

    bool SetAutostart(bool enable)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        {
            return false;
        }

        LSTATUS status;
        if (enable)
        {
            std::wstring cmd = QuotedExePath();
            status = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(cmd.c_str()),
                                    static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            status = RegDeleteValueW(key, kRunValue);
            if (status == ERROR_FILE_NOT_FOUND)
                status = ERROR_SUCCESS;
        }

        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
}
