#include "ring/ring_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <windows.h>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace sv::overlay::ring {
namespace {
    constexpr float kBaseRadiusRatio = 0.10f;
    constexpr float kRingThicknessRatio = 0.020f;
    constexpr float kAmplitudeScale = 0.14f; // Reduced from 0.20f - peaks were too tall
    constexpr size_t kRenderSegments = 360; // One segment per degree for truly smooth appearance
    constexpr float kPeakWidthDegrees = 30.0f; // Narrower peak - more distinct (was 60.0f)
    constexpr float kPeakSharpness = 2.0f; // Sharper falloff
    constexpr float kConstantAlpha = 0.7f; // Fixed alpha - no variation
    constexpr float kAngleSmoothingFactor = 0.85f; // High smoothing for stable direction (0.0 = no smoothing, 1.0 = max smoothing)
    constexpr float kEnergySmoothingFactor = 0.7f; // Smooth energy changes
    constexpr float kStereoWidthThreshold = 0.15f; // Below this = mono (dual peaks), above = stereo (single peak)

    HRESULT CompileShader(const char* source, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& blob) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG;
#endif
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target, flags, 0, &blob, &errorBlob);
        if (FAILED(hr) && errorBlob) {
            OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return hr;
    }
}

bool RingRenderer::initialize(ID3D11Device* device) {
    if (!device) {
        return false;
    }

    static const char* kVertexShader = R"(cbuffer Transform : register(b0) {
    float2 resolution;
    float2 padding;
};

struct VSInput {
    float2 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VSOutput main(VSInput input) {
    float2 normalized = input.position / resolution;
    float2 clip = float2(normalized.x * 2.0f - 1.0f, 1.0f - normalized.y * 2.0f);
    VSOutput output;
    output.position = float4(clip, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}
)";

    static const char* kPixelShader = R"(struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    return input.color;
}
)";

    ComPtr<ID3DBlob> vsBlob;
    if (FAILED(CompileShader(kVertexShader, "main", "vs_5_0", vsBlob))) {
        return false;
    }

    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader))) {
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    if (FAILED(device->CreateInputLayout(layout, _countof(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout))) {
        return false;
    }

    ComPtr<ID3DBlob> psBlob;
    if (FAILED(CompileShader(kPixelShader, "main", "ps_5_0", psBlob))) {
        return false;
    }

    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader))) {
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.ByteWidth = sizeof(Transform);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer))) {
        return false;
    }

    m_vertexCount = static_cast<UINT>(kRenderSegments * 6);

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.ByteWidth = sizeof(Vertex) * m_vertexCount;
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&vbDesc, nullptr, &m_vertexBuffer))) {
        return false;
    }

    // Create stereo circle vertex buffer (2 circles, 32 segments each = 64 triangles * 3 vertices)
    constexpr size_t kCircleSegments = 32;
    m_stereoCircleVertexCount = static_cast<UINT>(kCircleSegments * 2 * 3 * 2); // 2 circles

    D3D11_BUFFER_DESC circleVbDesc{};
    circleVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    circleVbDesc.ByteWidth = sizeof(Vertex) * m_stereoCircleVertexCount;
    circleVbDesc.Usage = D3D11_USAGE_DYNAMIC;
    circleVbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&circleVbDesc, nullptr, &m_stereoCircleBuffer))) {
        return false;
    }

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;          // Standard alpha blending
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blendDesc, &m_blendState))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &m_rasterState))) {
        return false;
    }

    return true;
}

void RingRenderer::resize(float width, float height) {
    m_width = std::max(width, 1.0f);
    m_height = std::max(height, 1.0f);
}

void RingRenderer::render(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum) {
    if (!context || !m_vertexBuffer || !m_constantBuffer) {
        return;
    }

    updateVertexBuffer(context, spectrum, 1.0f, 0.0f, 0.0f); // Default to fully stereo (single peak)

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto transform = reinterpret_cast<Transform*>(mapped.pData);
        transform->resolution[0] = m_width;
        transform->resolution[1] = m_height;
        transform->padding[0] = 0.0f;
        transform->padding[1] = 0.0f;
        context->Unmap(m_constantBuffer.Get(), 0);
    }

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    context->RSSetState(m_rasterState.Get());

    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    context->Draw(m_vertexCount, 0);
}

