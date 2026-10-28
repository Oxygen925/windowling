#include "bubble.h"

#include <algorithm>
#include <cmath>

#include "diagnostics/log.h"
#include "dp_version.h"

namespace dp {

namespace {
uint64_t g_draws = 0;
constexpr wchar_t kBubbleClass[] = DP_CLASS_PREFIX L"Bubble";
constexpr float kMaxWidth = 320, kFont = 14, kPadding = 9, kTail = 8;  // 96 DPI 기준

LRESULT CALLBACK BubbleProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;  // 입력은 항상 뒤로
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

uint64_t SpeechBubble::DrawCount() { return g_draws; }

void SpeechBubble::RegisterClass(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = BubbleProc;
    wc.hInstance = instance;
    wc.lpszClassName = kBubbleClass;
    RegisterClassExW(&wc);
}

void SpeechBubble::SetText(const std::wstring& text) {
    if (text == text_ && active_) return;
    text_ = text;
    active_ = true;
    dirty_ = true;
    hideAt = 0;
}

void SpeechBubble::Hide() {
    active_ = false;
    hideAt = 0;
    SuspendWindow();
}

void SpeechBubble::SuspendWindow() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    dirty_ = active_;  // 다시 보일 때 그린다
}

void SpeechBubble::ReleaseGpu() {
    target_.reset();
    dirty_ = active_;
}

void SpeechBubble::Destroy() {
    target_.reset();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    shown_ = active_ = false;
}

HRESULT SpeechBubble::Update(HINSTANCE instance, GpuContext& gpu, BubbleRenderer& renderer, const RECT& petRect,
                             const RECT& work, float s, HDWP& batch, double now) {
    if (!active_ || !renderer.Ready()) return S_OK;
    // 갱신 묶기: 이미 보이는 말풍선은 직전 그리기 후 0.1초가 지나야 다시 그린다 (delta 마다 그리지 않음)
    redrawAt = -1;
    if (dirty_ && shown_ && lastDrawAt_ >= 0 && now - lastDrawAt_ < kMinRedrawSec) {
        redrawAt = lastDrawAt_ + kMinRedrawSec;
        dirty_ = false;   // 위치 갱신만 하고
        deferred_ = true; // 내용은 redrawAt 에 그린다
    } else if (deferred_ && now - lastDrawAt_ >= kMinRedrawSec) {
        dirty_ = true;
        deferred_ = false;
    }
    const float font = kFont * s, pad = kPadding * s, tail = kTail * s;
    if (dirty_ || s != measuredScale_) {  // 텍스트·배율이 바뀔 때만 다시 측정 (DirectWrite 레이아웃 비용)
        const D2D1_SIZE_F text = renderer.Measure(text_.empty() ? L"…" : text_, font, kMaxWidth * s);
        size_ = {std::max(static_cast<int>(std::ceil(text.width + pad * 2 + 4)), static_cast<int>(40 * s)),
                 static_cast<int>(std::ceil(text.height + pad * 2 + tail + 4))};
        measuredScale_ = s;
    }
    const int w = size_.cx, h = size_.cy;
    const int cx = (petRect.left + petRect.right) / 2;
    int left = std::clamp(cx - w / 2, static_cast<int>(work.left), static_cast<int>(std::max(work.left, work.right - w)));
    int top = std::max(static_cast<int>(work.top), static_cast<int>(petRect.top + 6 * s) - h);
    const RECT r{left, top, left + w, top + h};

    if (!hwnd_) {
        hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kBubbleClass, L"bubble", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, instance, nullptr);
        if (!hwnd_) return S_OK;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    }
    const bool resized = !target_ || target_->Width() != w || target_->Height() != h;
    if (!target_) {
        target_ = std::make_unique<CompositionTarget>();
        if (!target_->Create(gpu, hwnd_, ContentMode::SwapChain, w, h, false, true)) {
            target_.reset();
            return S_OK;
        }
    } else if (resized) {
        HRESULT hr = target_->Resize(w, h);
        if (FAILED(hr)) {
            static int logged = 0;
            if (logged++ < 3) log::Warn("[bubble] Resize 실패 {}", log::HrToString(hr));
            return hr;
        }
    }
    if (dirty_ || resized) {
        FrameTarget ft;
        HRESULT hr = target_->Begin(ft);
        if (SUCCEEDED(hr)) hr = renderer.Draw(ft, text_.empty() ? L"…" : text_, font, pad, tail);
        if (SUCCEEDED(hr)) hr = target_->End();
        if (FAILED(hr)) {
            static int logged = 0;
            if (logged++ < 3) log::Warn("[bubble] 그리기 실패 {}", log::HrToString(hr));
            return hr;
        }
        dirty_ = false;
        deferred_ = false;
        lastDrawAt_ = now;
        ++g_draws;
    }
    if (!shown_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        shown_ = true;
    } else if ((r.left != rect_.left || r.top != rect_.top || resized) && batch) {
        batch = DeferWindowPos(batch, hwnd_, nullptr, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    rect_ = r;
    return S_OK;
}

}  // namespace dp
