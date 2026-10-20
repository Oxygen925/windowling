#include "rendering/d3d11/model_renderer.h"

#include <wincodec.h>

#include <algorithm>
#include <cstring>

#include "diagnostics/log.h"
#include "shaders/g_ModelPS.h"
#include "shaders/g_ModelVS.h"

namespace dp {

using Microsoft::WRL::ComPtr;

namespace {

struct FrameCB {
    float viewProj[16];
    float lightDir[4];
    float baseColor[4];
    float params[4];
};
constexpr UINT kMaxBones = 128;

// 이미지(PNG·JPEG) → BGRA(straight alpha) 텍스처
ComPtr<ID3D11ShaderResourceView> DecodeTexture(ID3D11Device* device, const model::Image& img) {
    if (img.bytes.empty()) return nullptr;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return nullptr;
    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(img.bytes.data()), static_cast<DWORD>(img.bytes.size()))))
        return nullptr;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return nullptr;
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0 || w > 2048 || h > 2048) {  // P5-R6
        log::Warn("[model] 텍스처 크기 {}x{} 는 지원 범위(2048) 밖 — 텍스처 없이 그림", w, h);
        return nullptr;
    }
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data()))) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateTexture2D(&td, &init, &tex)) || FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, &srv)))
        return nullptr;
    return srv;
}

}  // namespace

model::Mat4 ModelCamera::ViewProj() const {
    model::Mat4 o;
    const float sx = 2.0f / (worldHeight * aspect), sy = 2.0f / worldHeight, zr = std::max(zMax - zMin, 1e-6f);
    o.m = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -1.0f / zr, 0, -centerX * sx, -bottomY * sy - 1.0f, zMax / zr, 1};
    return o * model::Mat4::RotationY(yaw);
}

// ---------------------------------------------------------------------------
// ModelView
// ---------------------------------------------------------------------------

HRESULT ModelView::Resize(ID3D11Device* device, int width, int height, UINT samples) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_ && samples == samples_ && rtv_) return S_OK;
    Release();
    width_ = width;
    height_ = height;
    samples_ = samples;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = samples;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    HRESULT hr = device->CreateTexture2D(&td, nullptr, &color_);
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(color_.Get(), nullptr, &rtv_);
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&td, nullptr, &depth_);
    if (SUCCEEDED(hr)) hr = device->CreateDepthStencilView(depth_.Get(), nullptr, &dsv_);
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = width;
    sd.Height = height;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&sd, nullptr, &staging_);
    if (FAILED(hr)) Release();
    return hr;
}

void ModelView::Release() {
    rtv_.Reset();
    dsv_.Reset();
    color_.Reset();
    depth_.Reset();
    staging_.Reset();
    width_ = height_ = 0;
    readbackPending_ = false;
}

// ---------------------------------------------------------------------------
// ModelRenderer
// ---------------------------------------------------------------------------

bool ModelRenderer::Init(ID3D11Device* device, const model::Model& model) {
    Release();
    HRESULT hr = device->CreateVertexShader(g_ModelVS, sizeof(g_ModelVS), nullptr, &vs_);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_ModelPS, sizeof(g_ModelPS), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(model::Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(model::Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(model::Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, offsetof(model::Vertex, joints), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(model::Vertex, weights), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (SUCCEEDED(hr)) hr = device->CreateInputLayout(il, 5, g_ModelVS, sizeof(g_ModelVS), &layout_);

    for (const auto& p : model.primitives) {
        Part part;
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = static_cast<UINT>(p.vertices.size() * sizeof(model::Vertex));
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vd{p.vertices.data(), 0, 0};
        if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, &vd, &part.vb);
        bd.ByteWidth = static_cast<UINT>(p.indices.size() * sizeof(uint32_t));
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA id{p.indices.data(), 0, 0};
        if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, &id, &part.ib);
        part.indexCount = static_cast<UINT>(p.indices.size());
        part.material = p.material;
        parts_.push_back(std::move(part));
    }
    for (const auto& img : model.images) textures_.push_back(DecodeTexture(device, img));

    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameCB);
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&cb, nullptr, &frameCb_);
    cb.ByteWidth = sizeof(float) * 16 * kMaxBones;
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&cb, nullptr, &boneCb_);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sd, &linear_);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sd, &point_);

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
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = TRUE;  // glTF: 반시계가 앞면 (직교 투영은 방향을 뒤집지 않는다)
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rd, &cullBack_);
    rd.CullMode = D3D11_CULL_NONE;
    if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rd, &cullNone_);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&dd, &depth_);

    // P5-R3: 4x MSAA (지원하지 않으면 1)
    UINT quality = 0;
    samples_ = SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0 ? 4 : 1;
    if (FAILED(hr)) {
        log::Error("ModelRenderer 초기화 실패 {}", log::HrToString(hr));
        Release();
        return false;
    }
    return true;
}

void ModelRenderer::Release() {
    parts_.clear();
    textures_.clear();
    vs_.Reset();
    ps_.Reset();
    layout_.Reset();
    frameCb_.Reset();
    boneCb_.Reset();
    linear_.Reset();
    point_.Reset();
    blend_.Reset();
    cullBack_.Reset();
    cullNone_.Reset();
    depth_.Reset();
}

