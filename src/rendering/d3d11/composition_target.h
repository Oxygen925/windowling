#pragma once
#include <windows.h>

#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "rendering/d3d11/content_mode.h"
#include "rendering/d3d11/gpu_context.h"

namespace dp {

// 한 프레임 동안 그릴 대상. surface 모드에서는 offset이 아틀라스 텍스처 안의 위치다.
struct FrameTarget {
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;  // swapchain + depth 요청 시에만
    ID2D1Bitmap1* d2dTarget = nullptr;      // d2d 요청 시에만
    POINT offset{0, 0};
    int width = 0, height = 0;
};

// 창 1개에 붙는 DComp target + visual + 콘텐츠(composition swapchain 또는 IDCompositionSurface)
class CompositionTarget {
public:
    ~CompositionTarget() { Release(); }
    bool Create(GpuContext& gpu, HWND hwnd, ContentMode mode, int width, int height, bool depth, bool d2d);
    HRESULT Resize(int width, int height);
    // 성공 시 out을 채우고 S_OK. (0,0,0,0)으로 클리어까지 수행
    HRESULT Begin(FrameTarget& out);
    HRESULT End();
    void Release();
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    HRESULT CreateContent();
    HRESULT CreateSwapChainViews();
    void ReleaseSwapChainViews();

    GpuContext* gpu_ = nullptr;
    HWND hwnd_ = nullptr;
    ContentMode mode_ = ContentMode::SwapChain;
    int width_ = 0, height_ = 0;
    bool depth_ = false, d2d_ = false;

    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    // swapchain 모드
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dBitmap_;
    // surface 모드
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> surfaceTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> surfaceRtv_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> surfaceD2d_;
    bool drawing_ = false;
};

}  // namespace dp
