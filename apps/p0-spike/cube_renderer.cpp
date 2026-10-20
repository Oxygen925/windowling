#include "cube_renderer.h"

#include <DirectXMath.h>

#include "diagnostics/log.h"
#include "shaders/g_CubePS.h"
#include "shaders/g_CubeVS.h"

namespace dp {

using namespace DirectX;

namespace {
struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT4 color;  // premultiplied
};
}  // namespace

bool CubeRenderer::Init(ID3D11Device* device) {
    Release();
    HRESULT hr = device->CreateVertexShader(g_CubeVS, sizeof(g_CubeVS), nullptr, &vs_);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_CubePS, sizeof(g_CubePS), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (SUCCEEDED(hr)) hr = device->CreateInputLayout(il, 2, g_CubeVS, sizeof(g_CubeVS), &layout_);

    // 면별 색 (마지막 면은 알파 0.5, premultiplied)
    const XMFLOAT4 faceColor[6] = {{0.95f, 0.35f, 0.30f, 1}, {0.30f, 0.80f, 0.40f, 1}, {0.30f, 0.50f, 0.95f, 1},
                                   {0.95f, 0.85f, 0.30f, 1}, {0.80f, 0.40f, 0.90f, 1}, {0.15f, 0.45f, 0.45f, 0.5f}};
    const XMFLOAT3 n[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    Vertex verts[24];
    uint16_t idx[36];
    for (int f = 0; f < 6; ++f) {
        XMVECTOR normal = XMLoadFloat3(&n[f]);
        XMVECTOR up = std::abs(n[f].y) > 0.5f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Cross(up, normal);
        for (int v = 0; v < 4; ++v) {
            float su = (v & 1) ? 0.5f : -0.5f, sv = (v & 2) ? 0.5f : -0.5f;
            XMVECTOR p = normal * 0.5f + right * su + up * sv;
            XMStoreFloat3(&verts[f * 4 + v].pos, p);
            verts[f * 4 + v].color = faceColor[f];
        }
        const uint16_t b = static_cast<uint16_t>(f * 4);
        const uint16_t q[6] = {b, static_cast<uint16_t>(b + 1), static_cast<uint16_t>(b + 2),
                               static_cast<uint16_t>(b + 2), static_cast<uint16_t>(b + 1), static_cast<uint16_t>(b + 3)};
        memcpy(idx + f * 6, q, sizeof(q));
    }
    D3D11_BUFFER_DESC bd{sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA sd{verts, 0, 0};
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, &sd, &vb_);
    bd = {sizeof(idx), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    sd = {idx, 0, 0};
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, &sd, &ib_);
    bd = {sizeof(XMFLOAT4X4), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&bd, nullptr, &cb_);

    D3D11_BLEND_DESC bl{};
    auto& rt = bl.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = device->CreateBlendState(&bl, &blend_);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // 반투명 면 뒤쪽도 보이도록
    rd.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rd, &raster_);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&dd, &depth_);

    if (FAILED(hr)) {
        log::Error("CubeRenderer 초기화 실패 {}", log::HrToString(hr));
        return false;
    }
    return true;
}

void CubeRenderer::Release() {
    vs_.Reset();
    ps_.Reset();
    layout_.Reset();
    vb_.Reset();
    ib_.Reset();
    cb_.Reset();
    blend_.Reset();
    raster_.Reset();
    depth_.Reset();
}

void CubeRenderer::Draw(ID3D11DeviceContext* ctx, const FrameTarget& t, const RECT& r, float angle) {
    if (!vs_ || !t.dsv) return;
    const float w = static_cast<float>(r.right - r.left), h = static_cast<float>(r.bottom - r.top);
    D3D11_VIEWPORT vp{static_cast<float>(t.offset.x + r.left), static_cast<float>(t.offset.y + r.top), w, h, 0, 1};
    // 직교 카메라 (계획서 §5.3)
    XMMATRIX world = XMMatrixRotationY(angle) * XMMatrixRotationX(0.45f + 0.25f * std::sin(angle * 0.7f));
    XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0, 0, -3, 1), XMVectorZero(), XMVectorSet(0, 1, 0, 0));
    XMMATRIX proj = XMMatrixOrthographicLH(1.9f * w / h, 1.9f, 0.1f, 10.0f);
    XMFLOAT4X4 mvp;
    XMStoreFloat4x4(&mvp, XMMatrixTranspose(world * view * proj));

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &mvp, sizeof(mvp));
    ctx->Unmap(cb_.Get(), 0);

    UINT stride = sizeof(Vertex), offset = 0;
    ctx->OMSetRenderTargets(1, &t.rtv, t.dsv);
    ctx->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    ctx->RSSetState(raster_.Get());
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(layout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ctx->DrawIndexed(36, 0, 0);
}

}  // namespace dp
