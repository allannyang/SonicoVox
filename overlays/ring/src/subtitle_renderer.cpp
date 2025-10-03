#include "ring/subtitle_renderer.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace sv::overlay::ring {

SubtitleRenderer::SubtitleRenderer() {
}

SubtitleRenderer::~SubtitleRenderer() {
    shutdown();
}

bool SubtitleRenderer::initialize(ID3D11Device* device) {
    HRESULT hr;

    // Create D2D factory
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) {
        m_lastError = L"Failed to create D2D factory";
        return false;
    }

    // Create DirectWrite factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = L"Failed to create DirectWrite factory";
        return false;
    }

    // Get DXGI surface from D3D11 backbuffer
    Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

    // We need to create render target from the swapchain
    // For now, we'll defer this until first render when we have the actual backbuffer
    // This is a simplified approach - in production you'd pass the swapchain

    // Create text format - using Segoe UI Semibold for clean, modern look
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_config.fontSize,
        L"en-us",
        m_textFormat.GetAddressOf()
    );

    if (FAILED(hr)) {
        m_lastError = L"Failed to create text format";
        return false;
    }

    // Center text alignment for perfect centering
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    return true;
}

void SubtitleRenderer::shutdown() {
    m_textFormat.Reset();
    m_dwriteFactory.Reset();
    m_bgBrush.Reset();
    m_textBrush.Reset();
    m_renderTarget.Reset();
    m_d2dFactory.Reset();
}

void SubtitleRenderer::updateText(const std::wstring& text, bool isFinal) {
    if (text.empty()) {
        return;
    }

    // Limit text to last ~60 characters (about 8-10 words) for readability
    const size_t MAX_CHARS = 60;
    std::wstring limitedText = text;
    if (limitedText.length() > MAX_CHARS) {
        // Keep only the last MAX_CHARS characters (most recent words)
        limitedText = L"..." + limitedText.substr(limitedText.length() - MAX_CHARS);
    }

    if (isFinal) {
        // Final result - replace everything
        m_currentSubtitle.text = limitedText;
        m_currentSubtitle.isPending = false;
        m_currentSubtitle.timeSinceUpdate = 0.0f;
        m_currentSubtitle.opacity = 1.0f;

        OutputDebugStringA("[SUBTITLE RENDER] Final: ");
        OutputDebugStringW(limitedText.c_str());
        OutputDebugStringA("\n");
    } else {
        // Partial result - only update if text is longer (accumulating)
        // This prevents flickering from shorter partial results
        if (limitedText.length() >= m_currentSubtitle.text.length() || m_currentSubtitle.timeSinceUpdate > 1.0f) {
            m_currentSubtitle.text = limitedText;
            m_currentSubtitle.isPending = true;
            m_currentSubtitle.timeSinceUpdate = 0.0f;
            m_currentSubtitle.opacity = 1.0f;
        }
    }
}

void SubtitleRenderer::render(ID3D11DeviceContext* context, float deltaTime, float stereoPosition, float audioEnergy) {
    if (!m_config.enabled || m_currentSubtitle.text.empty()) {
        return;
    }

    // Lazy-create render target from context
    if (!m_renderTarget) {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;

        context->OMGetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        if (!rtv) {
            return;
        }

        rtv->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

        HRESULT hr = resource.As(&backBuffer);
        if (FAILED(hr)) {
            return;
        }

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        m_width = static_cast<float>(desc.Width);
        m_height = static_cast<float>(desc.Height);

        Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
        hr = backBuffer.As(&dxgiSurface);
        if (FAILED(hr)) {
            return;
        }

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface.Get(), &props, m_renderTarget.GetAddressOf());
        if (FAILED(hr)) {
            return;
        }

        // Create brushes
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_textBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f), m_bgBrush.GetAddressOf());
    }

    // Update opacity
    updateOpacity(deltaTime, audioEnergy);

    // Render
    renderSubtitle(m_currentSubtitle, stereoPosition);
}

void SubtitleRenderer::setConfig(const SubtitleConfig& config) {
    m_config = config;

    // Update text format if font size changed
    if (m_dwriteFactory && m_textFormat) {
        m_textFormat.Reset();
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            m_config.fontSize,
            L"en-us",
            m_textFormat.GetAddressOf()
        );
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
}

void SubtitleRenderer::resize(float width, float height) {
    m_width = width;
    m_height = height;

    // Force render target recreation on next render
    m_renderTarget.Reset();
    m_textBrush.Reset();
    m_bgBrush.Reset();
}

