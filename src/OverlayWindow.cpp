#include "OverlayWindow.h"

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

namespace
{
    const wchar_t* const kClassName    = L"SoftTempestOverlayWindow";
    const UINT_PTR       kTopmostTimer = 1;
    const UINT           kTopmostMs    = 2000;
}

OverlayWindow::~OverlayWindow()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool OverlayWindow::Create(HINSTANCE instance, ResizeCallback onResize)
{
    m_onResize = std::move(onResize);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &OverlayWindow::WndProcThunk;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // never painted; DirectComposition supplies all pixels
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    m_left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_width  = static_cast<UINT>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    m_height = static_cast<UINT>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (m_width == 0 || m_height == 0)
        return false;

    // WS_EX_NOREDIRECTIONBITMAP : no GDI surface; DirectComposition content only (per-pixel alpha, GPU composed).
    // WS_EX_LAYERED + WS_EX_TRANSPARENT : hit-testing skips this window entirely -> input goes to what is beneath.
    // WS_EX_TOPMOST              : above normal windows.
    // WS_EX_NOACTIVATE           : never takes focus.
    // WS_EX_TOOLWINDOW           : not in taskbar / Alt-Tab.
    const DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                          WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

    m_hwnd = CreateWindowExW(exStyle, kClassName, L"Soft TEMPEST overlay", WS_POPUP,
                             m_left, m_top, static_cast<int>(m_width), static_cast<int>(m_height),
                             nullptr, nullptr, instance, this);
    if (!m_hwnd)
        return false;

    // A layered window is only displayed once its attributes have been set. With
    // WS_EX_NOREDIRECTIONBITMAP there is no GDI surface for this alpha to apply
    // to, so 255 simply means "compose the DirectComposition tree as is".
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    SetTimer(m_hwnd, kTopmostTimer, kTopmostMs, nullptr);
    return true;
}

void OverlayWindow::Show(bool visible)
{
    if (!m_hwnd)
        return;

    if (visible)
    {
        RefreshBounds();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        EnsureTopmost();
    }
    else
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void OverlayWindow::RefreshBounds()
{
    if (!m_hwnd)
        return;

    const int  left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int  top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const UINT width  = static_cast<UINT>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const UINT height = static_cast<UINT>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (width == 0 || height == 0)
        return;

    const bool sizeChanged = (width != m_width) || (height != m_height);
    if (!sizeChanged && left == m_left && top == m_top)
        return;

    m_left = left;  m_top = top;
    m_width = width; m_height = height;

    SetWindowPos(m_hwnd, HWND_TOPMOST, left, top, static_cast<int>(width), static_cast<int>(height),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    if (sizeChanged && m_onResize)
        m_onResize(m_width, m_height);
}

void OverlayWindow::EnsureTopmost()
{
    if (!m_hwnd || !IsWindowVisible(m_hwnd))
        return;
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

LRESULT CALLBACK OverlayWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
        return HTTRANSPARENT;          // belt and braces on top of WS_EX_TRANSPARENT

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;                      // nothing to erase, no GDI surface

    case WM_PAINT:
        ValidateRect(m_hwnd, nullptr);
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        RefreshBounds();
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
            RefreshBounds();
        return 0;

    case WM_TIMER:
        if (wParam == kTopmostTimer)
        {
            RefreshBounds();
            EnsureTopmost();
            return 0;
        }
        break;

    case WM_CLOSE:
        return 0;                      // only the tray "Exit" ends the app

    case WM_DESTROY:
        KillTimer(m_hwnd, kTopmostTimer);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
