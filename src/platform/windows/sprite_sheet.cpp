#include "platform/windows/sprite_sheet.h"

#include <windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include "platform/windows/strings.h"
#include "diagnostics/log.h"

namespace dp {

using Microsoft::WRL::ComPtr;

bool SpriteSheet::Load(const std::wstring& path, int frameWidth) {
    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) {
        log::Error("WIC factory 생성 실패 {}", log::HrToString(hr));
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        log::Error("스프라이트 로드 실패 {} {}", ToUtf8(path), log::HrToString(hr));
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);
    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                          WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (frameWidth <= 0 || w % frameWidth != 0) {
        log::Error("시트 폭 {} 이 프레임 폭 {} 의 배수가 아님", w, frameWidth);
        return false;
    }
    pixels_.resize(static_cast<size_t>(w) * h);
    hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels_.size() * 4), reinterpret_cast<BYTE*>(pixels_.data()));
    if (FAILED(hr)) return false;
    width_ = static_cast<int>(w);
    height_ = static_cast<int>(h);
    frameWidth_ = frameWidth;
    log::Info("스프라이트 시트 {}x{} 프레임 {}개", width_, height_, FrameCount());
    return true;
}

uint8_t SpriteSheet::AlphaAt(int frame, int x, int y) const {
    if (x < 0 || y < 0 || x >= frameWidth_ || y >= height_ || frame < 0 || frame >= FrameCount()) return 0;
    return static_cast<uint8_t>(pixels_[static_cast<size_t>(y) * width_ + frame * frameWidth_ + x] >> 24);
}

}  // namespace dp
