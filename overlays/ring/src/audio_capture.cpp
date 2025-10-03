#include "ring/audio_capture.hpp"

#include <algorithm>
#include <avrt.h>
#include <cmath>
#include <combaseapi.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <numbers>

using Microsoft::WRL::ComPtr;

namespace sv::overlay::ring {
namespace {
    constexpr size_t kWindowFrames = 2048;
    constexpr size_t kHopFrames = 512;

    std::optional<float> AngleForSpeaker(DWORD bit) {
        switch (bit) {
        case SPEAKER_FRONT_CENTER:
            return 0.0f;
        case SPEAKER_FRONT_RIGHT:
            return 30.0f;
        case SPEAKER_FRONT_LEFT:
            return 330.0f;
        case SPEAKER_FRONT_RIGHT_OF_CENTER:
            return 15.0f;
        case SPEAKER_FRONT_LEFT_OF_CENTER:
            return 345.0f;
        case SPEAKER_BACK_CENTER:
            return 180.0f;
        case SPEAKER_BACK_RIGHT:
            return 150.0f;
        case SPEAKER_BACK_LEFT:
            return 210.0f;
        case SPEAKER_SIDE_RIGHT:
            return 90.0f;
        case SPEAKER_SIDE_LEFT:
            return 270.0f;
        case SPEAKER_TOP_FRONT_LEFT:
            return 330.0f;
        case SPEAKER_TOP_FRONT_RIGHT:
            return 30.0f;
        case SPEAKER_TOP_FRONT_CENTER:
            return 0.0f;
        case SPEAKER_TOP_CENTER:
            return 0.0f;
        case SPEAKER_TOP_BACK_LEFT:
            return 210.0f;
        case SPEAKER_TOP_BACK_RIGHT:
            return 150.0f;
        case SPEAKER_TOP_BACK_CENTER:
            return 180.0f;
            return 270.0f;
            return 90.0f;
            return 0.0f;
            return 330.0f;
            return 30.0f;
        case SPEAKER_LOW_FREQUENCY:
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    float WrapAngle(float angleDegrees) {
        while (angleDegrees < 0.0f) {
            angleDegrees += 360.0f;
        }
        while (angleDegrees >= 360.0f) {
            angleDegrees -= 360.0f;
        }
        return angleDegrees;
    }
}

AudioCapture::AudioCapture() {
    m_spectrum.fill(0.0f);
    m_windowSize = kWindowFrames;
    m_hopSize = kHopFrames;
}

AudioCapture::~AudioCapture() {
    shutdown();
}

bool AudioCapture::initialize() {
    m_lastError.clear();
    if (!initializeClient()) {
        releaseClient();
        return false;
    }

    m_running = true;
    m_thread = std::thread(&AudioCapture::captureLoop, this);
    return true;
}

void AudioCapture::shutdown() {
    m_running = false;
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    releaseClient();
}

bool AudioCapture::getSpectrum(std::array<float, SpectrumBins>& outValues) {
    std::scoped_lock lock(m_mutex);
    outValues = m_spectrum;
    return true;
}

bool AudioCapture::getStereoRMS(float& outLeft, float& outRight) {
    std::scoped_lock lock(m_mutex);
    outLeft = m_stereoLeftRMS;
    outRight = m_stereoRightRMS;
    return true;
}

float AudioCapture::getStereoWidth() const {
    return m_stereoWidth;
}

float AudioCapture::getStereoWidthVariance() const {
    return m_stereoWidthVariance;
}

float AudioCapture::getChannelCorrelation() const {
    return m_channelCorrelation;
}

bool AudioCapture::getRawSamples(std::vector<float>& outLeft, std::vector<float>& outRight, size_t& outSampleRate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    outLeft = m_rawLeftChannel;
    outRight = m_rawRightChannel;
    outSampleRate = m_sampleRate;

    // Clear buffers after retrieval to avoid processing same audio twice
    m_rawLeftChannel.clear();
    m_rawRightChannel.clear();

    return !outLeft.empty();
}

bool AudioCapture::initializeClient() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        m_lastError = L"CoCreateInstance(MMDeviceEnumerator) failed.";
        return false;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
    if (FAILED(hr)) {
        m_lastError = L"GetDefaultAudioEndpoint failed.";
        return false;
    }

    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &m_audioClient);
    if (FAILED(hr)) {
        m_lastError = L"IMMDevice::Activate(IAudioClient) failed.";
        return false;
    }

