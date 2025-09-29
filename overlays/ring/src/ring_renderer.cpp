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
    constexpr float kBaseRadiusRatio = 0.12f;
    constexpr float kRingThicknessRatio = 0.032f;
    constexpr float kAmplitudeScale = 0.085f;
    constexpr size_t kRenderSegments = AudioCapture::SpectrumBins * 2;

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

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
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

    updateVertexBuffer(context, spectrum);

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

void RingRenderer::updateVertexBuffer(ID3D11DeviceContext* context, const std::array<float, AudioCapture::SpectrumBins>& spectrum) {
    if (!context || !m_vertexBuffer) {
        return;
    }

    std::array<float, AudioCapture::SpectrumBins> smoothed{};
    for (size_t i = 0; i < AudioCapture::SpectrumBins; ++i) {
        size_t prev = (i + AudioCapture::SpectrumBins - 1) % AudioCapture::SpectrumBins;
        size_t next = (i + 1) % AudioCapture::SpectrumBins;
        smoothed[i] = (spectrum[prev] + spectrum[i] * 2.0f + spectrum[next]) / 4.0f;
    }

    std::array<float, kRenderSegments> interpolated{};
    for (size_t i = 0; i < kRenderSegments; ++i) {
        float position = (static_cast<float>(i) / static_cast<float>(kRenderSegments)) * static_cast<float>(AudioCapture::SpectrumBins);
        size_t base = static_cast<size_t>(std::floor(position)) % AudioCapture::SpectrumBins;
        size_t nextIndex = (base + 1) % AudioCapture::SpectrumBins;
        float frac = position - std::floor(position);
        interpolated[i] = smoothed[base] * (1.0f - frac) + smoothed[nextIndex] * frac;
    }

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

    for (size_t i = 0; i < kRenderSegments; ++i) {
        float amplitude = std::clamp(interpolated[i], 0.0f, 1.0f);
        float eased = std::pow(amplitude, 0.7f);

        float outerRadius = baseOuter + minAxis * kAmplitudeScale * eased;
        float innerRadius = baseInner - baseThickness * 0.4f * eased;
        innerRadius = std::max(innerRadius, baseInner * 0.55f);

        float angle0 = (static_cast<float>(i) / static_cast<float>(kRenderSegments)) * twoPi;
        float angle1 = (static_cast<float>(i + 1) / static_cast<float>(kRenderSegments)) * twoPi;

        float cos0 = std::cos(angle0);
        float sin0 = std::sin(angle0);
        float cos1 = std::cos(angle1);
        float sin1 = std::sin(angle1);

        auto setVertex = [&](Vertex& vert, float radius, float sine, float cosine, float alphaFactor) {
            vert.position[0] = centerX + sine * radius;
            vert.position[1] = centerY - cosine * radius;
            vert.color[0] = 0.2f + eased * 0.5f;
            vert.color[1] = 0.4f + eased * 0.35f;
            vert.color[2] = 1.0f;
            vert.color[3] = 0.18f + eased * alphaFactor;
        };


        Vertex* v = vertices + i * 6;

        setVertex(v[0], innerRadius, sin0, cos0, 0.55f);
        setVertex(v[1], outerRadius, sin0, cos0, 0.75f);
        setVertex(v[2], outerRadius, sin1, cos1, 0.75f);

        setVertex(v[3], innerRadius, sin0, cos0, 0.55f);
        setVertex(v[4], outerRadius, sin1, cos1, 0.75f);
        setVertex(v[5], innerRadius, sin1, cos1, 0.55f);
    }

    context->Unmap(m_vertexBuffer.Get(), 0);
}

} // namespace sv::overlay::ring