void RingRenderer::updateVertexBuffer(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum, float stereoWidth, float channelCorrelation, float stereoWidthVariance) {
    if (!context || !m_vertexBuffer) {
        return;
    }

    // Track stereo width history for context-aware detection
    m_stereoWidthHistory.push_back(stereoWidth);
    if (m_stereoWidthHistory.size() > kStereoHistorySize) {
        m_stereoWidthHistory.erase(m_stereoWidthHistory.begin());
    }

    // Calculate max recent stereo width (looking back ~1 second = 30 frames)
    m_maxRecentStereoWidth = 0.0f;
    if (m_stereoWidthHistory.size() >= 30) {
        size_t startIdx = m_stereoWidthHistory.size() - 30;
        for (size_t i = startIdx; i < m_stereoWidthHistory.size(); ++i) {
            m_maxRecentStereoWidth = std::max(m_maxRecentStereoWidth, m_stereoWidthHistory[i]);
        }
    }

    // Calculate mono content confidence
    // Bias towards directional - catch footsteps more easily
    float targetMonoConfidence = 1.0f; // Start at full mono confidence

    // If we had significant stereo width recently (>0.3), and now centered → this is directional (enemy approaching)
    bool recentStereoActivity = m_maxRecentStereoWidth > 0.3f; // Lowered from 0.35

    if (recentStereoActivity) {
        // Recent stereo activity - treat as directional, not mono
        targetMonoConfidence = 0.0f;
    } else {
        // No recent stereo activity - check if we should go directional
        // More sensitive thresholds - easier to break into directional mode

        float antiMonoFactor = 0.0f;

        if (channelCorrelation < 0.7f) {
            // Lowered from 0.5 - more sensitive to channel differences
            antiMonoFactor = std::max(antiMonoFactor, (0.7f - channelCorrelation) / 0.7f);
        }

        if (stereoWidthVariance > 0.03f) {
            // Lowered from 0.05 - more sensitive to variance
            antiMonoFactor = std::max(antiMonoFactor, (stereoWidthVariance - 0.03f) / 0.07f);
        }

        if (stereoWidth > 0.25f) {
            // Lowered from 0.4 - catch subtle panning
            antiMonoFactor = std::max(antiMonoFactor, (stereoWidth - 0.25f) / 0.75f);
        }

        // Apply power curve to boost anti-mono factor (easier to escape mono mode)
        antiMonoFactor = std::pow(std::clamp(antiMonoFactor, 0.0f, 1.0f), 0.8f);
        targetMonoConfidence = 1.0f - antiMonoFactor;
    }

    // Apply hysteresis - resist mode switching based on time in current state
    // Determine current mode: <0.3 = directional, >0.7 = mono, middle = transitioning
    float currentMode = m_monoContentConfidence;
    bool inDirectionalMode = currentMode < 0.3f;
    bool inMonoMode = currentMode > 0.7f;
    bool wasInSameMode = std::abs(currentMode - m_lastModeState) < 0.1f;

    if (wasInSameMode) {
        // Still in same mode - increase time counter
        m_timeInCurrentMode = std::min(m_timeInCurrentMode + 1.0f, 180.0f); // Cap at ~6 seconds
    } else {
        // Mode changed - reset counter
        m_timeInCurrentMode = 0.0f;
    }

    // Calculate base hysteresis factor based on time in mode
    // 0-30 frames (~1 sec) = low resistance (0.0)
    // 180 frames (~6 sec) = high resistance (0.5)
    float baseHysteresisFactor = std::clamp(m_timeInCurrentMode / 180.0f, 0.0f, 0.5f);

    // Apply asymmetric hysteresis bias
    if (inDirectionalMode && targetMonoConfidence > currentMode) {
        // In directional mode, trying to go mono - STRONG resistance (always)
        // We don't want music/UI to break directional mode during gameplay
        float directionalHysteresis = baseHysteresisFactor;
        targetMonoConfidence = targetMonoConfidence * (1.0f - directionalHysteresis) + currentMode * directionalHysteresis;
    } else if (inMonoMode && targetMonoConfidence < currentMode) {
        // In mono mode, trying to go directional - ADAPTIVE resistance
        // Weak resistance when quiet (let footsteps break out easily)
        // Strong resistance when loud (keep music as music)

        // Use energy scale to determine resistance (calculated earlier in the function)
        // energyScale is calculated around line 316, but we need it here
        // We'll use m_recentEnergyLevel which is already available
        float energyBasedResistance = std::clamp(m_recentEnergyLevel / 0.4f, 0.0f, 1.0f);

        // Scale hysteresis: quiet = minimal, loud = full
        float monoHysteresis = baseHysteresisFactor * energyBasedResistance;
        targetMonoConfidence = targetMonoConfidence * (1.0f - monoHysteresis) + currentMode * monoHysteresis;
    }

    // Faster response for directional changes (was 0.95/0.05)
    m_monoContentConfidence = m_monoContentConfidence * 0.92f + targetMonoConfidence * 0.08f;

    // Update last mode state
    m_lastModeState = m_monoContentConfidence;

    // Poll mouse position for correlation analysis
    m_previousMouse = m_currentMouse;
    GetCursorPos(&m_currentMouse.position);
    m_currentMouse.timestamp = GetTickCount();

    // Find single dominant direction using center of mass
    float weightedSumX = 0.0f;
    float weightedSumY = 0.0f;
    float totalWeight = 0.0f;

    for (size_t i = 0; i < AudioCapture::SpectrumBins; ++i) {
        float angle = (static_cast<float>(i) / static_cast<float>(AudioCapture::SpectrumBins)) * 2.0f * std::numbers::pi_v<float>;
        float weight = spectrum[i] * spectrum[i]; // Square for emphasis

        weightedSumX += std::cos(angle) * weight;
        weightedSumY += std::sin(angle) * weight;
        totalWeight += weight;
    }

    // Calculate dominant angle
    float targetAngle = 0.0f;
    if (totalWeight > 0.001f) {
        targetAngle = std::atan2(weightedSumY, weightedSumX);
        if (targetAngle < 0.0f) targetAngle += 2.0f * std::numbers::pi_v<float>;
        targetAngle = targetAngle * 180.0f / std::numbers::pi_v<float>; // Convert to degrees
    }

    // Smooth angle transitions to prevent jitter
    // Handle wrap-around at 0/360 degrees
    float angleDiff = targetAngle - m_smoothedDominantAngle;
    if (angleDiff > 180.0f) angleDiff -= 360.0f;
    if (angleDiff < -180.0f) angleDiff += 360.0f;

    m_smoothedDominantAngle += angleDiff * (1.0f - kAngleSmoothingFactor);
    if (m_smoothedDominantAngle < 0.0f) m_smoothedDominantAngle += 360.0f;
    if (m_smoothedDominantAngle >= 360.0f) m_smoothedDominantAngle -= 360.0f;

    // Calculate overall energy level (for mono scaling)
    float totalEnergy = 0.0f;
    for (size_t i = 0; i < AudioCapture::SpectrumBins; ++i) {
        totalEnergy += spectrum[i];
    }
    float avgEnergy = totalEnergy / static_cast<float>(AudioCapture::SpectrumBins);
    float targetEnergyScale = std::clamp(avgEnergy * 3.0f, 0.0f, 1.0f); // Increased multiplier for more visibility

    // Smooth energy changes
    m_smoothedEnergyScale = m_smoothedEnergyScale * kEnergySmoothingFactor + targetEnergyScale * (1.0f - kEnergySmoothingFactor);

    float dominantAngle = m_smoothedDominantAngle;
    float energyScale = m_smoothedEnergyScale;

    // Calculate adaptive gain for quiet sounds
    // Track overall energy level (smoothed)
    m_recentEnergyLevel = m_recentEnergyLevel * 0.9f + energyScale * 0.1f;

    // When overall energy is low, boost sensitivity
    // When energy is high, reduce boost (avoid clipping)
    float baseAdaptiveGain = 1.0f;
    if (m_recentEnergyLevel < 0.3f) {
        // Quiet overall - boost significantly
        // Scale from 1.0x at 0.3 energy to 3.5x at 0.0 energy
        baseAdaptiveGain = 1.0f + (0.3f - m_recentEnergyLevel) / 0.3f * 2.5f;
    }

    // Apply directional multiplier - boost directional sounds more than mono
    // Low mono confidence (directional) = extra boost for footsteps
    // High mono confidence (mono) = less boost
    float directionalBoost = 1.0f + (1.0f - m_monoContentConfidence) * 1.2f; // 1.0x to 2.2x (increased from 1.8x)

    // Combine adaptive gain with directional boost
    float targetGain = baseAdaptiveGain * directionalBoost;
    targetGain = std::clamp(targetGain, 1.0f, 8.0f); // Cap at 8x boost (increased from 6x)

    // Smooth adaptive gain to prevent jarring changes
    m_adaptiveGain = m_adaptiveGain * 0.92f + targetGain * 0.08f;

    // Apply adaptive gain to energy scale for rendering
    // This boosts quiet directional sounds (footsteps) significantly
    energyScale = std::clamp(energyScale * m_adaptiveGain, 0.0f, 1.5f);

    // Update correlation tracking for front/back disambiguation
    if (m_previousMouse.timestamp != 0) { // Have previous sample
        DWORD timeDelta = m_currentMouse.timestamp - m_previousMouse.timestamp;
        if (timeDelta > 0 && timeDelta < 500) { // Reasonable frame time (2-500ms)
            float mouseDeltaX = static_cast<float>(m_currentMouse.position.x - m_previousMouse.position.x);

            // Only track if there's meaningful mouse movement
            if (std::abs(mouseDeltaX) > 1.0f) {
                // Calculate angle change from previous frame
                // Note: we stored dominantAngle in m_smoothedDominantAngle
                static float s_previousAngle = dominantAngle;
                float angleChange = dominantAngle - s_previousAngle;
                // Handle wrap-around
                if (angleChange > 180.0f) angleChange -= 360.0f;
                if (angleChange < -180.0f) angleChange += 360.0f;

                // Calculate energy change
                static float s_previousEnergy = energyScale;
                float energyChange = energyScale - s_previousEnergy;

                // Store correlation sample
                CorrelationSample sample;
                sample.mouseDeltaX = mouseDeltaX;
                sample.angleChange = angleChange;
                sample.energyChange = energyChange;
                sample.timestamp = m_currentMouse.timestamp;
                m_correlationHistory.push_back(sample);

                // Keep history bounded
                if (m_correlationHistory.size() > kCorrelationHistorySize) {
                    m_correlationHistory.erase(m_correlationHistory.begin());
                }

                s_previousAngle = dominantAngle;
                s_previousEnergy = energyScale;
            }
        }
    }

    // Compute correlation coefficient over recent history
    if (m_correlationHistory.size() >= 10) { // Need minimum samples
        DWORD cutoffTime = m_currentMouse.timestamp - kCorrelationWindowMs;

        // Calculate correlation between mouse movement and angle following
        // Positive correlation: angle follows mouse = sound is in front
        // Negative/zero correlation: angle doesn't follow = sound might be behind
        float sumMouseAngle = 0.0f;
        float sumMouse2 = 0.0f;
        float sumAngle2 = 0.0f;
        int count = 0;

        for (const auto& sample : m_correlationHistory) {
            if (sample.timestamp >= cutoffTime) {
                // Normalize mouse and angle to similar scales
                float normalizedMouse = sample.mouseDeltaX / 100.0f; // Typical mouse movement range
                float normalizedAngle = sample.angleChange / 10.0f;  // Typical angle change range

                sumMouseAngle += normalizedMouse * normalizedAngle;
                sumMouse2 += normalizedMouse * normalizedMouse;
                sumAngle2 += normalizedAngle * normalizedAngle;
                count++;
            }
        }

        if (count > 5 && sumMouse2 > 0.001f && sumAngle2 > 0.001f) {
            float correlation = sumMouseAngle / (std::sqrt(sumMouse2) * std::sqrt(sumAngle2));
            correlation = std::clamp(correlation, -1.0f, 1.0f);

            // EXTREMELY heavy smoothing to prevent jitter on the smaller peak
            float targetConfidence = correlation;
            m_frontBackConfidence = m_frontBackConfidence * 0.985f + targetConfidence * 0.015f;
        }
    }

    // Calculate target peak scale factors ONCE per frame (before loop)
    // Apply adaptive sizing based on front/back confidence
    float targetFrontPeakScale = 1.0f;
    float targetBackPeakScale = 1.0f;

    // Add dead zone to prevent tiny fluctuations from causing changes
    if (m_frontBackConfidence > 0.15f) {
        // Confident that sound is in front: reduce back peak
        float confidence = std::clamp(m_frontBackConfidence, 0.0f, 1.0f);
        targetBackPeakScale = 0.3f + 0.7f * (1.0f - confidence);
    } else if (m_frontBackConfidence < -0.15f) {
        // Confident that sound is behind: reduce front peak
        float confidence = std::clamp(-m_frontBackConfidence, 0.0f, 1.0f);
        targetFrontPeakScale = 0.3f + 0.7f * (1.0f - confidence);
    }

    // Smoothing for peak scaling factors (temporal smoothing, once per frame)
    static float s_smoothedFrontScale = 1.0f;
    static float s_smoothedBackScale = 1.0f;
    s_smoothedFrontScale = s_smoothedFrontScale * 0.97f + targetFrontPeakScale * 0.03f;
    s_smoothedBackScale = s_smoothedBackScale * 0.97f + targetBackPeakScale * 0.03f;

    // Determine rendering mode based on stereo width
    // Low stereo width = mono/centered audio = show dual peaks (front + back mirror)
    // High stereo width = panned audio = show single peak
    // dualPeakBlend: 0.0 = fully stereo (single peak), 1.0 = fully mono (dual peaks)
    float dualPeakBlend = std::clamp((kStereoWidthThreshold - stereoWidth) / kStereoWidthThreshold, 0.0f, 1.0f);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }

    auto* vertices = reinterpret_cast<Vertex*>(mapped.pData);
    const float centerX = m_width * 0.5f;
    const float centerY = m_height * 0.5f;
    const float minAxis = std::min(m_width, m_height);

    const float baseInner = minAxis * kBaseRadiusRatio;
    const float baseThickness = minAxis * kRingThicknessRatio;
    const float baseOuter = baseInner + baseThickness;
    const float twoPi = std::numbers::pi_v<float> * 2.0f;

    // Don't use hard switch - instead interpolate smoothly between modes
    // monoBlend: 0.0 = fully directional, 1.0 = fully mono
    float monoBlend = std::clamp(m_monoContentConfidence, 0.0f, 1.0f);

    // For mono rendering, calculate uniform scale based on overall energy
    float monoUniformScale = energyScale; // Use energy scale directly

    for (size_t i = 0; i < kRenderSegments; ++i) {
        // Calculate angle for this segment
        float segmentAngle = (static_cast<float>(i) / static_cast<float>(kRenderSegments)) * 360.0f;

        // Calculate distance from dominant direction
        float segmentAngleDiff = std::abs(segmentAngle - dominantAngle);
        if (segmentAngleDiff > 180.0f) segmentAngleDiff = 360.0f - segmentAngleDiff; // Wrap around

        // Sharp gaussian falloff for distinct peak
        float normalizedDiff = segmentAngleDiff / kPeakWidthDegrees;
        float peakHighlight = std::exp(-normalizedDiff * normalizedDiff * kPeakSharpness);

        // For mono/centered audio, add mirror peak at 180 degrees
        float mirrorAngle = dominantAngle + 180.0f;
        if (mirrorAngle >= 360.0f) mirrorAngle -= 360.0f;

        float mirrorAngleDiff = std::abs(segmentAngle - mirrorAngle);
        if (mirrorAngleDiff > 180.0f) mirrorAngleDiff = 360.0f - mirrorAngleDiff;

        float mirrorNormalizedDiff = mirrorAngleDiff / kPeakWidthDegrees;
        float mirrorPeakHighlight = std::exp(-mirrorNormalizedDiff * mirrorNormalizedDiff * kPeakSharpness);

        // Apply the pre-calculated smoothed scale factors to this segment's peak highlights
        float scaledFrontPeak = peakHighlight * s_smoothedFrontScale;
        float scaledBackPeak = mirrorPeakHighlight * s_smoothedBackScale;

        // Blend between single peak and dual peaks based on stereo width
        float finalPeakHighlight = scaledFrontPeak + scaledBackPeak * dualPeakBlend;
        finalPeakHighlight = std::min(finalPeakHighlight, 1.0f); // Cap at 1.0

        // Calculate both rendering modes
        // Mono mode: uniform ring growth (reduced intensity - was 0.5f)
        float monoGrowth = monoUniformScale * 0.25f; // Much more subtle
        float monoOuterRadius = baseOuter + minAxis * kAmplitudeScale * monoGrowth;

        // Directional mode: peak-based rendering
        float directionalGrowth = finalPeakHighlight * energyScale;
        float directionalOuterRadius = baseOuter + minAxis * kAmplitudeScale * directionalGrowth;

        // Smoothly interpolate between the two modes
        float outerRadius = directionalOuterRadius * (1.0f - monoBlend) + monoOuterRadius * monoBlend;
        float innerRadius = baseInner;

        float angle0 = (static_cast<float>(i) / static_cast<float>(kRenderSegments)) * twoPi;
        float angle1 = (static_cast<float>(i + 1) / static_cast<float>(kRenderSegments)) * twoPi;

        float cos0 = std::cos(angle0);
        float sin0 = std::sin(angle0);
        float cos1 = std::cos(angle1);
        float sin1 = std::sin(angle1);

        auto setVertex = [&](Vertex& vert, float radius, float sine, float cosine) {
            vert.position[0] = centerX + sine * radius;
            vert.position[1] = centerY - cosine * radius;
            // Constant color everywhere - NO variation at all
            vert.color[0] = 0.3f;
            vert.color[1] = 0.5f;
            vert.color[2] = 1.0f;
            vert.color[3] = kConstantAlpha; // Constant alpha
        };


        Vertex* v = vertices + i * 6;

        setVertex(v[0], innerRadius, sin0, cos0);
        setVertex(v[1], outerRadius, sin0, cos0);
        setVertex(v[2], outerRadius, sin1, cos1);

        setVertex(v[3], innerRadius, sin0, cos0);
        setVertex(v[4], outerRadius, sin1, cos1);
        setVertex(v[5], innerRadius, sin1, cos1);
    }

    context->Unmap(m_vertexBuffer.Get(), 0);
}