    hr = m_audioClient->GetMixFormat(&m_mixFormat);
    if (FAILED(hr) || !m_mixFormat) {
        m_lastError = L"IAudioClient::GetMixFormat failed.";
        return false;
    }

    m_channelsCount = m_mixFormat->nChannels;
    m_sampleRate = m_mixFormat->nSamplesPerSec;

    if (m_mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_sampleFormat = SampleFormat::Float32;
    } else if (m_mixFormat->wFormatTag == WAVE_FORMAT_PCM) {
        m_sampleFormat = SampleFormat::Int16;
    } else if (m_mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_mixFormat);
        if (extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            m_sampleFormat = SampleFormat::Float32;
        } else {
            m_sampleFormat = SampleFormat::Int16;
        }
    } else {
        m_sampleFormat = SampleFormat::Float32;
    }

    if (!initializeChannelLayout()) {
        return false;
    }

    m_audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_audioEvent || !m_stopEvent) {
        m_lastError = L"CreateEvent failed.";
        return false;
    }

    REFERENCE_TIME bufferDuration = 10000000 / 30;
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        bufferDuration,
        0,
        m_mixFormat,
        nullptr);
    if (FAILED(hr)) {
        m_lastError = L"IAudioClient::Initialize failed.";
        return false;
    }

    hr = m_audioClient->SetEventHandle(m_audioEvent);
    if (FAILED(hr)) {
        m_lastError = L"IAudioClient::SetEventHandle failed.";
        return false;
    }

    hr = m_audioClient->GetService(IID_PPV_ARGS(&m_captureClient));
    if (FAILED(hr)) {
        m_lastError = L"IAudioClient::GetService(IAudioCaptureClient) failed.";
        return false;
    }

    m_channelEnergy.assign(m_channelsCount, 0.0);
    m_channelSampleCount.assign(m_channelsCount, 0u);
    m_channelSumProduct.assign(m_channelsCount, 0.0);
    m_accumulatedFrames = 0;

    return true;
}

bool AudioCapture::initializeChannelLayout() {
    m_channels.clear();
    m_channels.resize(m_channelsCount);

    bool hasDirectional = false;

    if (m_mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_mixFormat);
        DWORD mask = extensible->dwChannelMask;
        if (mask != 0) {
            size_t channelIndex = 0;
            for (DWORD bit = 0; bit < 32 && channelIndex < m_channelsCount; ++bit) {
                DWORD speakerBit = 1u << bit;
                if ((mask & speakerBit) == 0) {
                    continue;
                }

                auto angle = AngleForSpeaker(speakerBit);
                if (angle.has_value()) {
                    m_channels[channelIndex].directional = true;
                    m_channels[channelIndex].angleDegrees = WrapAngle(*angle);
                    hasDirectional = true;
                } else {
                    m_channels[channelIndex].directional = false;
                    m_channels[channelIndex].angleDegrees = 0.0f;
                }
                ++channelIndex;
            }
            for (; channelIndex < m_channelsCount; ++channelIndex) {
                m_channels[channelIndex].directional = false;
                m_channels[channelIndex].angleDegrees = 0.0f;
            }
        }
    }

    if (!hasDirectional) {
        if (m_channelsCount == 1) {
            m_channels[0] = {true, 0.0f};
            hasDirectional = true;
        } else if (m_channelsCount >= 2) {
            m_channels[0] = {true, 330.0f};
            m_channels[1] = {true, 30.0f};
            hasDirectional = true;
            if (m_channelsCount >= 3) {
                m_channels[2] = {true, 0.0f};
            }
            if (m_channelsCount >= 4) {
                m_channels[3] = {true, 270.0f};
            }
            if (m_channelsCount >= 5) {
                m_channels[4] = {true, 90.0f};
            }
        }
    }

    if (!hasDirectional) {
        m_lastError = L"Unable to determine channel directions from format.";
        return false;
    }

    return true;
}

