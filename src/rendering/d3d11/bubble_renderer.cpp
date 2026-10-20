#include "rendering/d3d11/bubble_renderer.h"

#include <cmath>

#include "diagnostics/log.h"

namespace dp {

using Microsoft::WRL::ComPtr;

bool BubbleRenderer::Init(ID2D1DeviceContext* d2d, IDWriteFactory* dwrite) {
    Release();
    HRESULT hr = d2d->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.10f, 0.09f, 1.0f), &text_);
    if (SUCCEEDED(hr)) hr = d2d->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.99f, 0.95f, 0.96f), &back_);
    if (SUCCEEDED(hr)) hr = d2d->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.25f, 0.18f, 0.9f), &border_);
    if (FAILED(hr)) {
        log::Error("BubbleRenderer 초기화 실패 {}", log::HrToString(hr));
        Release();
        return false;
    }
    d2d_ = d2d;
    dwrite_ = dwrite;
    return true;
}

void BubbleRenderer::Release() {
    formats_.clear();
    text_.Reset();
    back_.Reset();
    border_.Reset();
    d2d_ = nullptr;
    dwrite_ = nullptr;
}

IDWriteTextFormat* BubbleRenderer::Format(float fontPx) {
    const int key = static_cast<int>(std::lround(fontPx * 4));
    auto& f = formats_[key];
    if (!f) {
        // Segoe UI 에 없는 한글은 DirectWrite 기본 폰트 대체(맑은 고딕 등)로 그린다
        if (FAILED(dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, fontPx, L"ko-kr", &f)))
            return nullptr;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    return f.Get();
}

D2D1_SIZE_F BubbleRenderer::Measure(const std::wstring& text, float fontPx, float maxWidthPx) {
    IDWriteTextFormat* fmt = dwrite_ ? Format(fontPx) : nullptr;
    if (!fmt) return {0, 0};
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, maxWidthPx, 10000.0f, &layout)))
        return {0, 0};
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return {std::ceil(m.widthIncludingTrailingWhitespace), std::ceil(m.height)};
}

HRESULT BubbleRenderer::Draw(const FrameTarget& t, const std::wstring& text, float fontPx, float padding, float tail) {
    if (!d2d_ || !t.d2dTarget) return E_POINTER;
    IDWriteTextFormat* fmt = Format(fontPx);
    if (!fmt) return E_FAIL;
    const float w = static_cast<float>(t.width), h = static_cast<float>(t.height);
    d2d_->SetTarget(t.d2dTarget);
    d2d_->BeginDraw();
    d2d_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(t.offset.x), static_cast<float>(t.offset.y)));
    const D2D1_RECT_F box = D2D1::RectF(1, 1, w - 1, h - tail - 1);
    const float r = fontPx * 0.6f;
    d2d_->FillRoundedRectangle(D2D1::RoundedRect(box, r, r), back_.Get());
    d2d_->DrawRoundedRectangle(D2D1::RoundedRect(box, r, r), border_.Get(), 1.2f);
    // 꼬리 (가운데 아래)
    ComPtr<ID2D1Factory> factory;
    d2d_->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> tri;
    if (tail > 0 && SUCCEEDED(factory->CreatePathGeometry(&tri))) {
        ComPtr<ID2D1GeometrySink> sink;
        tri->Open(&sink);
        const float cx = w / 2;
        sink->BeginFigure(D2D1::Point2F(cx - tail, box.bottom - 1), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(cx, h - 1));
        sink->AddLine(D2D1::Point2F(cx + tail, box.bottom - 1));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        d2d_->FillGeometry(tri.Get(), back_.Get());
    }
    const D2D1_RECT_F textRect = D2D1::RectF(box.left + padding, box.top + padding, box.right - padding, box.bottom - padding);
    d2d_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, textRect, text_.Get(),
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
    HRESULT hr = d2d_->EndDraw();
    d2d_->SetTarget(nullptr);
    return hr;
}

}  // namespace dp
