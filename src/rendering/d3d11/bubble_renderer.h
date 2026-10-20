#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <map>
#include <string>

#include "rendering/d3d11/composition_target.h"

namespace dp {

// 말풍선 (P3-07): 같은 D3D11 Device 위 D2D/DirectWrite. 여러 줄 줄바꿈, 크기 측정, 둥근 배경 + 꼬리
class BubbleRenderer {
public:
    bool Init(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite);
    void Release();
    bool Ready() const { return d2d_ != nullptr; }
    // 텍스트 상자 크기(패딩 제외, 픽셀). maxWidth 안에서 줄바꿈
    D2D1_SIZE_F Measure(const std::wstring& text, float fontPx, float maxWidthPx);
    // target 전체에 말풍선을 그린다 (아래쪽 tail 높이만큼은 꼬리)
    HRESULT Draw(const FrameTarget& target, const std::wstring& text, float fontPx, float padding, float tail);

private:
    IDWriteTextFormat* Format(float fontPx);
    ID2D1DeviceContext* d2d_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    std::map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_, back_, border_;
};

}  // namespace dp
