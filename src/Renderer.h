#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <cstdint>
#include <random>
#include <string>

#include "NoiseConfig.h"

// Direct3D 11 + DXGI + DirectComposition renderer.
//
// A composition swap chain (premultiplied alpha, flip sequential) is attached to
// a DirectComposition visual that is the root of the overlay window's target.
// Every frame a full-screen triangle is drawn with the noise pixel shader and a
// fresh random seed, then presented with the configured sync interval.
class Renderer
{
public:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the device, swap chain, composition tree and shaders.
    bool Initialize(HWND hwnd, UINT width, UINT height, std::wstring* error);

    // Releases everything; Initialize() may be called again afterwards.
    void Shutdown();

    bool IsInitialized() const { return m_swapChain != nullptr; }

    // True after a device-removed / reset error: the caller should Shutdown() and Initialize() again.
    bool NeedsReset() const { return m_needsReset; }

    // Resizes swap chain buffers (call when the virtual screen changes).
    void Resize(UINT width, UINT height);

    // Draws one noise frame and presents it.
    void RenderFrame(const NoiseConfig& config);

    // Presents a fully transparent frame (used before hiding the overlay).
    void PresentClear();

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // Must match cbuffer NoiseConstants in noise_ps.hlsl (32 bytes, 16-byte aligned).
    struct alignas(16) NoiseConstants
    {
        float    time;
        float    amplitude;
        int32_t  mode;
        uint32_t seed;
        float    resolution[2];
        float    pad[2];
    };
    static_assert(sizeof(NoiseConstants) == 32, "constant buffer layout must match the HLSL cbuffer");

    bool CreateDevice(std::wstring* error);
    bool CreateSwapChain(UINT width, UINT height, std::wstring* error);
    bool CreateComposition(HWND hwnd, std::wstring* error);
    bool CreateRenderTarget(std::wstring* error);
    bool CreateShaders(std::wstring* error);
    void HandlePresentResult(HRESULT hr);

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain1>        m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader>     m_vs;
    ComPtr<ID3D11PixelShader>      m_ps;
    ComPtr<ID3D11Buffer>           m_constants;

    ComPtr<IDCompositionDevice>    m_dcompDevice;
    ComPtr<IDCompositionTarget>    m_dcompTarget;
    ComPtr<IDCompositionVisual>    m_dcompVisual;

    UINT          m_width      = 0;
    UINT          m_height     = 0;
    bool          m_needsReset = false;
    LARGE_INTEGER m_qpcFreq    = {};
    LARGE_INTEGER m_qpcStart   = {};
    std::mt19937  m_rng;
};