void AudioCapture::releaseClient() {
    if (m_captureClient) {
        m_captureClient.Reset();
    }

    if (m_audioClient) {
        m_audioClient->Stop();
        m_audioClient.Reset();
    }

    if (m_device) {
        m_device.Reset();
    }

    if (m_mixFormat) {
        CoTaskMemFree(m_mixFormat);
        m_mixFormat = nullptr;
    }

    if (m_audioEvent) {
        CloseHandle(m_audioEvent);
        m_audioEvent = nullptr;
    }

    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void AudioCapture::captureLoop() {
    DWORD mmcssTaskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        m_running = false;
    }

    HANDLE waitHandles[2] = { m_stopEvent, m_audioEvent };

    while (m_running) {
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            continue;
        }

        bool spectrumUpdated = false;
        UINT32 packetLength = 0;

        while (SUCCEEDED(m_captureClient->GetNextPacketSize(&packetLength)) && packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            hr = m_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }

            bool appended = false;
            appendSamples(data, numFrames, flags, appended);
            spectrumUpdated = spectrumUpdated || appended;

            m_captureClient->ReleaseBuffer(numFrames);
        }

        if (spectrumUpdated) {
            computeSpectrum();
        }
    }

    m_audioClient->Stop();

    if (mmcssHandle) {
        AvRevertMmThreadCharacteristics(mmcssHandle);
    }
}

void AudioCapture::appendSamples(const BYTE* data, size_t frames, DWORD flags, bool& updated) {
    updated = false;
    if (frames == 0 || m_channelsCount == 0) {
        return;
    }

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        m_accumulatedFrames += frames;
        updated = m_accumulatedFrames >= m_windowSize;
        return;
    }

    if (!data) {
        return;
    }

    // Store raw samples for speech recognition (limit to ~1 second)
    constexpr size_t MAX_RAW_SAMPLES = 48000;

    if (m_sampleFormat == SampleFormat::Float32) {
        const float* samples = reinterpret_cast<const float*>(data);
        for (size_t frame = 0; frame < frames; ++frame) {
            // Store raw L/R channels
            if (m_channelsCount >= 2) {
                m_rawLeftChannel.push_back(samples[frame * m_channelsCount + 0]);
                m_rawRightChannel.push_back(samples[frame * m_channelsCount + 1]);
            }

            for (UINT ch = 0; ch < m_channelsCount; ++ch) {
                float sample = samples[frame * m_channelsCount + ch];
                double value = static_cast<double>(sample);
                m_channelEnergy[ch] += value * value;
                m_channelSampleCount[ch] += 1;
            }
            // Track L*R product for correlation (first 2 channels)
            if (m_channelsCount >= 2) {
                double L = static_cast<double>(samples[frame * m_channelsCount + 0]);
                double R = static_cast<double>(samples[frame * m_channelsCount + 1]);
                m_channelSumProduct[0] += L * R;
            }
        }
    } else {
        const int16_t* samples = reinterpret_cast<const int16_t*>(data);
        constexpr double norm = 1.0 / 32768.0;
        for (size_t frame = 0; frame < frames; ++frame) {
            // Store raw L/R channels
            if (m_channelsCount >= 2) {
                m_rawLeftChannel.push_back(static_cast<float>(samples[frame * m_channelsCount + 0]) * static_cast<float>(norm));
                m_rawRightChannel.push_back(static_cast<float>(samples[frame * m_channelsCount + 1]) * static_cast<float>(norm));
            }

            for (UINT ch = 0; ch < m_channelsCount; ++ch) {
                double sample = static_cast<double>(samples[frame * m_channelsCount + ch]) * norm;
                m_channelEnergy[ch] += sample * sample;
                m_channelSampleCount[ch] += 1;
            }
            // Track L*R product for correlation (first 2 channels)
            if (m_channelsCount >= 2) {
                double L = static_cast<double>(samples[frame * m_channelsCount + 0]) * norm;
                double R = static_cast<double>(samples[frame * m_channelsCount + 1]) * norm;
                m_channelSumProduct[0] += L * R;
            }
        }
    }

    // Limit raw buffer size
    if (m_rawLeftChannel.size() > MAX_RAW_SAMPLES) {
        m_rawLeftChannel.erase(m_rawLeftChannel.begin(), m_rawLeftChannel.begin() + (m_rawLeftChannel.size() - MAX_RAW_SAMPLES));
        m_rawRightChannel.erase(m_rawRightChannel.begin(), m_rawRightChannel.begin() + (m_rawRightChannel.size() - MAX_RAW_SAMPLES));
    }

    m_accumulatedFrames += frames;
    updated = m_accumulatedFrames >= m_windowSize;
}

