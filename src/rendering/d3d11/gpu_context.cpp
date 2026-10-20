#include "rendering/d3d11/gpu_context.h"

#include "platform/windows/strings.h"
#include "diagnostics/log.h"

namespace dp {

using Microsoft::WRL::ComPtr;

bool GpuContext::IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET;
}

HRESULT GpuContext::RemovedReason() const {
    return device_ ? device_->GetDeviceRemovedReason() : E_POINTER;
}

bool GpuContext::Create(bool tryDebugLayer) {
    Release();
    // D7: BGRA_SUPPORT 초기부터 활성 (D2D interop)
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = E_FAIL;
    if (tryDebugLayer) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags | D3D11_CREATE_DEVICE_DEBUG, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, &got, &context);
        if (FAILED(hr))
            log::Warn("D3D11 debug layer 사용 불가 ({}) — Graphics Tools 미설치 가능성. debug 없이 재시도",
                      log::HrToString(hr));
        else
            debugLayer_ = true;
    }
    if (!device) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &device, &got, &context);
    }
    if (FAILED(hr)) {
        log::Error("D3D11CreateDevice 실패 {}", log::HrToString(hr));
        return false;
    }
    device.As(&device_);
    context.As(&context_);
    device.As(&dxgiDevice_);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice_->GetAdapter(&adapter);
    adapter.As(&adapter_);
    adapter->GetParent(IID_PPV_ARGS(&factory_));
    DXGI_ADAPTER_DESC desc{};
    adapter->GetDesc(&desc);
    log::Info("D3D11 device 생성: adapter={} feature_level=0x{:X} debug_layer={}", ToUtf8(desc.Description),
              static_cast<int>(got), debugLayer_);

    hr = DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcomp_));
    if (FAILED(hr)) {
        log::Error("DCompositionCreateDevice 실패 {}", log::HrToString(hr));
        return false;
    }

    D2D1_FACTORY_OPTIONS fo{};
    fo.debugLevel = debugLayer_ ? D2D1_DEBUG_LEVEL_INFORMATION : D2D1_DEBUG_LEVEL_NONE;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                           reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = d2dFactory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_);
    if (SUCCEEDED(hr)) hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_);
    if (FAILED(hr)) {
        log::Error("D2D 초기화 실패 {}", log::HrToString(hr));
        return false;
    }
    d2dContext_->SetDpi(96.0f, 96.0f);  // D2D 단위 = 물리 픽셀
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(hr)) {
        log::Error("DWrite 초기화 실패 {}", log::HrToString(hr));
        return false;
    }
    return true;
}

void GpuContext::Release() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    dwrite_.Reset();
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    dcomp_.Reset();
    factory_.Reset();
    adapter_.Reset();
    dxgiDevice_.Reset();
    context_.Reset();
    device_.Reset();
    debugLayer_ = false;
}

}  // namespace dp