void RingRenderer::renderWithStereo(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum, float leftRMS, float rightRMS, float stereoWidth, float channelCorrelation, float stereoWidthVariance) {
    if (!context || !m_vertexBuffer || !m_stereoCircleBuffer || !m_constantBuffer) {
        return;
    }

    // First render the ring
    updateVertexBuffer(context, spectrum, stereoWidth, channelCorrelation, stereoWidthVariance);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto transform = reinterpret_cast<Transform*>(mapped.pData);
        transform->resolution[0] = m_width;
        transform->resolution[1] = m_height;
        transform->padding[0] = 0.0f;
        transform->padding[1] = 0.0f;
        context->Unmap(m_constantBuffer.Get(), 0);
    }

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    context->RSSetState(m_rasterState.Get());

    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // Draw ring
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->Draw(m_vertexCount, 0);

    // Then render stereo circles on top
    updateStereoCircles(context, leftRMS, rightRMS);
    context->IASetVertexBuffers(0, 1, m_stereoCircleBuffer.GetAddressOf(), &stride, &offset);
    context->Draw(m_stereoCircleVertexCount, 0);
}

void RingRenderer::updateStereoCircles(ID3D11DeviceContext* context, float leftRMS, float rightRMS) {
    if (!context || !m_stereoCircleBuffer) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_stereoCircleBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }

    constexpr size_t kCircleSegments = 32;
    auto* vertices = reinterpret_cast<Vertex*>(mapped.pData);

    const float centerX = m_width * 0.5f;
    const float centerY = m_height * 0.5f;
    const float minAxis = std::min(m_width, m_height);

    // Circle parameters - sized to fit inside ring without overlap
    constexpr float kBaseCircleRadius = 0.025f;  // Minimum radius when silent
    constexpr float kMaxCircleRadius = 0.06f;    // Maximum radius at full volume - more responsive (was 0.04f)
    constexpr float kCircleSpacing = 0.04f;      // Distance from center - closer together

    // Ring inner radius is 0.10, so circles at ±0.04 with max radius 0.06 = 0.10 total, fits inside

    // Calculate dynamic radii based on RMS - amplified for more sensitivity
    float leftAmplified = std::pow(leftRMS, 0.5f) * 1.5f; // Square root makes quiet sounds more visible, then amplify
    float rightAmplified = std::pow(rightRMS, 0.5f) * 1.5f;

    float leftRadius = minAxis * (kBaseCircleRadius + std::clamp(leftAmplified, 0.0f, 1.0f) * (kMaxCircleRadius - kBaseCircleRadius));
    float rightRadius = minAxis * (kBaseCircleRadius + std::clamp(rightAmplified, 0.0f, 1.0f) * (kMaxCircleRadius - kBaseCircleRadius));

    // Position circles side-by-side horizontally, inside the ring
    float leftCenterX = centerX - (minAxis * kCircleSpacing);
    float leftCenterY = centerY;
    float rightCenterX = centerX + (minAxis * kCircleSpacing);
    float rightCenterY = centerY;

    auto generateCircle = [&](Vertex* verts, float cx, float cy, float radius, float /*rms*/) {
        float alpha = kConstantAlpha; // Constant alpha - no pulsing

        for (size_t i = 0; i < kCircleSegments; ++i) {
            float angle1 = (static_cast<float>(i) / kCircleSegments) * 2.0f * std::numbers::pi_v<float>;
            float angle2 = (static_cast<float>(i + 1) / kCircleSegments) * 2.0f * std::numbers::pi_v<float>;

            float x1 = cx + std::cos(angle1) * radius;
            float y1 = cy + std::sin(angle1) * radius;
            float x2 = cx + std::cos(angle2) * radius;
            float y2 = cy + std::sin(angle2) * radius;

            // Triangle fan from center
            size_t baseIdx = i * 3;

            // Center vertex - match ring color
            verts[baseIdx].position[0] = cx;
            verts[baseIdx].position[1] = cy;
            verts[baseIdx].color[0] = 0.3f;
            verts[baseIdx].color[1] = 0.5f;
            verts[baseIdx].color[2] = 1.0f;
            verts[baseIdx].color[3] = alpha;

            // First edge vertex - match ring color
            verts[baseIdx + 1].position[0] = x1;
            verts[baseIdx + 1].position[1] = y1;
            verts[baseIdx + 1].color[0] = 0.3f;
            verts[baseIdx + 1].color[1] = 0.5f;
            verts[baseIdx + 1].color[2] = 1.0f;
            verts[baseIdx + 1].color[3] = alpha; // Same constant alpha

            // Second edge vertex - match ring color
            verts[baseIdx + 2].position[0] = x2;
            verts[baseIdx + 2].position[1] = y2;
            verts[baseIdx + 2].color[0] = 0.3f;
            verts[baseIdx + 2].color[1] = 0.5f;
            verts[baseIdx + 2].color[2] = 1.0f;
            verts[baseIdx + 2].color[3] = alpha; // Same constant alpha
        }
    };

    // Generate left circle
    generateCircle(vertices, leftCenterX, leftCenterY, leftRadius, leftRMS);

    // Generate right circle (offset in vertex buffer)
    generateCircle(vertices + kCircleSegments * 3, rightCenterX, rightCenterY, rightRadius, rightRMS);

    context->Unmap(m_stereoCircleBuffer.Get(), 0);
}

} // namespace sv::overlay::ring