HRESULT ModelRenderer::Draw(ID3D11DeviceContext* ctx, const FrameTarget& t, ModelView& view, const model::Model& model,
                            const std::vector<model::Mat4>& world, const std::vector<model::Mat4>& palette,
                            const ModelCamera& cam, bool readback) {
    if (!vs_ || !view.rtv_ || !t.rtv) return S_OK;
    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(view.rtv_.Get(), clear);
    ctx->ClearDepthStencilView(view.dsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(view.width_), static_cast<float>(view.height_), 0.0f, 1.0f};
    ctx->OMSetRenderTargets(1, view.rtv_.GetAddressOf(), view.dsv_.Get());
    ctx->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(layout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[2] = {frameCb_.Get(), boneCb_.Get()};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 1, cbs);

    const model::Mat4 vpm = cam.ViewProj();
    // 빛: 왼쪽 위 앞에서 (카메라 기준이 아니라 모델 좌표 기준, yaw 를 돌려도 같은 쪽이 밝게) [검증: 보기에 자연스러운지]
    float light[3] = {0.35f, -0.75f, -0.55f};
    const float ll = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (float& v : light) v /= ll;

    for (size_t i = 0; i < parts_.size() && i < model.primitives.size(); ++i) {
        const Part& part = parts_[i];
        const model::Primitive& prim = model.primitives[i];
        const model::Material mat = part.material >= 0 && part.material < static_cast<int>(model.materials.size())
                                        ? model.materials[part.material] : model::Material{};
        // 팔레트: 스킨이면 전체, 아니면 노드 월드 행렬 1개
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(boneCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return E_FAIL;
        if (prim.skinned) {
            const size_t n = std::min<size_t>(palette.size(), kMaxBones);
            for (size_t b = 0; b < n; ++b) std::memcpy(static_cast<float*>(m.pData) + b * 16, palette[b].m.data(), 64);
        } else {
            std::memcpy(m.pData, world[prim.node].m.data(), 64);
        }
        ctx->Unmap(boneCb_.Get(), 0);
        FrameCB f{};
        std::memcpy(f.viewProj, vpm.m.data(), 64);
        std::copy_n(light, 3, f.lightDir);
        std::copy_n(mat.baseColor.data(), 4, f.baseColor);
        ID3D11ShaderResourceView* srv = mat.image >= 0 && mat.image < static_cast<int>(textures_.size())
                                            ? textures_[mat.image].Get() : nullptr;
        f.params[0] = mat.alphaCutoff;
        f.params[1] = mat.alphaMode == model::AlphaMode::Mask ? 1.0f : mat.alphaMode == model::AlphaMode::Blend ? 2.0f : 0.0f;
        f.params[2] = srv ? 1.0f : 0.0f;
        if (FAILED(ctx->Map(frameCb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return E_FAIL;
        std::memcpy(m.pData, &f, sizeof(f));
        ctx->Unmap(frameCb_.Get(), 0);
        ctx->RSSetState(mat.doubleSided ? cullNone_.Get() : cullBack_.Get());
        ctx->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* smp = mat.linearFilter ? linear_.Get() : point_.Get();
        ctx->PSSetSamplers(0, 1, &smp);
        const UINT stride = sizeof(model::Vertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, part.vb.GetAddressOf(), &stride, &offset);
        ctx->IASetIndexBuffer(part.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->DrawIndexed(part.indexCount, 0, 0);
    }
    // swapchain 백버퍼로: MSAA 면 resolve, 아니면 복사
    ComPtr<ID3D11Resource> back;
    t.rtv->GetResource(&back);
    if (view.samples_ > 1) ctx->ResolveSubresource(back.Get(), 0, view.color_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    else ctx->CopyResource(back.Get(), view.color_.Get());
    if (readback && view.staging_) {
        ctx->CopyResource(view.staging_.Get(), back.Get());
        view.readbackPending_ = true;
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return S_OK;
}

bool ModelRenderer::ReadAlpha(ID3D11DeviceContext* ctx, ModelView& view, std::vector<uint8_t>& bgra, int& pitch, bool wait) {
    if (!view.readbackPending_ || !view.staging_) return false;
    D3D11_MAPPED_SUBRESOURCE m;
    const HRESULT hr = ctx->Map(view.staging_.Get(), 0, D3D11_MAP_READ, wait ? 0u : static_cast<UINT>(D3D11_MAP_FLAG_DO_NOT_WAIT), &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
    if (FAILED(hr)) {
        view.readbackPending_ = false;
        return false;
    }
    pitch = static_cast<int>(m.RowPitch);
    bgra.assign(static_cast<const uint8_t*>(m.pData), static_cast<const uint8_t*>(m.pData) + m.RowPitch * view.height_);
    ctx->Unmap(view.staging_.Get(), 0);
    view.readbackPending_ = false;
    return true;
}

}  // namespace dp
