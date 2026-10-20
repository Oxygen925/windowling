#include "rendering/d3d11/composition_target.h"

#include <algorithm>

#include "diagnostics/log.h"
#include "diagnostics/metrics.h"

namespace dp {

using Microsoft::WRL::ComPtr;

bool CompositionTarget::Create(GpuContext& gpu, HWND hwnd, ContentMode mode, int width, int height, bool depth,
                               bool d2d) {
    Release();
    gpu_ = &gpu;
    hwnd_ = hwnd;
    mode_ = mode;
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    depth_ = depth && mode == ContentMode::SwapChain;
    d2d_ = d2d;

    HRESULT hr = gpu.DComp()->CreateTargetForHwnd(hwnd, TRUE, &target_);
    if (SUCCEEDED(hr)) hr = gpu.DComp()->CreateVisual(&visual_);
    if (SUCCEEDED(hr)) hr = CreateContent();
    if (SUCCEEDED(hr)) hr = target_->SetRoot(visual_.Get());
    if (SUCCEEDED(hr)) hr = gpu.DComp()->Commit();
    if (FAILED(hr)) {
        log::Error("CompositionTarget 생성 실패 {} ({})", log::HrToString(hr), ToString(mode));
        return false;
    }
    return true;
}

HRESULT CompositionTarget::CreateContent() {
    HRESULT hr;
    if (mode_ == ContentMode::SwapChain) {
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = width_;
        d.Height = height_;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.Scaling = DXGI_SCALING_STRETCH;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = gpu_->Factory()->CreateSwapChainForComposition(gpu_->Device(), &d, nullptr, &swapChain_);
        if (FAILED(hr)) return hr;
        hr = CreateSwapChainViews();
        if (FAILED(hr)) return hr;
        return visual_->SetContent(swapChain_.Get());
    }
    hr = gpu_->DComp()->CreateSurface(width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                      &surface_);
    if (FAILED(hr)) return hr;
    return visual_->SetContent(surface_.Get());
}

HRESULT CompositionTarget::CreateSwapChainViews() {
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr)) return hr;
    hr = gpu_->Device()->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
    if (FAILED(hr)) return hr;
    if (depth_) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width_;
        td.Height = height_;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = gpu_->Device()->CreateTexture2D(&td, nullptr, &depthTex_);
        if (SUCCEEDED(hr)) hr = gpu_->Device()->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_);
        if (FAILED(hr)) return hr;
    }
    if (d2d_) {
        ComPtr<IDXGISurface> surf;
        back.As(&surf);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        hr = gpu_->D2D()->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &d2dBitmap_);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

void CompositionTarget::ReleaseSwapChainViews() {
    // ResizeBuffers 전에 back buffer 참조(RTV, D2D 비트맵)를 모두 해제해야 한다.
    if (gpu_ && gpu_->D2D()) gpu_->D2D()->SetTarget(nullptr);
    if (gpu_ && gpu_->Context()) gpu_->Context()->OMSetRenderTargets(0, nullptr, nullptr);
    d2dBitmap_.Reset();
    dsv_.Reset();
    depthTex_.Reset();
    rtv_.Reset();
}

HRESULT CompositionTarget::Resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_) return S_OK;
    width_ = width;
    height_ = height;
    if (mode_ == ContentMode::SwapChain) {
        ReleaseSwapChainViews();
        HRESULT hr = swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return hr;
        return CreateSwapChainViews();
    }
    surfaceRtv_.Reset();
    surfaceD2d_.Reset();
    surfaceTex_.Reset();
    surface_.Reset();
    HRESULT hr = gpu_->DComp()->CreateSurface(width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM,
                                              DXGI_ALPHA_MODE_PREMULTIPLIED, &surface_);
    if (SUCCEEDED(hr)) hr = visual_->SetContent(surface_.Get());
    if (SUCCEEDED(hr)) hr = gpu_->DComp()->Commit();
    return hr;
}

HRESULT CompositionTarget::Begin(FrameTarget& out) {
    out = {};
    const float clear[4] = {0, 0, 0, 0};
    if (mode_ == ContentMode::SwapChain) {
        out.rtv = rtv_.Get();
        out.dsv = dsv_.Get();
        out.d2dTarget = d2dBitmap_.Get();
        out.width = width_;
        out.height = height_;
        gpu_->Context()->ClearRenderTargetView(rtv_.Get(), clear);
        if (dsv_) gpu_->Context()->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        return S_OK;
    }
    ComPtr<ID3D11Texture2D> tex;
    POINT offset{};
    HRESULT hr = surface_->BeginDraw(nullptr, IID_PPV_ARGS(&tex), &offset);
    if (FAILED(hr)) return hr;
    drawing_ = true;
    if (tex != surfaceTex_) {
        // 아틀라스 텍스처가 바뀐 경우에만 뷰 재생성
        surfaceTex_ = tex;
        surfaceRtv_.Reset();
        surfaceD2d_.Reset();
        hr = gpu_->Device()->CreateRenderTargetView(tex.Get(), nullptr, &surfaceRtv_);
        if (FAILED(hr)) return hr;
        if (d2d_) {
            ComPtr<IDXGISurface> surf;
            tex.As(&surf);
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
            hr = gpu_->D2D()->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &surfaceD2d_);
            if (FAILED(hr)) return hr;
        }
    }
    out.rtv = surfaceRtv_.Get();
    out.d2dTarget = surfaceD2d_.Get();
    out.offset = offset;
    out.width = width_;
    out.height = height_;
    // 아틀라스 전체가 아니라 이 surface 영역만 클리어
    D3D11_RECT r{offset.x, offset.y, offset.x + width_, offset.y + height_};
    gpu_->Context()->ClearView(surfaceRtv_.Get(), clear, &r, 1);
    return S_OK;
}

HRESULT CompositionTarget::End() {
    HRESULT hr;
    if (mode_ == ContentMode::SwapChain) {
        hr = swapChain_->Present(1, 0);
    } else {
        if (!drawing_) return E_UNEXPECTED;
        drawing_ = false;
        hr = surface_->EndDraw();
        if (SUCCEEDED(hr)) hr = gpu_->DComp()->Commit();
    }
    if (SUCCEEDED(hr)) GlobalCounters().presents++;
    return hr;
}

void CompositionTarget::Release() {
    if (drawing_ && surface_) surface_->EndDraw();
    drawing_ = false;
    ReleaseSwapChainViews();
    swapChain_.Reset();
    surfaceRtv_.Reset();
    surfaceD2d_.Reset();
    surfaceTex_.Reset();
    surface_.Reset();
    if (target_) target_->SetRoot(nullptr);
    visual_.Reset();
    target_.Reset();
    gpu_ = nullptr;
}

}  // namespace dp
