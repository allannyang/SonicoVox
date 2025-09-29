#include "ring/overlay_window.hpp"

#include <cwchar>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace sv::overlay::ring {
namespace {
    constexpr wchar_t kWindowClassName[] = L"SVOverlayRingWindow";

    std::wstring HexDWORD(DWORD value) {
        wchar_t buffer[16];
        swprintf_s(buffer, L"0x%08X", static_cast<unsigned>(value));
        return buffer;
    }

    std::wstring DescribeSystemError(const wchar_t* context, DWORD error) {
        std::wstring message = context;
        message += L" (code ";
        message += HexDWORD(error);
        message += L")";
        return message;
    }

    std::wstring DescribeHResult(const wchar_t* context, HRESULT hr) {
        std::wstring message = context;
        message += L" (hr ";
        message += HexDWORD(static_cast<DWORD>(hr));
        message += L")";
        return message;
    }
}

OverlayWindow::~OverlayWindow() {
    shutdown();
}

bool OverlayWindow::initialize(HINSTANCE instance, ResizeCallback onResize) {
    m_lastError.clear();
    m_instance = instance;
    m_resizeCallback = std::move(onResize);

    if (!createWindow(instance)) {
        if (m_lastError.empty()) {
            m_lastError = L"CreateWindowExW failed.";
        }
        return false;
    }

    if (!initializeDeviceResources()) {
        if (m_lastError.empty()) {
            m_lastError = L"Failed to initialize Direct3D device.";
        }
        return false;
    }

    if (!createRenderTarget()) {
        if (m_lastError.empty()) {
            m_lastError = L"Failed to create render target.";
        }
        return false;
    }

    updateViewport();

    if (m_resizeCallback) {
        m_resizeCallback(m_width, m_height);
    }

    return true;
}

void OverlayWindow::shutdown() {
    destroyRenderTarget();
    destroyDirectComposition();

    if (m_swapChain) {
        m_swapChain->SetFullscreenState(FALSE, nullptr);
        m_swapChain.Reset();
    }

    m_context.Reset();
    m_device.Reset();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    if (m_instance) {
        UnregisterClassW(kWindowClassName, m_instance);
        m_instance = nullptr;
    }
}

bool OverlayWindow::beginFrame(const float clearColor[4]) {
    if (!m_context || !m_rtv) {
        return false;
    }

    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    m_context->RSSetViewports(1, &viewport);

    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    return true;
}

void OverlayWindow::present() {
    if (!m_swapChain) {
        return;
    }

    if (SUCCEEDED(m_swapChain->Present(1, 0)) && m_usingDirectComposition && m_dcompDevice) {
        m_dcompDevice->Commit();
    }
}

bool OverlayWindow::createWindow(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;

    ATOM atom = RegisterClassExW(&wc);
    if (!atom) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            m_lastError = DescribeSystemError(L"RegisterClassExW failed.", error);
            return false;
        }
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        m_lastError = DescribeSystemError(L"GetMonitorInfoW failed.", GetLastError());
        return false;
    }

    m_width = static_cast<UINT>(monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left);
    m_height = static_cast<UINT>(monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;
    DWORD style = WS_POPUP;

    m_hwnd = CreateWindowExW(exStyle, kWindowClassName, L"SonicoVox Ring Overlay", style,
                             monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                             static_cast<int>(m_width), static_cast<int>(m_height),
                             nullptr, nullptr, instance, this);
    if (!m_hwnd) {
        m_lastError = DescribeSystemError(L"CreateWindowExW failed.", GetLastError());
        return false;
    }

    if (!SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA)) {
        m_lastError = DescribeSystemError(L"SetLayeredWindowAttributes failed.", GetLastError());
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwnd);

    return true;
}

bool OverlayWindow::initializeDeviceResources() {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL selectedLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
                                   featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
                                   &device, &selectedLevel, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
                               featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
                               &device, &selectedLevel, &context);
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"D3D11CreateDevice failed.", hr);
            return false;
        }
    }

    m_device = device;
    m_context = context;

    return createSwapChain();
}

