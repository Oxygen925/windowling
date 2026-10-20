#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>

#include "rendering/d3d11/composition_target.h"

namespace dp {

// E8: 깊이 버퍼 + 직교 카메라 + premultiplied 투명 합성 확인용 회전 큐브.
// 한 면은 반투명(알파 0.5)으로 블렌딩 경계를 확인한다.
class CubeRenderer {
public:
    bool Init(ID3D11Device* device);
    void Release();
    // rect: target 기준 픽셀 영역. target.dsv 가 필요하다.
    void Draw(ID3D11DeviceContext* ctx, const FrameTarget& target, const RECT& rect, float angleRad);

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb_, ib_, cb_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
};

}  // namespace dp
