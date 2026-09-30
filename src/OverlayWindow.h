#pragma once

#include <windows.h>
#include <functional>

// Full-virtual-screen, click-through, always-on-top, non-activating popup window
// that hosts the DirectComposition visual tree. It has no redirection surface of
// its own (WS_EX_NOREDIRECTIONBITMAP): everything visible comes from the
// composition swap chain, with per-pixel alpha.
class OverlayWindow
{
public:
    using ResizeCallback = std::function<void(UINT width, UINT height)>;

    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // Creates the (hidden) window covering the virtual screen.
    bool Create(HINSTANCE instance, ResizeCallback onResize);

    HWND Handle() const { return m_hwnd; }
    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

    void Show(bool visible);

    // Re-reads the virtual screen bounds; moves/resizes the window and fires
    // the resize callback if the size changed.
    void RefreshBounds();

    // Re-asserts HWND_TOPMOST without activating.
    void EnsureTopmost();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND           m_hwnd   = nullptr;
    UINT           m_width  = 0;
    UINT           m_height = 0;
    int            m_left   = 0;
    int            m_top    = 0;
    ResizeCallback m_onResize;
};
