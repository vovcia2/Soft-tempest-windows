// Soft TEMPEST display-masking overlay for Windows.
//
// WinMain: DPI awareness, single instance, settings, tray icon + menu, global
// hotkey (Ctrl+Alt+N), overlay window and the render loop.

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>

#include "NoiseConfig.h"
#include "OverlayWindow.h"
#include "Renderer.h"
#include "Settings.h"
#include "../res/resource.h"

namespace
{
    // ---- identifiers -------------------------------------------------------
    const wchar_t* const kTrayClassName = L"SoftTempestTrayWindow";
    const wchar_t* const kMutexName     = L"Local\\SoftTempestOverlay.SingleInstance";
    const UINT           kTrayMessage   = WM_APP + 1;
    const UINT           kTrayIconId    = 1;
    const int            kHotkeyId      = 1;

    enum MenuId : UINT
    {
        IDM_TOGGLE = 1000,
        IDM_MODE_WHITE,
        IDM_MODE_HFREQ,
        IDM_MODE_TEMPORAL,
        IDM_MODE_WAVES,
        IDM_MODE_VSTRIPES,
        IDM_AMP_5,
        IDM_AMP_10,
        IDM_AMP_25,
        IDM_AMP_40,
        IDM_AMP_60,
        IDM_SYNC_1,
        IDM_SYNC_2,
        IDM_SYNC_4,
        IDM_AUTOSTART,
        IDM_ABOUT,
        IDM_EXIT,
    };

    struct AmplitudeItem { UINT id; int percent; const wchar_t* label; };
    const AmplitudeItem kAmplitudes[] = {
        { IDM_AMP_5,   5, L"5 %"  },
        { IDM_AMP_10, 10, L"10 %" },
        { IDM_AMP_25, 25, L"25 %" },
        { IDM_AMP_40, 40, L"40 %" },
        { IDM_AMP_60, 60, L"60 %" },
    };

    // ---- application state -------------------------------------------------
    struct App
    {
        HINSTANCE     instance      = nullptr;
        HWND          trayHwnd      = nullptr;
        UINT          taskbarCreated = 0;
        NoiseConfig   config;
        OverlayWindow overlay;
        Renderer      renderer;
        bool          rendererFailed = false;
        std::wstring  rendererError;
    };

    App g_app;

