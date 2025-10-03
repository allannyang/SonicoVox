#include "ring/speech_service.hpp"
#include <windows.h>
#include <algorithm>
#include <sstream>

namespace sv::overlay::ring {

SpeechService::SpeechService() {
}

SpeechService::~SpeechService() {
    shutdown();
}

bool SpeechService::initialize(RecognizerType type, const char* modelPath, ID3D11Device* device) {
    shutdown();

    // Create subtitle renderer
    m_subtitleRenderer = std::make_unique<SubtitleRenderer>();
    if (!m_subtitleRenderer->initialize(device)) {
        m_lastError = L"Failed to initialize subtitle renderer";
        return false;
    }

    // Store model path for Python subprocess
    m_modelPath = modelPath;

    // Start processing thread
    m_running = true;
    m_thread = std::thread(&SpeechService::processingThread, this);

    return true;
}

void SpeechService::shutdown() {
    m_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_subtitleRenderer.reset();
}

void SpeechService::addAudio(const float* leftChannel, const float* rightChannel, size_t sampleCount, size_t sampleRate) {
    if (!m_enabled || !m_running) {
        return;
    }

    // Convert stereo float32 to mono int16
    std::vector<int16_t> monoSamples;
    convertToMono16(leftChannel, rightChannel, sampleCount, monoSamples);

    // Add to buffer
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioBuffer.insert(m_audioBuffer.end(), monoSamples.begin(), monoSamples.end());
    }
}

void SpeechService::render(ID3D11DeviceContext* context, float deltaTime, float stereoWidth, float audioEnergy) {
    if (!m_subtitleRenderer || !m_enabled) {
        return;
    }

    // Stereo position from stereo width (-1 left, 0 center, +1 right)
    float stereoPosition = (stereoWidth - 0.5f) * 2.0f;
    stereoPosition = std::clamp(stereoPosition, -1.0f, 1.0f);

    m_subtitleRenderer->render(context, deltaTime, stereoPosition, audioEnergy);
}

void SpeechService::resize(float width, float height) {
    if (m_subtitleRenderer) {
        m_subtitleRenderer->resize(width, height);
    }
}

void SpeechService::setSubtitleConfig(const SubtitleConfig& config) {
    if (m_subtitleRenderer) {
        m_subtitleRenderer->setConfig(config);
    }
}

const SubtitleConfig& SpeechService::subtitleConfig() const {
    static SubtitleConfig defaultConfig;
    if (m_subtitleRenderer) {
        return m_subtitleRenderer->config();
    }
    return defaultConfig;
}

void SpeechService::processingThread() {
    // Launch Python subprocess
    HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    PROCESS_INFORMATION pi = {};
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    // Create pipes
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0) ||
        !CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        return;
    }

    // Don't inherit write end of stdin or read end of stdout
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // Build command line
    std::string cmdLine = "python scripts\\vosk_worker.py \"" + m_modelPath + "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // Launch process
    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        OutputDebugStringA("[SUBTITLE] Failed to launch Python worker process\n");
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        return;
    }

    OutputDebugStringA("[SUBTITLE] Python worker launched successfully\n");

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    // Wait for "ready" signal
    char readBuffer[4096];
    DWORD bytesRead = 0;
    bool ready = false;
    std::string lineBuffer;

    // Read ready signal
    while (!ready && m_running) {
        if (ReadFile(hStdoutRead, readBuffer, sizeof(readBuffer), &bytesRead, nullptr) && bytesRead > 0) {
            lineBuffer.append(readBuffer, bytesRead);
            size_t newlinePos = lineBuffer.find('\n');
            if (newlinePos != std::string::npos) {
                std::string line = lineBuffer.substr(0, newlinePos);
                if (line.find("\"ready\"") != std::string::npos) {
                    ready = true;
                    OutputDebugStringA("[SUBTITLE] Python worker ready\n");
                } else if (line.find("\"error\"") != std::string::npos) {
                    OutputDebugStringA("[SUBTITLE] Python worker error: ");
                    OutputDebugStringA(line.c_str());
                    OutputDebugStringA("\n");
                }
                lineBuffer.clear();
            }
        }
        Sleep(10);
    }

    if (!ready) {
        OutputDebugStringA("[SUBTITLE] Python worker failed to initialize\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return;
    }

    // Main loop
    const size_t CHUNK_SIZE = 4000; // ~0.25s at 16kHz
    lineBuffer.clear();

    while (m_running) {
        // Send audio chunks to Python
        std::vector<int16_t> chunk;
        {
            std::lock_guard<std::mutex> lock(m_audioMutex);
            if (m_audioBuffer.size() >= CHUNK_SIZE) {
                chunk.assign(m_audioBuffer.begin(), m_audioBuffer.begin() + CHUNK_SIZE);
                m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + CHUNK_SIZE);
            }
        }

        if (!chunk.empty()) {
            // Write chunk size
            uint32_t chunkBytes = static_cast<uint32_t>(chunk.size() * sizeof(int16_t));
            DWORD written = 0;
            WriteFile(hStdinWrite, &chunkBytes, sizeof(chunkBytes), &written, nullptr);

            // Write audio data
            WriteFile(hStdinWrite, chunk.data(), chunkBytes, &written, nullptr);
        }

        // Read results from Python (non-blocking check)
        DWORD available = 0;
        if (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD toRead = std::min(available, static_cast<DWORD>(sizeof(readBuffer)));
            if (ReadFile(hStdoutRead, readBuffer, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                lineBuffer.append(readBuffer, bytesRead);

                // Process complete lines
                size_t newlinePos;
                while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {
                    std::string line = lineBuffer.substr(0, newlinePos);
                    lineBuffer.erase(0, newlinePos + 1);

                    // Parse JSON result
                    if (line.find("\"type\"") != std::string::npos) {
                        bool isFinal = line.find("\"final\"") != std::string::npos;

                        // Extract text field
                        size_t textPos = line.find("\"text\"");
                        if (textPos != std::string::npos) {
                            size_t start = line.find("\"", textPos + 6);
                            if (start != std::string::npos) {
                                size_t end = line.find("\"", start + 1);
                                if (end != std::string::npos) {
                                    std::string text = line.substr(start + 1, end - start - 1);

                                    // Convert to wstring and update renderer
                                    std::wstring wtext(text.begin(), text.end());
                                    if (m_subtitleRenderer) {
                                        OutputDebugStringA("[SUBTITLE] Transcript: ");
                                        OutputDebugStringA(text.c_str());
                                        OutputDebugStringA(isFinal ? " (final)\n" : " (partial)\n");
                                        m_subtitleRenderer->updateText(wtext, isFinal);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Sleep(10); // Small delay to prevent busy-wait
    }

    // Cleanup
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(hStdinWrite);
    CloseHandle(hStdoutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void SpeechService::convertToMono16(const float* left, const float* right, size_t count, std::vector<int16_t>& output) {
    output.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        // Mix to mono and convert to int16
        float mono = (left[i] + right[i]) * 0.5f;
        mono = std::clamp(mono, -1.0f, 1.0f);
        output.push_back(static_cast<int16_t>(mono * 32767.0f));
    }
}

} // namespace sv::overlay::ring
