#pragma once

#include "ring/audio_capture.hpp"

#include <array>
#include <wrl/client.h>
#include <d3d11.h>

namespace sv::overlay::ring {

class RingRenderer {
public:
    bool initialize(ID3D11Device* device);
    void resize(float width, float height);
    void render(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum);
    void renderWithStereo(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum, float leftRMS, float rightRMS, float stereoWidth, float channelCorrelation, float stereoWidthVariance);

private:
    struct Vertex {
        float position[2];
        float color[4];
    };

    struct Transform {
        float resolution[2];
        float padding[2];
    };

    void updateVertexBuffer(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum, float stereoWidth, float channelCorrelation, float stereoWidthVariance);
    void updateStereoCircles(ID3D11DeviceContext* context, float leftRMS, float rightRMS);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_stereoCircleBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterState;

    float m_width = 1.0f;
    float m_height = 1.0f;
    UINT m_vertexCount = 0;
    UINT m_stereoCircleVertexCount = 0;

    // Temporal smoothing for peak direction
    float m_smoothedDominantAngle = 0.0f;
    float m_smoothedEnergyScale = 0.0f;

    // Mouse tracking for front/back disambiguation
    struct MouseSample {
        POINT position;
        DWORD timestamp;
    };
    MouseSample m_currentMouse{};
    MouseSample m_previousMouse{};

    // Correlation tracking
    struct CorrelationSample {
        float mouseDeltaX;      // Horizontal mouse movement
        float angleChange;      // Peak angle change
        float energyChange;     // Energy change
        DWORD timestamp;
    };
    std::vector<CorrelationSample> m_correlationHistory;
    float m_frontBackConfidence = 0.0f; // -1.0 = confident back, +1.0 = confident front, 0.0 = ambiguous
    static constexpr size_t kCorrelationHistorySize = 30; // ~1 second at 30fps
    static constexpr DWORD kCorrelationWindowMs = 300; // 300ms correlation window

    // Mono content detection
    float m_monoContentConfidence = 0.0f; // 0.0 = directional, 1.0 = non-directional mono content

    // Stereo history for context-aware detection
    std::vector<float> m_stereoWidthHistory;
    float m_maxRecentStereoWidth = 0.0f; // Max stereo width in recent history
    static constexpr size_t kStereoHistorySize = 90; // ~3 seconds at 30fps

    // Adaptive gain for quiet sounds
    float m_adaptiveGain = 1.0f; // Multiplier for quiet sounds
    float m_recentEnergyLevel = 0.0f; // Smoothed energy level for adaptive gain

    // State hysteresis - resist mode switching based on time in current state
    float m_timeInCurrentMode = 0.0f; // Frames spent in current mode
    float m_lastModeState = 0.5f; // Last frame's mode (0.0 = directional, 1.0 = mono)
};

} // namespace sv::overlay::ring