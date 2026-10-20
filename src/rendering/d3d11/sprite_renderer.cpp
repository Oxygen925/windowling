#include "rendering/d3d11/sprite_renderer.h"

#include "diagnostics/log.h"
#include "shaders/g_SpritePS.h"
#include "shaders/g_SpriteVS.h"

namespace dp {

namespace {
struct SpriteCB {
    float dst[4];
    float src[4];
    float tint[4];
};
}  // namespace

bool SpriteRenderer::Init(ID3D11Device* device, const SpriteSheet& sheet) {
    Release();
    HRESULT hr = device->CreateVertexShader(g_SpriteVS, sizeof(g_SpriteVS), nullptr, &vs_);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_SpritePS, sizeof(g_SpritePS), nullptr, &ps_);

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(SpriteCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, nullptr, &cb_);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = sheet.Width();
    td.Height = sheet.Height();
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // WIC 32bppPBGRA = premultiplied
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{sheet.Pixels(), static_cast<UINT>(sheet.Width() * 4), 0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&td, &init, &tex);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(tex.Get(), nullptr, &srv_);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sd, &sampler_);

    D3D11_BLEND_DESC bl{};
    auto& rt = bl.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;  // premultiplied over
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = device->CreateBlendState(&bl, &blend_);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rd, &raster_);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&dd, &noDepth_);

    if (FAILED(hr)) {
        log::Error("SpriteRenderer 초기화 실패 {}", log::HrToString(hr));
        return false;
    }
    sheetW_ = sheet.Width();
    sheetH_ = sheet.Height();
    frameW_ = sheet.FrameWidth();
    return true;
}

void SpriteRenderer::Release() {
    vs_.Reset();
    ps_.Reset();
    cb_.Reset();
    srv_.Reset();
    sampler_.Reset();
    blend_.Reset();
    raster_.Reset();
    noDepth_.Reset();
}

void SpriteRenderer::Draw(ID3D11DeviceContext* ctx, const FrameTarget& t, const SpritePlacement& p, bool flipX,
                          const float tint[4]) {
    if (!vs_ || p.w <= 0 || p.h <= 0) return;
    // 뷰포트는 target 전체(오프셋 포함), 쿼드는 NDC로 배치
    D3D11_VIEWPORT vp{static_cast<float>(t.offset.x), static_cast<float>(t.offset.y), static_cast<float>(t.width),
                      static_cast<float>(t.height), 0.0f, 1.0f};
    SpriteCB cb{};
    cb.dst[0] = 2.0f * p.x / t.width - 1.0f;
    cb.dst[1] = 1.0f - 2.0f * p.y / t.height;
    cb.dst[2] = 2.0f * (p.x + p.w) / t.width - 1.0f;
    cb.dst[3] = 1.0f - 2.0f * (p.y + p.h) / t.height;
    float u0 = static_cast<float>(p.frame * frameW_) / sheetW_;
    float u1 = static_cast<float>((p.frame + 1) * frameW_) / sheetW_;
    cb.src[0] = flipX ? u1 : u0;
    cb.src[1] = 0.0f;
    cb.src[2] = flipX ? u0 : u1;
    cb.src[3] = 1.0f;
    for (int i = 0; i < 4; ++i) cb.tint[i] = tint[i];

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &cb, sizeof(cb));
    ctx->Unmap(cb_.Get(), 0);

    ctx->OMSetRenderTargets(1, &t.rtv, t.dsv);
    ctx->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(noDepth_.Get(), 0);
    ctx->RSSetState(raster_.Get());
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, cb_.GetAddressOf());  // tint
    ctx->PSSetShaderResources(0, 1, srv_.GetAddressOf());
    ctx->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    ctx->Draw(4, 0);
}

}  // namespace dp
