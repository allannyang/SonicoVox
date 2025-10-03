#include "ring/ring_app.hpp"

#include <windows.h>
#include <string>
using namespace sv::overlay::ring;

// ============================================================
// FEATURE TOGGLES - Easy testing control
// ============================================================
constexpr bool ENABLE_RING_OVERLAY = true;
constexpr bool ENABLE_SUBTITLES = true;
// ============================================================

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

    // Create log file for debugging
    FILE* logFile = nullptr;
    fopen_s(&logFile, "sonicovox_debug.log", "w");

    auto log = [&logFile](const char* msg) {
        OutputDebugStringA(msg);
        if (logFile) {
            fprintf(logFile, "%s", msg);
            fflush(logFile);
        }
    };

    log("========================================\n");
    log("[SONICOVOX] Ring overlay starting...\n");
    log(ENABLE_RING_OVERLAY ? "[SONICOVOX] Ring overlay: ENABLED\n" : "[SONICOVOX] Ring overlay: DISABLED\n");
    log(ENABLE_SUBTITLES ? "[SONICOVOX] Subtitles: ENABLED\n" : "[SONICOVOX] Subtitles: DISABLED\n");
    log("========================================\n");

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

    // Initialize speech service (optional - will log error if model not found)
    if (ENABLE_SUBTITLES) {
        log("[SONICOVOX] Initializing speech service...\n");
        if (!m_speechService.initialize(RecognizerType::Vosk, "models/vosk-model-small-en-us-0.15", m_window.device())) {
            // Non-fatal - subtitles are optional feature
            log("[SONICOVOX] Speech service initialization failed - subtitles disabled\n");
        } else {
            log("[SONICOVOX] Speech service initialized successfully\n");
        }
    }

    if (logFile) {
        fclose(logFile);
    }

    return true;
}

void RingApp::shutdown() {
    m_speechService.shutdown();
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

    float leftRMS = 0.0f;
    float rightRMS = 0.0f;
    m_audioCapture.getStereoRMS(leftRMS, rightRMS);
    float stereoWidth = m_audioCapture.getStereoWidth();
    float channelCorrelation = m_audioCapture.getChannelCorrelation();

    // Feed audio to speech recognition
    if (ENABLE_SUBTITLES) {
        std::vector<float> leftChannel, rightChannel;
        size_t sampleRate = 0;
        if (m_audioCapture.getRawSamples(leftChannel, rightChannel, sampleRate)) {
            static bool logged = false;
            if (!logged) {
                char buf[256];
                sprintf_s(buf, "[AUDIO] Capture sample rate: %zu Hz\n", sampleRate);
                OutputDebugStringA(buf);
                logged = true;
            }
            m_speechService.addAudio(leftChannel.data(), rightChannel.data(), leftChannel.size(), sampleRate);
        }
    }

    // Adjust stereo width based on channel correlation for better mono detection
    // High correlation (>0.9) = channels are very similar = likely mono
    // Use this to make stereo width detection more robust
    if (channelCorrelation > 0.9f) {
        stereoWidth *= 0.5f; // Reduce effective stereo width for mono-like signals
    }

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!m_window.beginFrame(clearColor)) {
        return;
    }

    // Render ring overlay
    if (ENABLE_RING_OVERLAY) {
        m_renderer.renderWithStereo(m_window.context(), m_spectrum, leftRMS, rightRMS, stereoWidth, channelCorrelation, m_audioCapture.getStereoWidthVariance());
    }

    // Render subtitles (if speech service is active)
    if (ENABLE_SUBTITLES) {
        float deltaTime = 0.016f; // ~60 fps
        float audioEnergy = (leftRMS + rightRMS) * 0.5f;
        m_speechService.render(m_window.context(), deltaTime, stereoWidth, audioEnergy);
    }

    m_window.present();
}

