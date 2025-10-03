#pragma once

#include "ring/speech_recognizer.hpp"
#include "ring/subtitle_renderer.hpp"
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

namespace sv::overlay::ring {

// Orchestrates speech recognition and subtitle display
class SpeechService {
public:
    SpeechService();
    ~SpeechService();

    bool initialize(RecognizerType type, const char* modelPath, ID3D11Device* device);
    void shutdown();

    // Add audio samples (stereo float32, will convert to mono int16 for recognition)
    void addAudio(const float* leftChannel, const float* rightChannel, size_t sampleCount, size_t sampleRate);

    // Render subtitles
    // stereoWidth: 0.0 (mono) to 1.0 (full stereo) for directional hints
    // audioEnergy: 0.0 (quiet) to 1.0 (loud) for adaptive opacity
    void render(ID3D11DeviceContext* context, float deltaTime, float stereoWidth, float audioEnergy);

    void resize(float width, float height);

    // Configuration
    void setSubtitleConfig(const SubtitleConfig& config);
    const SubtitleConfig& subtitleConfig() const;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    void processingThread();
    void convertToMono16(const float* left, const float* right, size_t count, std::vector<int16_t>& output);

    std::unique_ptr<ISpeechRecognizer> m_recognizer;
    std::unique_ptr<SubtitleRenderer> m_subtitleRenderer;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_enabled{true};

    std::mutex m_audioMutex;
    std::vector<int16_t> m_audioBuffer;
    std::string m_modelPath;

    std::wstring m_lastError;
};

} // namespace sv::overlay::ring