    // ---- helpers -----------------------------------------------------------
    void EnablePerMonitorV2()
    {
        // The manifest already declares PerMonitorV2; this is the programmatic
        // equivalent for builds/launchers that lose the manifest. Resolved at
        // run time so the binary still starts on older Windows 10 builds.
        using SetCtxFn = BOOL (WINAPI*)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32)
            return;
        FARPROC raw = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (!raw)
            return;
        auto setCtx = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(raw));
        if (setCtx)
            setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }

    const wchar_t* ModeName(NoiseMode mode)
    {
        switch (mode)
        {
        case NoiseMode::HFreqHorizontal: return L"HF horizontal";
        case NoiseMode::Temporal:        return L"temporal";
        case NoiseMode::Waves:           return L"waves";
        case NoiseMode::VerticalStripes: return L"vertical stripes";
        default:                         return L"white";
        }
    }

    std::wstring TooltipText()
    {
        wchar_t buf[128];
        _snwprintf_s(buf, _TRUNCATE, L"Soft TEMPEST: %s  (%s, %d%%)  Ctrl+Alt+N",
                     g_app.config.running ? L"ON" : L"OFF",
                     ModeName(g_app.config.mode), AmplitudeToPercent(g_app.config.amplitude));
        return buf;
    }

    void FillTrayIconData(NOTIFYICONDATAW& nid)
    {
        nid = {};
        nid.cbSize           = sizeof(nid);
        nid.hWnd             = g_app.trayHwnd;
        nid.uID              = kTrayIconId;
        nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = kTrayMessage;
        nid.hIcon            = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP));
        std::wstring tip = TooltipText();
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    }

    void AddTrayIcon()
    {
        NOTIFYICONDATAW nid;
        FillTrayIconData(nid);
        Shell_NotifyIconW(NIM_ADD, &nid);
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }

    void UpdateTrayIcon()
    {
        NOTIFYICONDATAW nid;
        FillTrayIconData(nid);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void RemoveTrayIcon()
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = g_app.trayHwnd;
        nid.uID    = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void SaveConfig()
    {
        settings::Save(g_app.config);
        UpdateTrayIcon();
    }

    void SetRunning(bool running)
    {
        if (g_app.config.running == running)
            return;
        g_app.config.running = running;

        if (running)
        {
            g_app.overlay.Show(true);
        }
        else
        {
            g_app.renderer.PresentClear();
            g_app.overlay.Show(false);
        }
        SaveConfig();
    }

    void ShowAbout()
    {
        const wchar_t* text =
            L"Soft TEMPEST overlay " SOFTTEMPEST_VERSION_STR L"\n"
            L"Full-screen, click-through noise layer re-randomised every frame\n"
            L"(soft TEMPEST after Kuhn & Anderson, 1998).\n\n"
            L"Known limitations - please read:\n"
            L"• Secure Desktop (UAC prompts, Ctrl+Alt+Del, logon, Windows Hello) is NOT covered.\n"
            L"• DRM / protected video (OPM, PlayReady) may bypass the overlay.\n"
            L"• Exclusive full-screen applications can bypass DWM and the overlay.\n"
            L"• Noise is re-drawn every frame because static noise averages away as sqrt(N).\n"
            L"• No measurement, no proof: this is a prototype that complements hardware\n"
            L"   shielding. It is not a replacement for it.\n"
            L"• Continuous GPU rendering costs power; lower the frame rate on laptops.\n\n"
            L"Hotkey: Ctrl+Alt+N toggles the overlay.\n"
            L"Settings: HKCU\\Software\\SoftTempest";
        MessageBoxW(nullptr, text, L"About Soft TEMPEST overlay", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }

    void ShowTrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        HMENU modeMenu = CreatePopupMenu();
        HMENU ampMenu  = CreatePopupMenu();
        HMENU rateMenu = CreatePopupMenu();

        AppendMenuW(menu, MF_STRING, IDM_TOGGLE,
                    g_app.config.running ? L"Stop overlay\tCtrl+Alt+N" : L"Start overlay\tCtrl+Alt+N");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(modeMenu, MF_STRING, IDM_MODE_WHITE,    L"White noise");
        AppendMenuW(modeMenu, MF_STRING, IDM_MODE_HFREQ,    L"High-frequency horizontal");
        AppendMenuW(modeMenu, MF_STRING, IDM_MODE_TEMPORAL, L"Temporal (random alpha)");
        AppendMenuW(modeMenu, MF_STRING, IDM_MODE_WAVES,    L"Horizontal waves (drifting)");
        AppendMenuW(modeMenu, MF_STRING, IDM_MODE_VSTRIPES, L"Soft vertical stripes");
        const UINT modeId = g_app.config.mode == NoiseMode::HFreqHorizontal ? IDM_MODE_HFREQ
                          : g_app.config.mode == NoiseMode::Temporal        ? IDM_MODE_TEMPORAL
                          : g_app.config.mode == NoiseMode::Waves           ? IDM_MODE_WAVES
                          : g_app.config.mode == NoiseMode::VerticalStripes ? IDM_MODE_VSTRIPES
                          : IDM_MODE_WHITE;
        CheckMenuRadioItem(modeMenu, IDM_MODE_WHITE, IDM_MODE_VSTRIPES, modeId, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modeMenu), L"Mode");

        const int currentPercent = AmplitudeToPercent(g_app.config.amplitude);
        for (const AmplitudeItem& item : kAmplitudes)
        {
            AppendMenuW(ampMenu, MF_STRING, item.id, item.label);
            if (item.percent == currentPercent)
                CheckMenuRadioItem(ampMenu, IDM_AMP_5, IDM_AMP_60, item.id, MF_BYCOMMAND);
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ampMenu), L"Amplitude");

        AppendMenuW(rateMenu, MF_STRING, IDM_SYNC_1, L"Every vblank (full refresh rate)");
        AppendMenuW(rateMenu, MF_STRING, IDM_SYNC_2, L"Every 2nd vblank (half)");
        AppendMenuW(rateMenu, MF_STRING, IDM_SYNC_4, L"Every 4th vblank (quarter)");
        const UINT rateId = g_app.config.syncInterval >= 4 ? IDM_SYNC_4
                          : g_app.config.syncInterval >= 2 ? IDM_SYNC_2
                          : IDM_SYNC_1;
        CheckMenuRadioItem(rateMenu, IDM_SYNC_1, IDM_SYNC_4, rateId, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rateMenu), L"Frame rate");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (settings::IsAutostartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART,
                    L"Start with Windows");
        AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About / limitations...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_app.trayHwnd);   // required so the menu closes when focus is lost
        TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, g_app.trayHwnd, nullptr);
        PostMessageW(g_app.trayHwnd, WM_NULL, 0, 0);

        DestroyMenu(menu);   // destroys sub-menus too
    }

    void HandleCommand(UINT id)
    {
        switch (id)
        {
        case IDM_TOGGLE:
            SetRunning(!g_app.config.running);
            break;

        case IDM_MODE_WHITE:    g_app.config.mode = NoiseMode::White;           SaveConfig(); break;
        case IDM_MODE_HFREQ:    g_app.config.mode = NoiseMode::HFreqHorizontal; SaveConfig(); break;
        case IDM_MODE_TEMPORAL: g_app.config.mode = NoiseMode::Temporal;        SaveConfig(); break;
        case IDM_MODE_WAVES:    g_app.config.mode = NoiseMode::Waves;           SaveConfig(); break;
        case IDM_MODE_VSTRIPES: g_app.config.mode = NoiseMode::VerticalStripes; SaveConfig(); break;

        case IDM_SYNC_1: g_app.config.syncInterval = 1; SaveConfig(); break;
        case IDM_SYNC_2: g_app.config.syncInterval = 2; SaveConfig(); break;
        case IDM_SYNC_4: g_app.config.syncInterval = 4; SaveConfig(); break;

        case IDM_AUTOSTART:
            settings::SetAutostart(!settings::IsAutostartEnabled());
            break;

        case IDM_ABOUT:
            ShowAbout();
            break;

        case IDM_EXIT:
            PostQuitMessage(0);
            break;

        default:
            for (const AmplitudeItem& item : kAmplitudes)
            {
                if (item.id == id)
                {
                    g_app.config.amplitude = PercentToAmplitude(item.percent);
                    SaveConfig();
                    break;
                }
            }
            break;
        }
    }

    LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == kTrayMessage)
        {
            switch (LOWORD(lParam))
            {
            case WM_CONTEXTMENU:
            case WM_RBUTTONUP:
                ShowTrayMenu();
                return 0;
            case NIN_SELECT:
            case NIN_KEYSELECT:
            case WM_LBUTTONDBLCLK:
                SetRunning(!g_app.config.running);
                return 0;
            default:
                return 0;
            }
        }

        if (g_app.taskbarCreated != 0 && msg == g_app.taskbarCreated)
        {
            AddTrayIcon();   // Explorer restarted: re-register the icon
            return 0;
        }

        switch (msg)
        {
        case WM_COMMAND:
            HandleCommand(LOWORD(wParam));
            return 0;

        case WM_HOTKEY:
            if (wParam == static_cast<WPARAM>(kHotkeyId))
                SetRunning(!g_app.config.running);
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            g_app.overlay.RefreshBounds();
            return 0;

        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool CreateTrayWindow()
    {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = TrayWndProc;
        wc.hInstance     = g_app.instance;
        wc.lpszClassName = kTrayClassName;
        wc.hIcon         = LoadIconW(g_app.instance, MAKEINTRESOURCEW(IDI_APP));
        if (!RegisterClassExW(&wc))
            return false;

        // A hidden, ordinary top-level window (not message-only) so that
        // SetForegroundWindow/TrackPopupMenu behave.
        g_app.trayHwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClassName, L"Soft TEMPEST", WS_POPUP,
                                         0, 0, 0, 0, nullptr, nullptr, g_app.instance, nullptr);
        return g_app.trayHwnd != nullptr;
    }

    bool InitRenderer()
    {
        std::wstring error;
        if (!g_app.renderer.Initialize(g_app.overlay.Handle(), g_app.overlay.Width(), g_app.overlay.Height(), &error))
        {
            g_app.rendererFailed = true;
            g_app.rendererError  = error;
            return false;
        }
        g_app.rendererFailed = false;
        return true;
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    EnablePerMonitorV2();
    g_app.instance = instance;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"Soft TEMPEST overlay is already running (see the tray icon).",
                    L"Soft TEMPEST overlay", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    g_app.config = settings::Load();

    if (!CreateTrayWindow())
    {
        MessageBoxW(nullptr, L"Failed to create the tray window.", L"Soft TEMPEST overlay", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();

    if (!RegisterHotKey(g_app.trayHwnd, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N'))
    {
        // Not fatal: the tray menu still works.
    }

    if (!g_app.overlay.Create(instance, [](UINT width, UINT height) { g_app.renderer.Resize(width, height); }))
    {
        RemoveTrayIcon();
        MessageBoxW(nullptr, L"Failed to create the overlay window.", L"Soft TEMPEST overlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!InitRenderer())
    {
        RemoveTrayIcon();
        std::wstring text = L"Failed to initialise Direct3D 11 / DirectComposition:\n\n" + g_app.rendererError;
        MessageBoxW(nullptr, text.c_str(), L"Soft TEMPEST overlay", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_app.overlay.Show(g_app.config.running);
    UpdateTrayIcon();

    // Render loop: pump all pending messages, then draw one frame when running
    // (Present with sync interval >= 1 paces the loop to vblank). When stopped,
    // block until the next message so the process is idle.
    MSG msg = {};
    for (;;)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto quit;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_app.config.running)
        {
            if (g_app.renderer.NeedsReset())
            {
                g_app.renderer.Shutdown();
                if (!InitRenderer())
                {
                    // Give the driver a moment before trying again.
                    MsgWaitForMultipleObjects(0, nullptr, FALSE, 1000, QS_ALLINPUT);
                    continue;
                }
            }
            g_app.renderer.RenderFrame(g_app.config);
        }
        else
        {
            WaitMessage();
        }
    }

quit:
    UnregisterHotKey(g_app.trayHwnd, kHotkeyId);
    RemoveTrayIcon();
    g_app.renderer.Shutdown();
    if (mutex)
        CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
