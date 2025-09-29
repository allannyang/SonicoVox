#include "ring/ring_app.hpp"

#include <windows.h>
#include <string>
using namespace sv::overlay::ring;

int RingApp::run(HINSTANCE instance, HINSTANCE prevInstance, PWSTR cmdLine, int showCmd) {
    UNREFERENCED_PARAMETER(prevInstance);
    UNREFERENCED_PARAMETER(cmdLine);
    UNREFERENCED_PARAMETER(showCmd);

    if (!initialize(instance)) {
        const auto& detail = startupError();
        const wchar_t* message = detail.empty() ? L"Failed to initialize ring overlay." : detail.c_str();
        MessageBoxW(nullptr, message, L"SonicoVox", MB_ICONERROR | MB_OK);
        shutdown();
        return -1;
    }

    constexpr int kHotkeyId = 1;
    RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT, 'V');

    MSG msg{};
    bool running = true;

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
                PostQuitMessage(0);
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) {
            break;
        }

        updateFrame();
        Sleep(1);
    }

    UnregisterHotKey(nullptr, kHotkeyId);
    shutdown();

    return static_cast<int>(msg.wParam);
}

bool RingApp::initialize(HINSTANCE instance) {
    m_startupError.clear();

    if (!QueryPerformanceFrequency(&m_frequency)) {
        m_startupError = L"High-resolution timer not available on this system.";
        return false;
    }

    if (!QueryPerformanceCounter(&m_lastCounter)) {
        m_startupError = L"Failed to query high-resolution timer.";
        return false;
    }

    if (!m_audioCapture.initialize()) {
        m_startupError = L"Audio capture initialization failed.";
        const auto& detail = m_audioCapture.lastError();
        if (!detail.empty()) {
            m_startupError += L"\n\n" + detail;
        }
        return false;
    }

    auto resizeHandler = [this](UINT width, UINT height) {
        m_renderer.resize(static_cast<float>(width), static_cast<float>(height));
    };

    if (!m_window.initialize(instance, resizeHandler)) {
        m_startupError = L"Overlay window creation failed.";
        const auto& detail = m_window.lastError();
        if (!detail.empty()) {
            m_startupError += L"\n\n" + detail;
        }
        return false;
    }

    if (!m_renderer.initialize(m_window.device())) {
        m_startupError = L"Ring renderer initialization failed.";
        return false;
    }

    m_renderer.resize(static_cast<float>(m_window.width()), static_cast<float>(m_window.height()));
    return true;
}

void RingApp::shutdown() {
    m_window.shutdown();
    m_audioCapture.shutdown();
}

void RingApp::updateFrame() {
    LARGE_INTEGER now{};
    if (QueryPerformanceCounter(&now)) {
        m_lastCounter = now;
    }

    std::array<float, AudioCapture::SpectrumBins> snapshot{};
    if (m_audioCapture.getSpectrum(snapshot)) {
        m_spectrum = snapshot;
    }

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!m_window.beginFrame(clearColor)) {
        return;
    }

    m_renderer.render(m_window.context(), m_spectrum);
    m_window.present();
}

