#pragma once

#include "ring/audio_capture.hpp"
#include "ring/overlay_window.hpp"
#include "ring/ring_renderer.hpp"

#include <array>
#include <atomic>
#include <string>
#include <windows.h>

namespace sv::overlay::ring {

class RingApp {
public:
    RingApp() = default;
    int run(HINSTANCE instance, HINSTANCE prev, PWSTR cmdLine, int showCmd);
    const std::wstring& startupError() const { return m_startupError; }


private:
    bool initialize(HINSTANCE instance);
    void shutdown();
    void updateFrame();

    OverlayWindow m_window;
    RingRenderer m_renderer;
    AudioCapture m_audioCapture;

    std::array<float, AudioCapture::SpectrumBins> m_spectrum{};
    std::wstring m_startupError;

    LARGE_INTEGER m_frequency{};
    LARGE_INTEGER m_lastCounter{};
};

} // namespace sv::overlay::ring
