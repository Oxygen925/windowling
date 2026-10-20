#include "rendering/d3d11/text_renderer.h"

#include "diagnostics/log.h"

namespace dp {

bool TextRenderer::Init(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite, float fontPx) {
    Release();
    d2d_ = d2d;
    fontPx_ = fontPx;
    HRESULT hr = dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, fontPx, L"ko-kr", &format_);
    if (SUCCEEDED(hr)) {
        format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    if (SUCCEEDED(hr)) hr = d2d->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.08f, 0.06f, 1.0f), &textBrush_);
    if (SUCCEEDED(hr)) hr = d2d->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.98f, 0.92f, 0.92f), &backBrush_);
    if (FAILED(hr)) {
        log::Error("TextRenderer 초기화 실패 {}", log::HrToString(hr));
        return false;
    }
    return true;
}

void TextRenderer::Release() {
    format_.Reset();
    textBrush_.Reset();
    backBrush_.Reset();
    d2d_ = nullptr;
}

HRESULT TextRenderer::Draw(const FrameTarget& t, const D2D1_RECT_F& rect, const std::wstring& text) {
    if (!d2d_ || !t.d2dTarget) return E_POINTER;
    d2d_->SetTarget(t.d2dTarget);
    d2d_->BeginDraw();
    // surface 모드에서는 아틀라스 오프셋만큼 이동, 이 영역 밖으로 그리지 않도록 클립
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(t.offset.x), static_cast<float>(t.offset.y)));
    d2d_->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(t.width), static_cast<float>(t.height)),
                              D2D1_ANTIALIAS_MODE_ALIASED);
    const float radius = fontPx_ * 0.5f;
    d2d_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), backBrush_.Get());
    d2d_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format_.Get(), rect, textBrush_.Get());
    d2d_->PopAxisAlignedClip();
    HRESULT hr = d2d_->EndDraw();
    d2d_->SetTarget(nullptr);
    return hr;
}

}  // namespace dp
