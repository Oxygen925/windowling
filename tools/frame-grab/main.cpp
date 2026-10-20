// 진단 도구 (P5): 화면의 지정 사각형을 Desktop Duplication 으로 프레임마다 읽어 거의 검은 픽셀 수를 센다.
// 착지 때 잠깐 검게 보이는 현상처럼 짧은 합성 결함을 찾는 용도. 사각형만 읽고, 검은 픽셀이 많은 프레임만 PNG 로 저장한다.
// 사용자 화면이 찍히지 않도록 시나리오는 사각형을 단색 시험 창으로 덮어 둔다.
//   frame-grab --rect=L,T,R,B --seconds=N --out=dir [--dark=40] [--save-over=200] [--bg=R,G,B]
// --bg 를 주면 배경색과 다른 픽셀 수(= 펫이 보이는 픽셀)도 센다 → 펫 일부가 잠깐 사라지는 프레임을 찾는다
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

void SavePng(const std::wstring& path, const std::vector<uint8_t>& px, int w, int h) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(w, h)))
        return;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(h, w * 4, static_cast<UINT>(px.size()), const_cast<BYTE*>(px.data()));
    frame->Commit();
    enc->Commit();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    RECT rc{};
    double seconds = 5;
    int dark = 40, saveOver = 200, bgR = -1, bgG = 0, bgB = 0;
    std::wstring out = L".";
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a.rfind(L"--rect=", 0) == 0) swscanf_s(a.c_str() + 7, L"%ld,%ld,%ld,%ld", &rc.left, &rc.top, &rc.right, &rc.bottom);
        else if (a.rfind(L"--seconds=", 0) == 0) seconds = _wtof(a.c_str() + 10);
        else if (a.rfind(L"--out=", 0) == 0) out = a.substr(6);
        else if (a.rfind(L"--dark=", 0) == 0) dark = _wtoi(a.c_str() + 7);
        else if (a.rfind(L"--save-over=", 0) == 0) saveOver = _wtoi(a.c_str() + 12);
        else if (a.rfind(L"--bg=", 0) == 0) swscanf_s(a.c_str() + 5, L"%d,%d,%d", &bgR, &bgG, &bgB);
    }
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        std::printf("--rect=L,T,R,B 필요\n");
        return 2;
    }
    std::filesystem::create_directories(out);
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
        return 3;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    dev.As(&dxgi);
    dxgi->GetAdapter(&adapter);
    // 사각형이 있는 출력(모니터) 찾기
    ComPtr<IDXGIOutput1> output;
    DXGI_OUTPUT_DESC od{};
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> o;
        if (adapter->EnumOutputs(i, &o) == DXGI_ERROR_NOT_FOUND) break;
        o->GetDesc(&od);
        RECT x;
        if (IntersectRect(&x, &od.DesktopCoordinates, &rc)) {
            o.As(&output);
            break;
        }
    }
    if (!output) return 4;
    ComPtr<IDXGIOutputDuplication> dup;
    if (FAILED(output->DuplicateOutput(dev.Get(), &dup))) return 5;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = w;
    sd.Height = h;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) return 6;
    const int ox = rc.left - od.DesktopCoordinates.left, oy = rc.top - od.DesktopCoordinates.top;

    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    std::printf("t,dark,fg\n");
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    int frames = 0, saved = 0, maxDark = 0;
    for (;;) {
        QueryPerformanceCounter(&t);
        const double now = static_cast<double>(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (now > seconds) break;
        DXGI_OUTDUPL_FRAME_INFO fi{};
        ComPtr<IDXGIResource> res;
        const HRESULT hr = dup->AcquireNextFrame(50, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) break;
        if (fi.LastPresentTime.QuadPart) {  // 화면이 실제로 바뀐 프레임
            ComPtr<ID3D11Texture2D> tex;
            res.As(&tex);
            D3D11_BOX box{static_cast<UINT>(ox), static_cast<UINT>(oy), 0, static_cast<UINT>(ox + w), static_cast<UINT>(oy + h), 1};
            ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                int count = 0, fg = 0;
                for (int y = 0; y < h; ++y) {
                    const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
                    std::copy_n(row, w * 4, px.begin() + static_cast<size_t>(y) * w * 4);
                    for (int x = 0; x < w; ++x) {
                        if (row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2] < dark) ++count;
                        if (bgR >= 0 && std::abs(row[x * 4 + 2] - bgR) + std::abs(row[x * 4 + 1] - bgG) + std::abs(row[x * 4] - bgB) > 60) ++fg;
                    }
                }
                ctx->Unmap(staging.Get(), 0);
                std::printf("%.4f,%d,%d\n", now, count, fg);
                ++frames;
                maxDark = std::max(maxDark, count);
                if (count > saveOver && saved < 10)
                    SavePng(out + L"\\dark_" + std::to_wstring(saved++) + L".png", px, w, h);
            }
        }
        dup->ReleaseFrame();
    }
    std::fprintf(stderr, "frames=%d max_dark=%d saved=%d\n", frames, maxDark, saved);
    return 0;
}
