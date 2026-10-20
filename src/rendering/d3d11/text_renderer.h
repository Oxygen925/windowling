#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

#include "rendering/d3d11/composition_target.h"

namespace dp {

// P0-11: 동일 D3D11 Device 위의 D2D/DirectWrite 텍스트 1줄 (말풍선 경로 사전 검증)
class TextRenderer {
public:
    bool Init(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float fontPx);
    void Release();
    // rect: target 기준 픽셀. D2DERR_RECREATE_TARGET 등은 호출자가 Device Lost로 처리
    HRESULT Draw(const FrameTarget& target, const D2D1_RECT_F& rect, const std::wstring& text);
    float FontPx() const { return fontPx_; }

private:
    ID2D1DeviceContext* d2d_ = nullptr;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_, backBrush_;
    float fontPx_ = 0;
};

}  // namespace dp
