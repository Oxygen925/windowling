#pragma once
// 3D 모델 렌더러 (P5-04·P5-05·P5-07). 메시·텍스처는 캐릭터마다 한 번만 올리고(ModelRenderer),
// 펫마다 MSAA 대상·readback 버퍼만 따로 둔다(ModelView). 물리는 2D 그대로 — 모델은 펫 사각형 안에 직교 투영으로 그린다.
#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <vector>

#include "model/gltf_model.h"
#include "rendering/d3d11/composition_target.h"

namespace dp {

// 직교 카메라 (펫 창 = 이 영역). 발 기준점 = 아래 가장자리 가운데 (물리의 foot 과 같은 점)
struct ModelCamera {
    float yaw = 0;            // Y 축 회전 (라디안). 오른쪽을 볼 때 +, 왼쪽은 -
    float centerX = 0;        // yaw 적용 후 좌표계
    float bottomY = 0;        // 발 높이
    float worldHeight = 1;    // 창 높이에 해당하는 모델 단위
    float aspect = 1;         // 창 너비 / 높이
    float zMin = -1, zMax = 1;
    model::Mat4 ViewProj() const;
};

// 펫마다: MSAA 색·깊이, 히트 테스트용 readback
class ModelView {
public:
    HRESULT Resize(ID3D11Device* device, int width, int height, UINT samples);
    void Release();
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    friend class ModelRenderer;
    int width_ = 0, height_ = 0;
    UINT samples_ = 1;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color_, depth_, staging_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
    bool readbackPending_ = false;
};

class ModelRenderer {
public:
    bool Init(ID3D11Device* device, const model::Model& model);
    void Release();
    bool Ready() const { return vs_ != nullptr; }
    UINT Samples() const { return samples_; }
    // pose 의 world·palette 로 그리고 swapchain 백버퍼로 resolve. readback=true 면 결과를 staging 으로 복사해 둔다
    HRESULT Draw(ID3D11DeviceContext* ctx, const FrameTarget& target, ModelView& view, const model::Model& model,
                 const std::vector<model::Mat4>& world, const std::vector<model::Mat4>& palette, const ModelCamera& cam,
                 bool readback);
    // 직전 readback 결과(BGRA 행 단위). wait=false 면 GPU 가 아직 끝나지 않았을 때 기다리지 않고 false
    bool ReadAlpha(ID3D11DeviceContext* ctx, ModelView& view, std::vector<uint8_t>& bgra, int& pitch, bool wait);

private:
    struct Part {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vb, ib;
        UINT indexCount = 0;
        int material = -1;
    };
    std::vector<Part> parts_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures_;  // model.images 순서 (실패 = null)
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> frameCb_, boneCb_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_, point_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> cullBack_, cullNone_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    UINT samples_ = 1;
};

}  // namespace dp
