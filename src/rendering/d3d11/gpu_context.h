#pragma once
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace dp {

// 프로세스당 1개: D3D11 Device/Context, DXGI Factory, DComp Device, D2D Device/Context, DWrite Factory.
// Device Lost 시 Release() → Create() 로 전체를 재생성한다 (부분 상태 유지 안 함).
class GpuContext {
public:
    ~GpuContext() { Release(); }
    bool Create(bool tryDebugLayer);
    void Release();
    bool Valid() const { return device_ != nullptr; }

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext1* Context() const { return context_.Get(); }
    IDXGIFactory2* Factory() const { return factory_.Get(); }
    IDXGIAdapter3* Adapter() const { return adapter_.Get(); }
    IDCompositionDevice* DComp() const { return dcomp_.Get(); }
    ID2D1DeviceContext* D2D() const { return d2dContext_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    bool DebugLayerActive() const { return debugLayer_; }

    // DXGI_ERROR_DEVICE_REMOVED/RESET 여부
    static bool IsDeviceLost(HRESULT hr);
    HRESULT RemovedReason() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    bool debugLayer_ = false;
};

}  // namespace dp
