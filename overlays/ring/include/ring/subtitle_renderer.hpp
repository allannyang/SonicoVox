#pragma once

#include "ring/subtitle_config.hpp"
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace sv::overlay::ring {

class SubtitleRenderer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();

    bool initialize(ID3D11Device* device);
    void shutdown();

    // Update subtitle text
    void updateText(const std::wstring& text, bool isFinal);

    // Render subtitles
    // stereoPosition: -1.0 (left) to +1.0 (right), 0.0 (center)
    // audioEnergy: 0.0 (silence) to 1.0 (loud) for adaptive opacity
    void render(ID3D11DeviceContext* context, float deltaTime, float stereoPosition = 0.0f, float audioEnergy = 0.0f);

    // Configuration
    void setConfig(const SubtitleConfig& config);
    const SubtitleConfig& config() const { return m_config; }

    void resize(float width, float height);

private:
    struct SubtitleState {
        std::wstring text;
        float opacity = 0.0f;
        float timeSinceUpdate = 0.0f;
        bool isPending = false; // Partial (true) vs final (false)
    };

    void updateOpacity(float deltaTime, float audioEnergy);
    void renderSubtitle(const SubtitleState& state, float stereoPosition);

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_bgBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;

    SubtitleConfig m_config;
    SubtitleState m_currentSubtitle;

    float m_width = 1.0f;
    float m_height = 1.0f;

    const std::wstring& lastError() const { return m_lastError; }
    std::wstring m_lastError;
};

} // namespace sv::overlay::ring
