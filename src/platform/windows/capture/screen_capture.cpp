#include "platform/windows/capture/screen_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <algorithm>
#include <format>

namespace dp {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;

namespace {

double Ms(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
}

// BGRA 픽셀 → 축소 → JPEG (WIC, 메모리 스트림)
bool EncodeJpeg(const uint8_t* bgra, UINT w, UINT h, UINT pitch, const CaptureOptions& o, CaptureResult& out) {
    winrt::com_ptr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()))))
        return false;
    winrt::com_ptr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, pitch, pitch * h, const_cast<BYTE*>(bgra),
                                           bmp.put())))
        return false;
    const double scale = std::min(1.0, static_cast<double>(o.maxLongSide) / std::max(w, h));
    const UINT nw = std::max(1u, static_cast<UINT>(w * scale + 0.5)), nh = std::max(1u, static_cast<UINT>(h * scale + 0.5));
    winrt::com_ptr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(bmp.get(), nw, nh, WICBitmapInterpolationModeFant)))
        return false;
    winrt::com_ptr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.put())) ||
        FAILED(conv->Initialize(scaler.get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return false;

    winrt::com_ptr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) return false;
    winrt::com_ptr<IWICBitmapEncoder> enc;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, enc.put())) ||
        FAILED(enc->Initialize(stream.get(), WICBitmapEncoderNoCache)))
        return false;
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::com_ptr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(frame.put(), props.put()))) return false;
    PROPBAG2 opt{};
    opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R4;
    v.fltVal = std::clamp(o.jpegQuality, 1, 100) / 100.0f;
    props->Write(1, &opt, &v);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->Initialize(props.get())) || FAILED(frame->SetSize(nw, nh)) || FAILED(frame->SetPixelFormat(&fmt)) ||
        FAILED(frame->WriteSource(conv.get(), nullptr)) || FAILED(frame->Commit()) || FAILED(enc->Commit()))
        return false;

    HGLOBAL mem = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.get(), &mem))) return false;
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    const size_t size = static_cast<size_t>(st.cbSize.QuadPart);
    if (const void* p = GlobalLock(mem)) {
        out.jpeg.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + size);
        SecureZeroMemory(const_cast<void*>(p), size);  // 스트림 버퍼도 지운다 (해제는 stream 이)
        GlobalUnlock(mem);
    }
    out.width = static_cast<int>(nw);
    out.height = static_cast<int>(nh);
    return !out.jpeg.empty();
}

}  // namespace

bool CaptureSupported() {
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

CaptureResult CaptureMonitorJpeg(HMONITOR monitor, const CaptureOptions& o) {
    CaptureResult out;
    LARGE_INTEGER t0, t1, t2;
    QueryPerformanceCounter(&t0);
    try {
        if (!wgc::GraphicsCaptureSession::IsSupported()) {
            out.error = "unsupported";
            return out;
        }
        winrt::com_ptr<ID3D11Device> dev;
        winrt::com_ptr<ID3D11DeviceContext> ctx;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put()))) {
            out.error = "d3d_failed";
            return out;
        }
        winrt::com_ptr<::IInspectable> insp;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dev.as<IDXGIDevice>().get(), insp.put()))) {
            out.error = "d3d_failed";
            return out;
        }
        const auto device = insp.as<wgdx::Direct3D11::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{nullptr};
        if (FAILED(interop->CreateForMonitor(monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item))) ||
            !item) {
            out.error = "item_failed";
            return out;
        }
        const auto size = item.Size();
        auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        auto session = pool.CreateCaptureSession(item);
        try {
            session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }
        // 테두리(IsBorderRequired)는 기본값 유지: 캡처 순간이 사용자에게 보이도록 (P4-R3)
        session.StartCapture();
        wgc::Direct3D11CaptureFrame frame{nullptr};
        const ULONGLONG deadline = GetTickCount64() + o.frameTimeoutMs;
        while (!(frame = pool.TryGetNextFrame()) && GetTickCount64() < deadline) Sleep(5);
        if (!frame) {
            session.Close();
            pool.Close();
            out.error = "no_frame";
            return out;
        }

        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(tex.put())));
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        winrt::com_ptr<ID3D11Texture2D> staging;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, staging.put()))) {
            out.error = "d3d_failed";
            return out;
        }
        ctx->CopyResource(staging.get(), tex.get());
        frame.Close();
        session.Close();  // 세션은 즉시 닫는다 (테두리도 사라짐)
        pool.Close();
        QueryPerformanceCounter(&t1);
        out.captureMs = Ms(t0, t1);
        out.srcWidth = static_cast<int>(d.Width);
        out.srcHeight = static_cast<int>(d.Height);

        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m))) {
            out.error = "d3d_failed";
            return out;
        }
        const bool encoded = EncodeJpeg(static_cast<const uint8_t*>(m.pData), d.Width, d.Height, m.RowPitch, o, out);
        SecureZeroMemory(m.pData, static_cast<size_t>(m.RowPitch) * d.Height);
        ctx->Unmap(staging.get(), 0);
        QueryPerformanceCounter(&t2);
        out.encodeMs = Ms(t1, t2);
        if (!encoded) {
            out.error = "encode_failed";
            out.jpeg.clear();
            return out;
        }
        out.ok = true;
    } catch (const winrt::hresult_error& e) {
        out.error = std::format("winrt_0x{:08X}", static_cast<uint32_t>(e.code().value));
    } catch (...) {
        out.error = "exception";
    }
    return out;
}

}  // namespace dp
