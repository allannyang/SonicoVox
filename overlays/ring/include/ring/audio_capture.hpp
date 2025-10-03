#pragma once

#include <array>
#include <atomic>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <vector>
#include <mutex>
#include <optional>
#include <wrl/client.h>

namespace sv::overlay::ring {

class AudioCapture {
public:
    static constexpr size_t SpectrumBins = 64;

    AudioCapture();
    ~AudioCapture();

    bool initialize();
    void shutdown();

    bool getSpectrum(std::array<float, SpectrumBins>& outValues);
    bool getStereoRMS(float& outLeft, float& outRight);
    float getStereoWidth() const;
    float getStereoWidthVariance() const;
    float getChannelCorrelation() const;
    bool getRawSamples(std::vector<float>& outLeft, std::vector<float>& outRight, size_t& outSampleRate);
    const std::wstring& lastError() const { return m_lastError; }

private:
    enum class SampleFormat {
        Float32,
        Int16
    };

    struct ChannelInfo {
        bool directional = false;
        float angleDegrees = 0.0f;
    };

    void captureLoop();
    bool initializeClient();
    bool initializeChannelLayout();
    void releaseClient();
    void computeSpectrum();
    void appendSamples(const BYTE* data, size_t frames, DWORD flags, bool& updated);

    Microsoft::WRL::ComPtr<IMMDevice> m_device;
    Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> m_captureClient;

    HANDLE m_audioEvent = nullptr;
    HANDLE m_stopEvent = nullptr;

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    std::mutex m_mutex;
    std::array<float, SpectrumBins> m_spectrum{};
    float m_stereoLeftRMS = 0.0f;
    float m_stereoRightRMS = 0.0f;
    float m_stereoWidth = 0.0f;

    // Raw sample buffers for speech recognition
    std::vector<float> m_rawLeftChannel;
    std::vector<float> m_rawRightChannel;

    // Mono detection tracking
    std::vector<float> m_stereoWidthHistory;
    float m_stereoWidthVariance = 0.0f;
    float m_channelCorrelation = 0.0f;
    static constexpr size_t kStereoWidthHistorySize = 60; // ~2 seconds at 30fps

    std::vector<ChannelInfo> m_channels;
    std::vector<double> m_channelEnergy;
    std::vector<size_t> m_channelSampleCount;
    std::vector<double> m_channelSumProduct; // For correlation calculation
    size_t m_accumulatedFrames = 0;

    WAVEFORMATEX* m_mixFormat = nullptr;
    SampleFormat m_sampleFormat = SampleFormat::Float32;
    UINT m_channelsCount = 0;
    UINT m_sampleRate = 0;
    size_t m_windowSize = 2048;
    size_t m_hopSize = 512;

    std::wstring m_lastError;
};

} // namespace sv::overlay::ring
