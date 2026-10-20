#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>

#include "rendering/hit_mask.h"
#include "platform/windows/sprite_sheet.h"
#include "rendering/d3d11/composition_target.h"

namespace dp {

// 스프라이트 시트 1장을 GPU 텍스처로 올리고 프레임 단위로 그린다 (point sampling, premultiplied alpha).
class SpriteRenderer {
public:
    bool Init(ID3D11Device* device, const SpriteSheet& sheet);
    void Release();
    // placement는 target 기준 픽셀 좌표 (surface offset은 내부에서 더한다)
    void Draw(ID3D11DeviceContext* ctx, const FrameTarget& target, const SpritePlacement& p, bool flipX,
              const float tint[4]);

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cb_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> noDepth_;
    int sheetW_ = 0, sheetH_ = 0, frameW_ = 0;
};

}  // namespace dp