void AudioCapture::computeSpectrum() {
    if (m_accumulatedFrames < m_windowSize) {
        return;
    }

    std::array<float, SpectrumBins> newSpectrum{};
    const float bins = static_cast<float>(SpectrumBins);

    auto accumulate = [&](float angle, float amplitude) {
        float wrapped = WrapAngle(angle);
        float position = (wrapped / 360.0f) * bins;
        float baseFloat = std::floor(position);
        int baseIndex = static_cast<int>(baseFloat) % static_cast<int>(SpectrumBins);
        float frac = position - baseFloat;

        auto add = [&](int index, float weight) {
            if (weight <= 0.0f) {
                return;
            }
            int wrappedIndex = (index % static_cast<int>(SpectrumBins) + static_cast<int>(SpectrumBins)) % static_cast<int>(SpectrumBins);
            newSpectrum[static_cast<size_t>(wrappedIndex)] += amplitude * weight;
        };

        add(baseIndex, 1.0f - frac);
        add(baseIndex + 1, frac);
        add(baseIndex - 1, 0.35f * (1.0f - frac));
        add(baseIndex + 2, 0.35f * frac);
    };

    for (size_t ch = 0; ch < m_channels.size(); ++ch) {
        if (!m_channels[ch].directional) {
            continue;
        }
        size_t count = m_channelSampleCount[ch];
        if (count == 0) {
            continue;
        }
        double meanSquare = m_channelEnergy[ch] / static_cast<double>(count);
        double rms = std::sqrt(std::max(meanSquare, 0.0));

        // Much higher sensitivity for quiet sounds - use power scaling
        // Square root makes quiet sounds much more visible
        float amplitude = std::pow(static_cast<float>(rms), 0.4f) * 2.5f; // Higher amplification
        amplitude = std::clamp(amplitude, 0.0f, 1.5f);
        accumulate(m_channels[ch].angleDegrees, amplitude);
    }

    std::array<float, SpectrumBins> blurred{};
    for (size_t i = 0; i < SpectrumBins; ++i) {
        size_t prev = (i + SpectrumBins - 1) % SpectrumBins;
        size_t next = (i + 1) % SpectrumBins;
        blurred[i] = newSpectrum[i] * 0.55f + (newSpectrum[prev] + newSpectrum[next]) * 0.225f;
    }

    float maxValue = *std::max_element(blurred.begin(), blurred.end());
    if (maxValue > 1.0f && maxValue > 0.0f) {
        for (auto& value : blurred) {
            value /= maxValue;
        }
    }

    {
        std::scoped_lock lock(m_mutex);
        for (size_t i = 0; i < SpectrumBins; ++i) {
            float clamped = std::clamp(blurred[i], 0.0f, 1.0f);
            m_spectrum[i] = m_spectrum[i] * 0.6f + clamped * 0.4f;
        }

        // Compute stereo L/R RMS from first two channels
        if (m_channelsCount >= 2 && m_channelSampleCount.size() >= 2) {
            // Left channel (index 0)
            if (m_channelSampleCount[0] > 0) {
                double meanSquareL = m_channelEnergy[0] / static_cast<double>(m_channelSampleCount[0]);
                float rmsL = static_cast<float>(std::sqrt(std::max(meanSquareL, 0.0)));
                m_stereoLeftRMS = m_stereoLeftRMS * 0.7f + rmsL * 0.3f; // Smooth
            }

            // Right channel (index 1)
            if (m_channelSampleCount[1] > 0) {
                double meanSquareR = m_channelEnergy[1] / static_cast<double>(m_channelSampleCount[1]);
                float rmsR = static_cast<float>(std::sqrt(std::max(meanSquareR, 0.0)));
                m_stereoRightRMS = m_stereoRightRMS * 0.7f + rmsR * 0.3f; // Smooth
            }
        } else if (m_channelsCount == 1 && m_channelSampleCount.size() >= 1) {
            // Mono: use same value for both
            if (m_channelSampleCount[0] > 0) {
                double meanSquare = m_channelEnergy[0] / static_cast<double>(m_channelSampleCount[0]);
                float rms = static_cast<float>(std::sqrt(std::max(meanSquare, 0.0)));
                m_stereoLeftRMS = m_stereoLeftRMS * 0.7f + rms * 0.3f;
                m_stereoRightRMS = m_stereoRightRMS * 0.7f + rms * 0.3f;
            }
        }

        // Calculate stereo width: 0.0 = mono/centered, higher = stereo/panned
        float sum = m_stereoLeftRMS + m_stereoRightRMS;
        if (sum > 0.001f) {
            float diff = std::abs(m_stereoLeftRMS - m_stereoRightRMS);
            float targetWidth = diff / sum;
            m_stereoWidth = m_stereoWidth * 0.8f + targetWidth * 0.2f; // Smooth
        } else {
            m_stereoWidth = 0.0f;
        }

        // Calculate L/R channel correlation (Pearson correlation coefficient)
        if (m_channelsCount >= 2 && m_channelSampleCount[0] > 0 && m_channelSampleCount[1] > 0) {
            double sumLR = m_channelSumProduct[0];
            double sumL2 = m_channelEnergy[0];
            double sumR2 = m_channelEnergy[1];

            // Correlation = E[LR] / sqrt(E[L²] * E[R²])
            // Simplified since we don't track means separately
            double denominator = std::sqrt(sumL2 * sumR2);
            if (denominator > 0.0) {
                float correlation = static_cast<float>(sumLR / denominator);
                correlation = std::clamp(correlation, -1.0f, 1.0f);
                m_channelCorrelation = m_channelCorrelation * 0.8f + correlation * 0.2f; // Smooth
            }
        }

        // Track stereo width variance over time
        m_stereoWidthHistory.push_back(m_stereoWidth);
        if (m_stereoWidthHistory.size() > kStereoWidthHistorySize) {
            m_stereoWidthHistory.erase(m_stereoWidthHistory.begin());
        }

        // Calculate variance
        if (m_stereoWidthHistory.size() > 10) { // Need some samples
            float mean = 0.0f;
            for (float val : m_stereoWidthHistory) {
                mean += val;
            }
            mean /= static_cast<float>(m_stereoWidthHistory.size());

            float variance = 0.0f;
            for (float val : m_stereoWidthHistory) {
                float diff = val - mean;
                variance += diff * diff;
            }
            variance /= static_cast<float>(m_stereoWidthHistory.size());
            m_stereoWidthVariance = variance;
        }
    }

    std::fill(m_channelEnergy.begin(), m_channelEnergy.end(), 0.0);
    std::fill(m_channelSampleCount.begin(), m_channelSampleCount.end(), 0u);
    std::fill(m_channelSumProduct.begin(), m_channelSumProduct.end(), 0.0);
    if (m_accumulatedFrames >= m_hopSize) {
        m_accumulatedFrames -= m_hopSize;
    } else {
        m_accumulatedFrames = 0;
    }
}

} // namespace sv::overlay::ring