void SubtitleRenderer::updateOpacity(float deltaTime, float audioEnergy) {
    m_currentSubtitle.timeSinceUpdate += deltaTime;

    if (!m_config.adaptiveOpacity) {
        m_currentSubtitle.opacity = 1.0f;
        return;
    }

    // Shorter display times for snappier subtitle cycling
    const float DISPLAY_TIME = m_currentSubtitle.isPending ? 0.8f : 2.0f;
    const float FADE_TIME = 0.3f;

    if (m_currentSubtitle.timeSinceUpdate < DISPLAY_TIME) {
        // Always full opacity
        m_currentSubtitle.opacity = 1.0f;
    } else {
        // Quick fade out
        float fadeProgress = (m_currentSubtitle.timeSinceUpdate - DISPLAY_TIME) / FADE_TIME;
        m_currentSubtitle.opacity = std::clamp(1.0f - fadeProgress, 0.0f, 1.0f);
    }
}

void SubtitleRenderer::renderSubtitle(const SubtitleState& state, float stereoPosition) {
    if (!m_renderTarget || state.opacity <= 0.0f) {
        return;
    }

    m_renderTarget->BeginDraw();

    // Calculate center position (always centered, no directional shifting)
    float centerX = m_width * 0.5f;
    float bottomY = m_height * (1.0f - m_config.bottomOffset);

    // Limit text width to 50% of screen width (forces wrapping for readability)
    float maxTextWidth = m_width * 0.5f;

    // Create text layout with limited width for wrapping
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    m_dwriteFactory->CreateTextLayout(
        state.text.c_str(),
        static_cast<UINT32>(state.text.length()),
        m_textFormat.Get(),
        maxTextWidth,    // Max 50% screen width
        200.0f,          // Tall enough for multi-line
        textLayout.GetAddressOf()
    );

    // Enable text wrapping
    textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    // Position the text layout box centered on screen
    // With CENTER alignment, text renders centered within this box
    float layoutX = centerX - (maxTextWidth * 0.5f);
    float layoutY = bottomY - metrics.height - 10.0f;

    // Background rectangle - centered around the layout area
    const float padding = 15.0f;
    D2D1_RECT_F bgRect = D2D1::RectF(
        centerX - (metrics.width * 0.5f) - padding,
        layoutY - padding,
        centerX + (metrics.width * 0.5f) + padding,
        layoutY + metrics.height + padding
    );

    // Colors
    D2D1_COLOR_F bgColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.80f * state.opacity);
    D2D1_COLOR_F textColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, state.opacity);

    // Draw background
    m_bgBrush->SetColor(bgColor);
    m_renderTarget->FillRectangle(bgRect, m_bgBrush.Get());

    // Draw text layout (text will be centered within the layout box)
    m_textBrush->SetColor(textColor);
    D2D1_POINT_2F layoutOrigin = D2D1::Point2F(layoutX, layoutY);
    m_renderTarget->DrawTextLayout(layoutOrigin, textLayout.Get(), m_textBrush.Get());

    // Draw directional arrow if enabled
    if (m_config.directionalHints && std::abs(stereoPosition) > 0.3f) {
        float arrowX = stereoPosition > 0 ? bgRect.right + 20.0f : bgRect.left - 20.0f;
        float arrowY = (bgRect.top + bgRect.bottom) * 0.5f;

        // Simple triangle arrow
        D2D1_POINT_2F arrow[3];
        if (stereoPosition > 0) {
            // Right arrow
            arrow[0] = D2D1::Point2F(arrowX, arrowY);
            arrow[1] = D2D1::Point2F(arrowX - 10.0f, arrowY - 8.0f);
            arrow[2] = D2D1::Point2F(arrowX - 10.0f, arrowY + 8.0f);
        } else {
            // Left arrow
            arrow[0] = D2D1::Point2F(arrowX, arrowY);
            arrow[1] = D2D1::Point2F(arrowX + 10.0f, arrowY - 8.0f);
            arrow[2] = D2D1::Point2F(arrowX + 10.0f, arrowY + 8.0f);
        }

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pathGeometry;
        m_d2dFactory->CreatePathGeometry(pathGeometry.GetAddressOf());

        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        pathGeometry->Open(sink.GetAddressOf());
        sink->BeginFigure(arrow[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(arrow[1]);
        sink->AddLine(arrow[2]);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        m_renderTarget->FillGeometry(pathGeometry.Get(), m_textBrush.Get());
    }

    m_renderTarget->EndDraw();
}

} // namespace sv::overlay::ring