bool OverlayWindow::createSwapChain() {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"Querying IDXGIDevice failed.", hr);
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"IDXGIDevice::GetAdapter failed.", hr);
        return false;
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"IDXGIAdapter::GetParent failed.", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(m_device.Get(), &desc, nullptr, &m_swapChain);
    if (SUCCEEDED(hr)) {
        hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice));
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"DCompositionCreateDevice failed.", hr);
            return false;
        }

        hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"IDCompositionDevice::CreateTargetForHwnd failed.", hr);
            return false;
        }

        hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"IDCompositionDevice::CreateVisual failed.", hr);
            return false;
        }

        hr = m_dcompVisual->SetContent(m_swapChain.Get());
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"IDCompositionVisual::SetContent failed.", hr);
            return false;
        }

        hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"IDCompositionTarget::SetRoot failed.", hr);
            return false;
        }

        hr = m_dcompDevice->Commit();
        if (FAILED(hr)) {
            m_lastError = DescribeHResult(L"IDCompositionDevice::Commit failed.", hr);
            return false;
        }

        m_usingDirectComposition = true;
        return true;
    }

    // Fallback path without DirectComposition.
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &desc, nullptr, nullptr, &m_swapChain);
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"IDXGIFactory2::CreateSwapChainForHwnd failed.", hr);
        return false;
    }

    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    m_usingDirectComposition = false;
    return true;
}

bool OverlayWindow::createRenderTarget() {
    if (!m_swapChain) {
        m_lastError = L"Swap chain unavailable.";
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"IDXGISwapChain::GetBuffer failed.", hr);
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, &m_rtv);
    if (FAILED(hr)) {
        m_lastError = DescribeHResult(L"ID3D11Device::CreateRenderTargetView failed.", hr);
        m_rtv.Reset();
        return false;
    }

    if (m_usingDirectComposition && m_dcompDevice) {
        m_dcompDevice->Commit();
    }

    return true;
}

void OverlayWindow::destroyRenderTarget() {
    if (m_rtv) {
        m_rtv.Reset();
    }
}

void OverlayWindow::destroyDirectComposition() {
    if (m_dcompDevice) {
        if (m_dcompVisual) {
            m_dcompVisual.Reset();
        }
        if (m_dcompTarget) {
            m_dcompTarget.Reset();
        }
        m_dcompDevice.Reset();
    }
    m_usingDirectComposition = false;
}

void OverlayWindow::updateViewport() {
    if (!m_context) {
        return;
    }

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(1, &viewport);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* that = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        that = static_cast<OverlayWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));
        return TRUE;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DPICHANGED: {
        RECT* const rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (that) {
            that->m_width = static_cast<UINT>(rect->right - rect->left);
            that->m_height = static_cast<UINT>(rect->bottom - rect->top);
            if (that->m_swapChain) {
                that->destroyRenderTarget();
                HRESULT hr = that->m_swapChain->ResizeBuffers(0, that->m_width, that->m_height, DXGI_FORMAT_UNKNOWN, 0);
                if (SUCCEEDED(hr)) {
                    if (!that->createRenderTarget()) {
                        // m_lastError already set inside createRenderTarget()
                    }
                } else {
                    that->m_lastError = DescribeHResult(L"IDXGISwapChain::ResizeBuffers failed.", hr);
                }
                that->updateViewport();
            }
            if (that->m_resizeCallback) {
                that->m_resizeCallback(that->m_width, that->m_height);
            }
        }
        return 0;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        if (that && that->m_swapChain) {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            if (width == 0 || height == 0) {
                return 0;
            }
            that->m_width = width;
            that->m_height = height;
            that->destroyRenderTarget();
            HRESULT hr = that->m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (SUCCEEDED(hr)) {
                if (!that->createRenderTarget()) {
                    // m_lastError already populated
                }
            } else {
                that->m_lastError = DescribeHResult(L"IDXGISwapChain::ResizeBuffers failed.", hr);
            }
            that->updateViewport();
            if (that->m_resizeCallback) {
                that->m_resizeCallback(width, height);
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace sv::overlay::ring
