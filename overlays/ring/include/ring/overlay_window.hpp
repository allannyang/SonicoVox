#pragma once

#include <functional>
#include <string>
#include <wrl/client.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

namespace sv::overlay::ring {

class OverlayWindow {
public:
    using ResizeCallback = std::function<void(UINT width, UINT height)>;

    OverlayWindow() = default;
    ~OverlayWindow();

    bool initialize(HINSTANCE instance, ResizeCallback onResize);
    void shutdown();
    const std::wstring& lastError() const { return m_lastError; }

    HWND hwnd() const { return m_hwnd; }
    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* context() const { return m_context.Get(); }
    IDXGISwapChain1* swapChain() const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* renderTarget() const { return m_rtv.Get(); }

    bool beginFrame(const float clearColor[4]);
    void present();

    UINT width() const { return m_width; }
    UINT height() const { return m_height; }

private:
    bool createWindow(HINSTANCE instance);
    bool initializeDeviceResources();
    bool createSwapChain();
    bool createRenderTarget();
    void destroyRenderTarget();
    void destroyDirectComposition();
    void updateViewport();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    ResizeCallback m_resizeCallback;

    UINT m_width = 0;
    UINT m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

    bool m_usingDirectComposition = false;
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_dcompVisual;

    std::wstring m_lastError;
};

} // namespace sv::overlay::ring
