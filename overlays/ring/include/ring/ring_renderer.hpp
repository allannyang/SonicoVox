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

private:
    struct Vertex {
        float position[2];
        float color[4];
    };

    struct Transform {
        float resolution[2];
        float padding[2];
    };

    void updateVertexBuffer(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterState;

    float m_width = 1.0f;
    float m_height = 1.0f;
    UINT m_vertexCount = 0;
};

} // namespace sv::overlay::ring