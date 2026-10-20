#include "app.h"

#include <dwmapi.h>
#include <dxgi1_6.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "diagnostics/log.h"

namespace dp {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kAppClass[] = L"DesktopPet.P0.App";
constexpr wchar_t kPetClass[] = L"DesktopPet.P0.Pet";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT_PTR kSetTimerId = 1;
constexpr int kBaseSize = 128;      // 100% 배율에서 표시 크기 (64px 프레임 2배)
constexpr double kJumpSec = 0.4;
constexpr int kDragThresholdPx = 4; // 96 DPI 기준
constexpr double kNoDeadline = 1e300;

enum MenuId : UINT {
    ID_INFO = 100,
    ID_FOLLOW,
    ID_UNFOLLOW,
    ID_RECREATE,
    ID_HIDE,
    ID_EXIT,
};

double Distance(double ax, double ay, double bx, double by) {
    return std::hypot(ax - bx, ay - by);
}

bool SameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool SameRects(const std::vector<RECT>& a, const std::vector<RECT>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!SameRect(a[i], b[i])) return false;
    return true;
}

std::string RectStr(const RECT& r) {
    return std::format("({},{},{},{})", r.left, r.top, r.right, r.bottom);
}

bool SavePng(const uint32_t* bits, int w, int h, const std::wstring& path) {
    bool ok = false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&fmt);
        ok = SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, reinterpret_cast<BYTE*>(const_cast<uint32_t*>(bits)))) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    }
    return ok;
}

// DXGI Desktop Duplication: 스캔아웃 직전의 DWM 합성 결과 (DComp 콘텐츠 포함)
bool CaptureDuplication(const RECT& r, std::vector<uint32_t>& out) {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx)))
        return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    dev.As(&dxgi);
    dxgi->GetAdapter(&adapter);
    ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC od{};
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        output->GetDesc(&od);
        POINT p{r.left + 1, r.top + 1};
        if (PtInRect(&od.DesktopCoordinates, p)) break;
        output.Reset();
    }
    if (!output) return false;
    ComPtr<IDXGIOutputDuplication> dup;
    HRESULT hr = E_FAIL;
    ComPtr<IDXGIOutput5> out5;
    if (SUCCEEDED(output.As(&out5))) {
        const DXGI_FORMAT fmts[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
        hr = out5->DuplicateOutput1(dev.Get(), 0, 1, fmts, &dup);
    }
    if (FAILED(hr)) {
        ComPtr<IDXGIOutput1> out1;
        output.As(&out1);
        hr = out1 ? out1->DuplicateOutput(dev.Get(), &dup) : E_NOINTERFACE;
    }
    if (FAILED(hr)) {
        log::Warn("DuplicateOutput 실패 {}", log::HrToString(hr));
        return false;
    }
    ComPtr<IDXGIResource> res;
    DXGI_OUTDUPL_FRAME_INFO fi{};
    // 첫 프레임은 비어 있을 수 있어 몇 번 시도
    for (int attempt = 0; attempt < 10; ++attempt) {
        res.Reset();
        hr = dup->AcquireNextFrame(200, &fi, &res);
        if (SUCCEEDED(hr) && fi.LastPresentTime.QuadPart != 0) break;
        if (SUCCEEDED(hr)) dup->ReleaseFrame();
        res.Reset();
    }
    if (!res) return false;
    ComPtr<ID3D11Texture2D> tex;
    res.As(&tex);
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    dev->CreateTexture2D(&td, nullptr, &staging);
    ctx->CopyResource(staging.Get(), tex.Get());
    dup->ReleaseFrame();
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const int w = r.right - r.left, h = r.bottom - r.top;
    out.assign(static_cast<size_t>(w) * h, 0xFF000000u);
    for (int y = 0; y < h; ++y) {
        const int sy = r.top - od.DesktopCoordinates.top + y;
        if (sy < 0 || sy >= static_cast<int>(td.Height)) continue;
        const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(m.pData) + sy * m.RowPitch);
        for (int x = 0; x < w; ++x) {
            const int sx = r.left - od.DesktopCoordinates.left + x;
            if (sx >= 0 && sx < static_cast<int>(td.Width)) out[static_cast<size_t>(y) * w + x] = row[sx] | 0xFF000000u;
        }
    }
    ctx->Unmap(staging.Get(), 0);
    return true;
}

// 자체 검증용: 화면 영역 캡처 → PNG. Desktop Duplication 우선, 실패 시 GDI BitBlt
bool CaptureScreenRect(const RECT& r, const std::wstring& path, const char** method) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;
    std::vector<uint32_t> px;
    if (CaptureDuplication(r, px)) {
        *method = "desktop-duplication";
        return SavePng(px.data(), w, h, path);
    }
    *method = "gdi-bitblt";
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    auto* p = static_cast<uint32_t*>(bits);
    for (int i = 0; i < w * h; ++i) p[i] |= 0xFF000000u;
    const bool ok = SavePng(p, w, h, path);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

bool IsShellWindow(HWND hwnd) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
           !wcscmp(cls, L"Shell_SecondaryTrayWnd");
}

}  // namespace

// ============================================================================
// 실행
// ============================================================================

int App::Run(HINSTANCE instance, const Options& options) {
    opt_ = options;
    instance_ = instance;
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);

    // PMv2 매니페스트가 실제로 적용됐는지 확인
    DPI_AWARENESS_CONTEXT ctx = GetThreadDpiAwarenessContext();
    log::Info("DPI awareness: PMv2={} awareness={}",
              AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
              static_cast<int>(GetAwarenessFromDpiAwarenessContext(ctx)));

    if (opt_.ecoQos) {
        PROCESS_POWER_THROTTLING_STATE s{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                         PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
                                         PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
        BOOL ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
        log::Info("EcoQoS(process) 적용={}", ok != FALSE);
    }

    const std::wstring sheetPath = [] {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p = buf;
        return p.substr(0, p.find_last_of(L'\\')) + L"\\assets\\test\\pet_sheet.png";
    }();
    if (!sheet_.Load(sheetPath, 64)) return 3;

    if (!CreateAppWindow()) return 3;
    tray_.Add(appWnd_, WM_TRAY, L"Desktop Pet P0 spike");
    // 세션 잠금 시 애니메이션·관찰 작업 중지 (기준선 §8.1)
    if (!WTSRegisterSessionNotification(appWnd_, NOTIFY_FOR_THIS_SESSION))
        log::Warn("WTSRegisterSessionNotification 실패 err={}", GetLastError());

    // S0-A(--gpu=off): 트레이만, GPU 장치 미생성 / S0-B: 장치 생성·창 없음
    if (opt_.hitTest != HitTestMode::C && opt_.gpu && !InitGpu()) return 3;
    if (!opt_.gpu) log::Info("GPU 장치 생성 생략 (--gpu=off)");
    if (!CreatePets()) return 3;

    sampler_.Init();
    sampler_.SetAdapter(gpu_.Adapter());
    if (!opt_.logPath.empty()) {
        auto meta = CollectEnvironmentMeta();
        meta.push_back("options: " + DescribeOptions(opt_));
        meta.push_back(std::format("d3d_debug_layer={}", gpu_.DebugLayerActive()));
        if (!csv_.Open(opt_.logPath, meta)) log::Error("CSV 열기 실패: {}", ToUtf8(opt_.logPath));
    }

    if (opt_.timer == TimerMode::HiRes) {
        hiResTimer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!hiResTimer_) log::Warn("high-resolution waitable timer 생성 실패 err={}", GetLastError());
    }

    const double t0 = Now();
    warmupEnd_ = t0 + opt_.warmupSec;
    endTime_ = opt_.durationSec > 0 ? warmupEnd_ + opt_.durationSec : 0;
    nextSample_ = warmupEnd_ + 1.0;
    loggingActive_ = false;
    nextAnimTick_ = t0;
    nextPoll_ = t0;
    lastTick_ = t0;
    if (opt_.followDelaySec >= 0) BeginFollowPick(opt_.followDelaySec);
    if (!opt_.capturePath.empty()) captureAt_ = t0 + opt_.captureDelayMs / 1000.0;
    if (opt_.recreateAtSec >= 0) recreateAt_ = t0 + opt_.recreateAtSec;

    log::Info("시작: {}", DescribeOptions(opt_));
    Render();

    // ---- 메인 루프 (단일 UI 스레드, §3.6) ----
    while (!quit_) {
        double now = Now();
        // 애니메이션 프레임 마감과 그 외(커서 확인·측정 샘플 등) 마감을 분리한다.
        // 고해상도 타이머는 애니메이션 프레임에만 one-shot으로 쓰고, 나머지는 일반 timeout으로 깨운다.
        const double animDeadline = Animating() ? nextAnimTick_ : kNoDeadline;
        double otherDeadline = kNoDeadline;
        if (NeedsCursorPoll()) otherDeadline = std::min(otherDeadline, nextPoll_);
        if (csv_.IsOpen()) otherDeadline = std::min(otherDeadline, loggingActive_ ? nextSample_ : warmupEnd_);
        if (endTime_ > 0) otherDeadline = std::min(otherDeadline, endTime_);
        if (followPickAt_ >= 0) otherDeadline = std::min(otherDeadline, followPickAt_);
        if (captureAt_ >= 0) otherDeadline = std::min(otherDeadline, captureAt_);
        if (recreateAt_ >= 0) otherDeadline = std::min(otherDeadline, recreateAt_);

        HANDLE handles[1];
        DWORD handleCount = 0, timeoutMs = INFINITE;
        ArmWait(animDeadline, otherDeadline, now, timeoutMs, handles, handleCount);
        MsgWaitForMultipleObjectsEx(handleCount, handles, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        GlobalCounters().wakeups++;

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit_ = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit_) break;

        now = Now();
        if (Animating() && now >= nextAnimTick_) {
            sampler_.AddTickJitterMs((now - nextAnimTick_) * 1000.0);
            Tick(now, now - lastTick_);
            lastTick_ = now;
            const double fps = opt_.animFps > 0 ? opt_.animFps : 30;
            nextAnimTick_ += 1.0 / fps;
            if (nextAnimTick_ < now) nextAnimTick_ = now + 1.0 / fps;  // 밀리면 누적하지 않고 재정렬
        } else if (!Animating()) {
            nextAnimTick_ = now;
            lastTick_ = now;
        }
        if (NeedsCursorPoll() && now >= nextPoll_) PollCursor(now);
        if (followPickAt_ >= 0 && now >= followPickAt_) {
            followPickAt_ = -1;
            StartFollow();
        }
        if (recreateAt_ >= 0 && now >= recreateAt_) {
            recreateAt_ = -1;
            RecreateGpu("--recreate-at");
        }
        Render();
        CaptureIfRequested(now);

        if (csv_.IsOpen()) {
            if (!loggingActive_ && now >= warmupEnd_) {
                loggingActive_ = true;
                sampler_.Reset();
                nextSample_ = now + 1.0;
            } else if (loggingActive_ && now >= nextSample_) {
                SampleMetrics(now);
                nextSample_ += 1.0;
                if (nextSample_ < now) nextSample_ = now + 1.0;
            }
        }
        if (endTime_ > 0 && now >= endTime_) {
            log::Info("duration 종료");
            break;
        }
    }

    LogSummary();
    tracker_.Stop();
    if (hiResTimer_) CloseHandle(hiResTimer_);
    WTSUnRegisterSessionNotification(appWnd_);
    DestroyWindows();
    tray_.Remove();
    csv_.Close();
    sprite_.Release();
    cube_.Release();
    text_.Release();
    gpu_.Release();
    return 0;
}

double App::Now() const {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart - qpcStart_.QuadPart) / qpcFreq_.QuadPart;
}

void App::ArmWait(double animDeadline, double otherDeadline, double now, DWORD& timeoutMs, HANDLE* handles,
                  DWORD& handleCount) {
    handleCount = 0;
    timeoutMs = INFINITE;
    auto toMs = [now](double deadline) {
        return deadline >= kNoDeadline ? INFINITE
                                       : static_cast<DWORD>(std::ceil(std::max(0.0, deadline - now) * 1000.0));
    };
    const double first = std::min(animDeadline, otherDeadline);

    switch (opt_.timer) {
        case TimerMode::HiRes:
            if (hiResTimer_ && animDeadline < kNoDeadline) {
                // 애니메이션 중에만 고해상도 one-shot. 그 외 마감은 일반 timeout
                LARGE_INTEGER due;
                due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>((animDeadline - now) * 1e7));
                SetWaitableTimer(hiResTimer_, &due, 0, nullptr, nullptr, FALSE);
                handles[handleCount++] = hiResTimer_;
                timeoutMs = toMs(otherDeadline);
                return;
            }
            if (hiResTimer_) CancelWaitableTimer(hiResTimer_);  // 정지·숨김·잠금: 타이머 해제, 이벤트 대기
            timeoutMs = toMs(first);  // 타이머 생성 실패 시 msgwait 폴백도 이 경로
            return;
        case TimerMode::MsgWait:
            timeoutMs = toMs(first);
            return;
        case TimerMode::SetTimer:
            if (first >= kNoDeadline) {
                KillTimer(appWnd_, kSetTimerId);
                return;
            }
            // WM_TIMER가 메시지로 깨운다 (USER_TIMER_MINIMUM 이하로는 설정 불가)
            SetTimer(appWnd_, kSetTimerId, std::max<UINT>(USER_TIMER_MINIMUM, toMs(first)), nullptr);
            return;
    }
}

bool App::Animating() const {
    if (suspended()) return false;
    if (opt_.animFps > 0) return !pets_.empty();
    for (const auto& p : pets_)
        if (p->state == PetState::Jump) return true;
    return false;
}

bool App::NeedsCursorPoll() const {
    if (pets_.empty() || suspended()) return false;
    return opt_.hitTest == HitTestMode::A || opt_.proximity;
}

void App::SetSuspended(bool hidden, bool locked, const char* reason) {
    const bool was = suspended();
    hidden_ = hidden;
    locked_ = locked;
    if (was == suspended()) return;
    log::Info("{} ({}): 애니메이션·커서 확인·렌더 {}", suspended() ? "일시정지" : "재개", reason,
              suspended() ? "중지, 타이머 해제" : "재개");
    const double now = Now();
    nextAnimTick_ = now;  // 복귀 시 누적 시간 폐기
    lastTick_ = now;
    nextPoll_ = now;
    lastPresentPass_ = 0;
    for (auto& p : pets_) {
        if (p->hwnd) ShowWindow(p->hwnd, hidden_ ? SW_HIDE : SW_SHOWNOACTIVATE);
        if (!suspended()) {
            MarkContentDirty(*p);
            p->posDirty = true;
        }
    }
    if (overlayWnd_) ShowWindow(overlayWnd_, hidden_ ? SW_HIDE : SW_SHOWNOACTIVATE);
}

// ============================================================================
// 초기화
// ============================================================================

bool App::InitGpu() {
    if (!gpu_.Create(opt_.debugLayer)) return false;
    if (!sprite_.Init(gpu_.Device(), sheet_)) return false;
    if (opt_.cube && !cube_.Init(gpu_.Device())) return false;
    return true;
}

bool App::CreateAppWindow() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &App::StaticAppWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kAppClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW pc{sizeof(pc)};
    pc.lpfnWndProc = &App::StaticPetWndProc;
    pc.hInstance = instance_;
    pc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    pc.lpszClassName = kPetClass;
    RegisterClassExW(&pc);

    // TaskbarCreated 브로드캐스트를 받기 위해 HWND_MESSAGE가 아닌 숨김 최상위 창
    appWnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClass, L"DesktopPet P0", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              instance_, this);
    return appWnd_ != nullptr;
}

bool App::CreatePets() {
    POINT origin{0, 0};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const RECT work = mi.rcWork;

    for (int i = 0; i < opt_.pets; ++i) {
        auto pet = std::make_unique<Pet>();
        pet->id = i;
        pet->dpi = dpiX;
        pet->size = static_cast<int>(std::lround(kBaseSize * opt_.scale * dpiX / 96.0));
        pet->footX = work.left + (work.right - work.left) * (i + 1.0) / (opt_.pets + 1.0);
        pet->footY = work.bottom;
        pet->vx = (i % 2 ? -1 : 1) * (70.0 + 10.0 * i) * dpiX / 96.0;
        pet->facingLeft = pet->vx < 0;
        pets_.push_back(std::move(pet));
    }

    if (opt_.anchor == AnchorMode::TopLeft && !pets_.empty()) {
        // 측정용: 작업 영역 좌상단에 나란히 배치해 커서가 실수로 다가가지 않게 한다.
        // 이동(--move)도 이 구역 안에서만 왕복한다.
        const int gap = static_cast<int>(16 * dpiX / 96);
        const PetLayout L0 = ComputeLayout(*pets_[0]);
        const int winW = L0.window.right - L0.window.left, winH = L0.window.bottom - L0.window.top;
        const int spacing = winW + gap;
        const double rangeLeft = work.left + gap;
        const double rangeRight = rangeLeft + spacing * static_cast<double>(pets_.size()) + (opt_.move ? 2 * winW : 0);
        for (size_t i = 0; i < pets_.size(); ++i) {
            Pet& p = *pets_[i];
            p.footX = rangeLeft + spacing * static_cast<double>(i) + p.size / 2.0;
            p.footY = work.top + gap + winH;
            p.rangeLeft = rangeLeft;
            p.rangeRight = rangeRight;
        }
        log::Info("배치: 좌상단 구역 x=[{:.0f},{:.0f}] y={}~{}", rangeLeft, rangeRight, work.top + gap, work.top + gap + winH);
    }

    if (opt_.overlay == OverlayMode::Single) {
        if (!CreateOverlayWindow()) return false;
    } else {
        for (auto& p : pets_)
            if (!CreatePetWindow(*p)) return false;
    }
    log::Info("펫 {}마리 생성 (overlay={}, dpi={}, size={})", pets_.size(), ToString(opt_.overlay), dpiX,
              pets_.empty() ? 0 : pets_[0]->size);
    return true;
}

bool App::CreatePetWindow(Pet& pet) {
    const PetLayout L = ComputeLayout(pet);
    const int w = L.window.right - L.window.left, h = L.window.bottom - L.window.top;
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (opt_.hitTest == HitTestMode::C) ex |= WS_EX_LAYERED;
    else ex |= WS_EX_NOREDIRECTIONBITMAP;
    if (opt_.hitTest == HitTestMode::A) ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;

    pet.hwnd = CreateWindowExW(ex, kPetClass, L"pet", WS_POPUP, L.window.left, L.window.top, w, h, nullptr, nullptr,
                               instance_, this);
    if (!pet.hwnd) {
        log::Error("펫 창 생성 실패 err={} exStyle=0x{:X}", GetLastError(), ex);
        return false;
    }
    pet.lastWindow = L.window;
    pet.transparent = opt_.hitTest == HitTestMode::A;

    if (opt_.hitTest == HitTestMode::A) {
        // [검증] LAYERED + NOREDIRECTIONBITMAP 공존
        BOOL ok = SetLayeredWindowAttributes(pet.hwnd, 0, 255, LWA_ALPHA);
        log::Info("[검증] LAYERED+NOREDIRECTIONBITMAP: CreateWindowEx=성공 SetLayeredWindowAttributes={} err={}",
                  ok != FALSE, ok ? 0 : GetLastError());
    }
    if (opt_.hitTest != HitTestMode::C) {
        pet.target = std::make_unique<CompositionTarget>();
        if (!pet.target->Create(gpu_, pet.hwnd, opt_.content, w, h, opt_.cube, opt_.text)) return false;
        if (opt_.text && text_.FontPx() == 0) text_.Init(gpu_.D2D(), gpu_.DWrite(), FontPx(pet));
    }
    // 실제 창 DPI 확인 (모니터 DPI와 다르면 로그)
    UINT winDpi = GetDpiForWindow(pet.hwnd);
    if (winDpi != pet.dpi) UpdatePetDpi(pet, winDpi, "create");
    pet.contentDirty = true;
    return true;
}

bool App::CreateOverlayWindow() {
    overlayRect_ = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const int w = overlayRect_.right - overlayRect_.left, h = overlayRect_.bottom - overlayRect_.top;
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    if (opt_.hitTest == HitTestMode::A) ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    overlayWnd_ = CreateWindowExW(ex, kPetClass, L"overlay", WS_POPUP, overlayRect_.left, overlayRect_.top, w, h,
                                  nullptr, nullptr, instance_, this);
    if (!overlayWnd_) return false;
    if (opt_.hitTest == HitTestMode::A) SetLayeredWindowAttributes(overlayWnd_, 0, 255, LWA_ALPHA);
    overlayTransparent_ = opt_.hitTest == HitTestMode::A;
    overlayTarget_ = std::make_unique<CompositionTarget>();
    if (!overlayTarget_->Create(gpu_, overlayWnd_, opt_.content, w, h, opt_.cube, opt_.text)) return false;
    if (opt_.text && !pets_.empty()) text_.Init(gpu_.D2D(), gpu_.DWrite(), FontPx(*pets_[0]));
    log::Info("단일 오버레이 {} ({}x{})", RectStr(overlayRect_), w, h);
    overlayDirty_ = true;
    return true;
}

void App::DestroyWindows() {
    for (auto& p : pets_) {
        p->target.reset();
        p->blitter.Release();
        if (p->hwnd) DestroyWindow(p->hwnd);
        p->hwnd = nullptr;
    }
    overlayTarget_.reset();
    if (overlayWnd_) DestroyWindow(overlayWnd_);
    overlayWnd_ = nullptr;
    if (appWnd_) DestroyWindow(appWnd_);
    appWnd_ = nullptr;
}

// ============================================================================
// 레이아웃
// ============================================================================

float App::FontPx(const Pet& pet) const {
    return static_cast<float>(14.0 * opt_.scale * pet.dpi / 96.0);
}

PetLayout App::ComputeLayout(const Pet& p) const {
    PetLayout L;
    const int s = p.size;
    const int topMargin = s / 4;  // 점프·흔들림 여유
    const int labelH = opt_.text ? static_cast<int>(std::lround(FontPx(p) * 2.0f)) : 0;
    const int cubeW = opt_.cube ? s : 0;
    const int winW = std::max(s + cubeW, opt_.text ? static_cast<int>(FontPx(p) * 18) : 0);
    const int winH = labelH + topMargin + s;
    const int left = static_cast<int>(std::lround(p.footX - s / 2.0));
    const int top = static_cast<int>(std::lround(p.footY)) - winH;
    L.window = {left, top, left + winW, top + winH};

    int yOff = 0;
    int frame = 0;
    switch (p.state) {
        case PetState::Idle:
            frame = opt_.animFps > 0 ? (p.animTick / 4) % 4 : 0;
            yOff = (opt_.animFps > 0 && (p.animTick / 4) % 2) ? -std::max(1, s / 32) : 0;
            break;
        case PetState::Look: frame = 4; break;
        case PetState::Drag: frame = 5; break;
        case PetState::Jump: {
            const double t = std::clamp((Now() - p.jumpStart) / kJumpSec, 0.0, 1.0);
            frame = 1;
            yOff = -static_cast<int>(topMargin * std::sin(std::numbers::pi * t));
            break;
        }
    }
    L.sprite = {frame, 0, labelH + topMargin + yOff, s, s};
    L.cube = {s, labelH + topMargin, s + cubeW, labelH + topMargin + s};
    L.label = D2D1::RectF(2.0f, 1.0f, winW - 2.0f, labelH - 2.0f);
    return L;
}

RECT App::WorkAreaFor(const Pet& pet) const {
    POINT pt{static_cast<LONG>(pet.footX), static_cast<LONG>(pet.footY) - 1};
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcWork;
}

void App::UpdatePetDpi(Pet& pet, UINT dpi, const char* reason) {
    if (dpi == 0 || dpi == pet.dpi) return;
    const UINT old = pet.dpi;
    pet.dpi = dpi;
    pet.size = static_cast<int>(std::lround(kBaseSize * opt_.scale * dpi / 96.0));
    pet.vx = (pet.vx < 0 ? -1 : 1) * std::abs(pet.vx) * dpi / old;
    if (opt_.text && text_.FontPx() != FontPx(pet) && gpu_.Valid()) text_.Init(gpu_.D2D(), gpu_.DWrite(), FontPx(pet));
    MarkContentDirty(pet);
    pet.posDirty = true;
    log::Info("DPI 변경({}) pet={} {}→{} size={} foot=({:.0f},{:.0f})", reason, pet.id, old, dpi, pet.size, pet.footX,
              pet.footY);
}

void App::MarkContentDirty(Pet& pet) {
    pet.contentDirty = true;
    overlayDirty_ = true;
    GlobalCounters().stateChanges++;
}

// ============================================================================
// 시뮬레이션
// ============================================================================

void App::Tick(double now, double dt) {
    ticks_++;
    dt = std::min(dt, 0.1);  // 복귀 시 누적시간 폐기
    for (auto& up : pets_) {
        Pet& p = *up;
        if (p.state == PetState::Jump && now - p.jumpStart >= kJumpSec) p.state = PetState::Idle;
        if (opt_.animFps > 0 || p.state == PetState::Jump) {
            p.animTick++;
            if (opt_.move && p.state == PetState::Idle && !p.attached) {
                const RECT work = WorkAreaFor(p);
                p.footX += p.vx * dt;
                const double half = p.size / 2.0;
                const double lo = p.rangeRight > p.rangeLeft ? p.rangeLeft : work.left;
                const double hi = p.rangeRight > p.rangeLeft ? p.rangeRight : work.right;
                if (p.footX < lo + half) { p.footX = lo + half; p.vx = std::abs(p.vx); }
                if (p.footX > hi - half) { p.footX = hi - half; p.vx = -std::abs(p.vx); }
                p.facingLeft = p.vx < 0;
                p.posDirty = true;
            }
            MarkContentDirty(p);
        }
    }
}

void App::PollCursor(double now) {
    POINT pt;
    GetCursorPos(&pt);
    bool nearAny = false;
    bool overAny = false;
    for (auto& up : pets_) {
        Pet& p = *up;
        const PetLayout L = ComputeLayout(p);
        const double cx = L.window.left + L.sprite.x + L.sprite.w / 2.0;
        const double cy = L.window.top + L.sprite.y + L.sprite.h / 2.0;
        const bool isNear = Distance(pt.x, pt.y, cx, cy) < p.size * 1.2;
        nearAny |= isNear;

        if (opt_.proximity && !p.pressed) {
            if (isNear && p.state == PetState::Idle) {
                p.state = PetState::Look;
                MarkContentDirty(p);
            } else if (!isNear && p.state == PetState::Look) {
                p.state = PetState::Idle;
                MarkContentDirty(p);
            }
        }

        if (opt_.hitTest == HitTestMode::A) {
            const int lx = pt.x - L.window.left, ly = pt.y - L.window.top;
            bool over = HitOpaque(sheet_, L.sprite, lx, ly, opt_.alphaThreshold);
            if (opt_.cube) over |= PtInRect(&L.cube, POINT{lx, ly}) != FALSE;
            // 드래그/캡처 중에는 통과 상태로 되돌리지 않는다
            if (p.pressed) over = true;
            overAny |= over;
            if (opt_.overlay == OverlayMode::Small) SetTransparent(p.hwnd, p.transparent, !over);
        }
    }
    if (opt_.hitTest == HitTestMode::A && opt_.overlay == OverlayMode::Single)
        SetTransparent(overlayWnd_, overlayTransparent_, !overAny);
    nextPoll_ = now + (nearAny ? opt_.pollNearMs : opt_.pollFarMs) / 1000.0;
}

void App::SetTransparent(HWND hwnd, bool& state, bool transparent) {
    if (!hwnd || state == transparent) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex = transparent ? (ex | WS_EX_TRANSPARENT) : (ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    state = transparent;
    toggles_++;
}

// ============================================================================
// 렌더링
// ============================================================================

void App::Render() {
    if (suspended()) return;
    const uint64_t presentsBefore = GlobalCounters().presents;
    RenderPass();
    // 애니메이션 중 실제 Present 간격 (렌더 패스 기준, 펫 수와 무관)
    if (GlobalCounters().presents != presentsBefore && Animating()) {
        const double now = Now();
        if (lastPresentPass_ > 0) {
            const double ms = (now - lastPresentPass_) * 1000.0;
            const double target = 1000.0 / (opt_.animFps > 0 ? opt_.animFps : 30);
            sampler_.AddPresentIntervalMs(ms, std::abs(ms - target));
        }
        lastPresentPass_ = now;
    } else if (!Animating()) {
        lastPresentPass_ = 0;
    }
}

void App::RenderPass() {
    if (opt_.overlay == OverlayMode::Single) {
        if (!overlayDirty_ || !overlayTarget_) return;
        HRESULT hr = RenderOverlay();
        if (GpuContext::IsDeviceLost(hr)) {
            RecreateGpu("present");
            return;
        }
        overlayDirty_ = false;
        for (auto& p : pets_) p->contentDirty = p->posDirty = false;
        UpdateRegions();
        if (!IsWindowVisible(overlayWnd_)) ShowWindow(overlayWnd_, SW_SHOWNOACTIVATE);
        return;
    }

    for (auto& up : pets_) {
        Pet& p = *up;
        if (p.contentDirty) {
            HRESULT hr = RenderPetSmall(p);
            if (GpuContext::IsDeviceLost(hr)) {
                RecreateGpu("present");
                return;
            }
            if (FAILED(hr)) log::Warn("pet {} 렌더 실패 {}", p.id, log::HrToString(hr));
            p.contentDirty = false;
            p.regionDirty = true;
            if (!IsWindowVisible(p.hwnd)) ShowWindow(p.hwnd, SW_SHOWNOACTIVATE);
        }
    }
    ApplyWindowPositions();
    UpdateRegions();
}

HRESULT App::RenderPetSmall(Pet& p) {
    const PetLayout L = ComputeLayout(p);
    const int w = L.window.right - L.window.left, h = L.window.bottom - L.window.top;
    LARGE_INTEGER t;

    if (opt_.hitTest == HitTestMode::C) {
        if (!p.blitter.Resize(w, h)) return E_OUTOFMEMORY;
        p.blitter.Clear();
        p.blitter.Blit(sheet_, L.sprite);
        if (!p.blitter.Present(p.hwnd, POINT{L.window.left, L.window.top})) return HRESULT_FROM_WIN32(GetLastError());
        p.lastWindow = L.window;
        p.posDirty = false;
    } else {
        HRESULT hr = S_OK;
        if (w != p.target->Width() || h != p.target->Height()) {
            hr = p.target->Resize(w, h);
            if (FAILED(hr)) return hr;
            p.posDirty = true;
        }
        FrameTarget ft;
        hr = p.target->Begin(ft);
        if (FAILED(hr)) return hr;
        const float tint[4] = {1, 1, 1, 1};
        sprite_.Draw(gpu_.Context(), ft, L.sprite, p.facingLeft, tint);
        if (opt_.cube) cube_.Draw(gpu_.Context(), ft, L.cube, static_cast<float>(p.animTick) * 0.05f);
        if (opt_.text) {
            hr = text_.Draw(ft, L.label, std::format(L"P0 #{} 한글 텍스트 · DirectWrite", p.id));
            if (FAILED(hr)) {
                p.target->End();
                return hr;
            }
        }
        hr = p.target->End();
        if (FAILED(hr)) return hr;
    }
    QueryPerformanceCounter(&t);
    if (p.pendingInputQpc) {
        sampler_.AddInputLatencyMs((t.QuadPart - p.pendingInputQpc) * 1000.0 / qpcFreq_.QuadPart);
        p.pendingInputQpc = 0;
    }
    return S_OK;
}

HRESULT App::RenderOverlay() {
    FrameTarget ft;
    HRESULT hr = overlayTarget_->Begin(ft);
    if (FAILED(hr)) return hr;
    const float tint[4] = {1, 1, 1, 1};
    bool textOk = true;
    for (auto& up : pets_) {
        Pet& p = *up;
        PetLayout L = ComputeLayout(p);
        const int ox = L.window.left - overlayRect_.left, oy = L.window.top - overlayRect_.top;
        SpritePlacement sp = L.sprite;
        sp.x += ox;
        sp.y += oy;
        sprite_.Draw(gpu_.Context(), ft, sp, p.facingLeft, tint);
        if (opt_.cube) {
            RECT c = L.cube;
            OffsetRect(&c, ox, oy);
            cube_.Draw(gpu_.Context(), ft, c, static_cast<float>(p.animTick) * 0.05f);
        }
        if (opt_.text && textOk) {
            D2D1_RECT_F r = L.label;
            r.left += ox; r.right += ox; r.top += oy; r.bottom += oy;
            hr = text_.Draw(ft, r, std::format(L"P0 #{} 한글 텍스트 · DirectWrite", p.id));
            if (FAILED(hr)) textOk = false;
        }
    }
    HRESULT endHr = overlayTarget_->End();
    if (!textOk) return hr;
    if (FAILED(endHr)) return endHr;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    for (auto& p : pets_) {
        if (p->pendingInputQpc) {
            sampler_.AddInputLatencyMs((t.QuadPart - p->pendingInputQpc) * 1000.0 / qpcFreq_.QuadPart);
            p->pendingInputQpc = 0;
        }
        if (p->pendingFollowQpc) {
            followLatency_.push_back((t.QuadPart - p->pendingFollowQpc) * 1000.0 / qpcFreq_.QuadPart);
            p->pendingFollowQpc = 0;
        }
    }
    return S_OK;
}

void App::ApplyWindowPositions() {
    LARGE_INTEGER t;
    for (auto& up : pets_) {
        Pet& p = *up;
        if (!p.posDirty || !p.hwnd) continue;
        const PetLayout L = ComputeLayout(p);
        if (!SameRect(L.window, p.lastWindow)) {
            // 이동만이면 콘텐츠 재렌더 없이 창 위치만 바꾼다 (DComp 콘텐츠는 창을 따라간다)
            SetWindowPos(p.hwnd, HWND_TOPMOST, L.window.left, L.window.top, L.window.right - L.window.left,
                         L.window.bottom - L.window.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            p.lastWindow = L.window;
            windowMoves_++;
        }
        p.posDirty = false;
        QueryPerformanceCounter(&t);
        if (p.pendingFollowQpc) {
            followLatency_.push_back((t.QuadPart - p.pendingFollowQpc) * 1000.0 / qpcFreq_.QuadPart);
            p.pendingFollowQpc = 0;
        }
        if (p.pendingInputQpc && !p.contentDirty) {
            sampler_.AddInputLatencyMs((t.QuadPart - p.pendingInputQpc) * 1000.0 / qpcFreq_.QuadPart);
            p.pendingInputQpc = 0;
        }
    }
}

void App::UpdateRegions() {
    if (opt_.hitTest != HitTestMode::B) return;
    if (opt_.overlay == OverlayMode::Single) {
        std::vector<RECT> rects;
        for (auto& up : pets_) {
            PetLayout L = ComputeLayout(*up);
            SpritePlacement sp = L.sprite;
            sp.x += L.window.left - overlayRect_.left;
            sp.y += L.window.top - overlayRect_.top;
            AppendOpaqueRects(sheet_, sp, opt_.alphaThreshold, rects);
            if (opt_.cube) {
                RECT c = L.cube;
                OffsetRect(&c, L.window.left - overlayRect_.left, L.window.top - overlayRect_.top);
                rects.push_back(c);
            }
        }
        if (!SameRects(rects, overlayRegion_)) {
            SetWindowRgn(overlayWnd_, CreateRegionFromRects(rects), TRUE);
            overlayRegion_ = std::move(rects);
            regionUpdates_++;
        }
        return;
    }
    for (auto& up : pets_) {
        Pet& p = *up;
        // Region은 창 기준이라 이동만으로는 바뀌지 않는다. 콘텐츠가 바뀐 펫만 재계산 (A와 공정 비교)
        if (!p.regionDirty) continue;
        p.regionDirty = false;
        PetLayout L = ComputeLayout(p);
        std::vector<RECT> rects;
        AppendOpaqueRects(sheet_, L.sprite, opt_.alphaThreshold, rects);
        if (opt_.cube) rects.push_back(L.cube);
        if (!SameRects(rects, p.lastRegion)) {
            SetWindowRgn(p.hwnd, CreateRegionFromRects(rects), TRUE);  // 성공 시 Region 소유권은 OS
            p.lastRegion = std::move(rects);
            regionUpdates_++;
        }
    }
}

// ============================================================================
// 측정
// ============================================================================

void App::SampleMetrics(double /*now*/) {
    MetricsSample m = sampler_.Sample();
    std::string notes;
    if (tracker_.Active())
        notes = std::format("we_raw={} we_acc={}", tracker_.RawEvents(), tracker_.AcceptedEvents());
    csv_.WriteRow(opt_, static_cast<int>(pets_.size()), m, suspended(), notes);
}

void App::CaptureIfRequested(double now) {
    if (captureAt_ < 0 || now < captureAt_) return;
    captureAt_ = -1;
    RECT u{};
    if (overlayWnd_) {
        for (auto& p : pets_) {
            RECT r = ComputeLayout(*p).window;
            UnionRect(&u, &u, &r);
        }
    } else {
        for (auto& p : pets_) {
            RECT r;
            GetWindowRect(p->hwnd, &r);
            UnionRect(&u, &u, &r);
        }
    }
    InflateRect(&u, 8, 8);
    const char* method = "";
    const bool ok = CaptureScreenRect(u, opt_.capturePath, &method);
    log::Info("캡처 {} {} ({}) → {}", RectStr(u), ok ? "성공" : "실패", method, ToUtf8(opt_.capturePath));
}

void App::LogSummary() {
    log::Info("요약: presents={} state_changes={} ticks={} window_moves={} clicks_opaque={} clicks_transparent={} "
              "a_toggles={} b_region_updates={} gpu_recreates={} follow_lat_p50={:.3f} p95={:.3f} (n={})",
              GlobalCounters().presents, GlobalCounters().stateChanges, ticks_, windowMoves_, clicksOpaque_,
              clicksTransparent_, toggles_, regionUpdates_, gpuRecreates_, Percentile(followLatency_, 0.5),
              Percentile(followLatency_, 0.95), followLatency_.size());
}

// ============================================================================
// 입력
// ============================================================================

Pet* App::PetFromWindowPoint(HWND hwnd, POINT pt) {
    if (opt_.overlay == OverlayMode::Small) {
        for (auto& p : pets_)
            if (p->hwnd == hwnd) return p.get();
        return nullptr;
    }
    for (auto& p : pets_)
        if (p->pressed) return p.get();
    // 단일 오버레이: 커서 아래 펫 탐색 (hit-test none 이면 사각형 기준)
    for (auto it = pets_.rbegin(); it != pets_.rend(); ++it) {
        PetLayout L = ComputeLayout(**it);
        const int lx = pt.x - L.window.left, ly = pt.y - L.window.top;
        if (opt_.hitTest == HitTestMode::None ? (lx >= 0 && ly >= 0 && lx < L.window.right - L.window.left &&
                                                 ly < L.window.bottom - L.window.top)
                                              : HitOpaque(sheet_, L.sprite, lx, ly, opt_.alphaThreshold))
            return it->get();
    }
    return nullptr;
}

void App::OnMouseDown(Pet& p, POINT pt) {
    const PetLayout L = ComputeLayout(p);
    const bool opaque = HitOpaque(sheet_, L.sprite, pt.x - L.window.left, pt.y - L.window.top, opt_.alphaThreshold);
    (opaque ? clicksOpaque_ : clicksTransparent_)++;
    log::Info("E1 클릭 pet={} pt=({},{}) 불투명={} variant={}", p.id, pt.x, pt.y, opaque, ToUtf8(opt_.variant));
    p.pressed = true;
    p.dragging = false;
    p.pressPt = pt;
    p.grabDx = p.footX - pt.x;
    p.grabDy = p.footY - pt.y;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    p.pendingInputQpc = t.QuadPart;
}

void App::OnMouseMove(Pet& p, POINT pt) {
    if (!p.pressed) return;
    const int threshold = kDragThresholdPx * static_cast<int>(p.dpi) / 96;
    if (!p.dragging && (std::abs(pt.x - p.pressPt.x) > threshold || std::abs(pt.y - p.pressPt.y) > threshold)) {
        p.dragging = true;
        p.state = PetState::Drag;
        if (p.attached) {
            p.attached = false;
            log::Info("드래그로 창 부착 해제 pet={}", p.id);
        }
        MarkContentDirty(p);
    }
    if (p.dragging) {
        p.footX = pt.x + p.grabDx;
        p.footY = pt.y + p.grabDy;
        p.posDirty = true;
        overlayDirty_ = true;
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        p.pendingInputQpc = t.QuadPart;
    }
}

void App::OnMouseUp(Pet& p, POINT) {
    if (!p.pressed) return;
    const bool wasDrag = p.dragging;
    p.pressed = false;
    p.dragging = false;
    if (wasDrag) {
        p.state = PetState::Idle;  // 중력 복구는 P1. P0는 놓은 위치 유지
    } else {
        p.state = PetState::Jump;
        p.jumpStart = Now();
    }
    MarkContentDirty(p);
}

LRESULT CALLBACK App::StaticPetWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->PetWndProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::PetWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto screenPt = [&] {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &pt);
        return pt;
    };
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // 포커스 비탈취
        case WM_LBUTTONDOWN: {
            POINT pt = screenPt();
            if (Pet* p = PetFromWindowPoint(hwnd, pt)) {
                SetCapture(hwnd);
                OnMouseDown(*p, pt);
            } else {
                log::Info("E1 오버레이가 펫 밖 클릭을 받음 pt=({},{}) — 입력 가로챔", pt.x, pt.y);
                clicksTransparent_++;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt = screenPt();
            for (auto& p : pets_)
                if (p->pressed && (opt_.overlay == OverlayMode::Single || p->hwnd == hwnd)) OnMouseMove(*p, pt);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt = screenPt();
            for (auto& p : pets_)
                if (p->pressed && (opt_.overlay == OverlayMode::Single || p->hwnd == hwnd)) OnMouseUp(*p, pt);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED:
            for (auto& p : pets_)
                if (p->pressed && (opt_.overlay == OverlayMode::Single || p->hwnd == hwnd)) {
                    p->pressed = p->dragging = false;
                    p->state = PetState::Idle;
                    MarkContentDirty(*p);
                }
            return 0;
        case WM_RBUTTONUP:
            ShowMenu(appWnd_);
            return 0;
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            if (opt_.overlay == OverlayMode::Small) {
                for (auto& p : pets_)
                    if (p->hwnd == hwnd) {
                        // 권장 rect 대신 발 위치 기준으로 재배치 (계획서 §3.5)
                        log::Info("WM_DPICHANGED pet={} suggested={}", p->id, RectStr(*suggested));
                        UpdatePetDpi(*p, LOWORD(wp), "WM_DPICHANGED");
                        p->lastWindow = {};  // 크기 변경 강제 적용
                    }
            } else {
                for (auto& p : pets_) {
                    POINT f{static_cast<LONG>(p->footX), static_cast<LONG>(p->footY) - 1};
                    UINT dx = 96, dy = 96;
                    GetDpiForMonitor(MonitorFromPoint(f, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
                    UpdatePetDpi(*p, dx, "overlay WM_DPICHANGED");
                }
            }
            return 0;
        }
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

LRESULT CALLBACK App::StaticAppWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->AppWndProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::AppWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == TrayIcon::TaskbarCreatedMessage()) {
        log::Info("TaskbarCreated 수신 — 트레이 아이콘 재등록 {}", tray_.Readd() ? "성공" : "실패");
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: ShowMenu(hwnd); break;
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_FOLLOW: BeginFollowPick(3); break;
                case ID_UNFOLLOW:
                    tracker_.Stop();
                    for (auto& p : pets_) p->attached = false;
                    break;
                case ID_RECREATE: RecreateGpu("tray simulate"); break;
                case ID_HIDE: SetSuspended(!hidden_, locked_, "tray"); break;
                case ID_EXIT: PostQuitMessage(0); break;
            }
            return 0;
        case WM_TIMER: return 0;  // settimer 모드: 깨우기 용도
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) SetSuspended(hidden_, true, "session lock");
            else if (wp == WTS_SESSION_UNLOCK) SetSuspended(hidden_, false, "session unlock");
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE: {
            if (msg == WM_SETTINGCHANGE && wp != SPI_SETWORKAREA) break;
            log::Info("{} 수신 — 작업 영역/모니터 재계산", msg == WM_DISPLAYCHANGE ? "WM_DISPLAYCHANGE" : "SPI_SETWORKAREA");
            if (overlayWnd_ && msg == WM_DISPLAYCHANGE) {
                overlayRect_ = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
                SetWindowPos(overlayWnd_, HWND_TOPMOST, overlayRect_.left, overlayRect_.top,
                             overlayRect_.right - overlayRect_.left, overlayRect_.bottom - overlayRect_.top,
                             SWP_NOACTIVATE);
                overlayTarget_->Resize(overlayRect_.right - overlayRect_.left, overlayRect_.bottom - overlayRect_.top);
            }
            for (auto& p : pets_) {
                if (p->attached) continue;
                RECT work = WorkAreaFor(*p);
                p->footX = std::clamp(p->footX, work.left + p->size / 2.0, work.right - p->size / 2.0);
                if (!p->pressed) p->footY = std::min<double>(p->footY, work.bottom);
                if (overlayWnd_) {
                    POINT f{static_cast<LONG>(p->footX), static_cast<LONG>(p->footY) - 1};
                    UINT dx = 96, dy = 96;
                    GetDpiForMonitor(MonitorFromPoint(f, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
                    UpdatePetDpi(*p, dx, "display change");
                }
                p->posDirty = true;
                MarkContentDirty(*p);
                log::Info("pet={} work={} foot=({:.0f},{:.0f})", p->id, RectStr(work), p->footX, p->footY);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::ShowMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    const std::wstring info = ToWide(std::format("P0 {} / timer={} / pets={}", ToUtf8(opt_.variant),
                                                 ToString(opt_.timer), pets_.size()));
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_INFO, info.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (pets_.empty() ? MF_GRAYED : 0), ID_FOLLOW, L"창 추종 시작 (3초 후 전경 창)");
    AppendMenuW(menu, MF_STRING | (tracker_.Active() ? 0 : MF_GRAYED), ID_UNFOLLOW, L"창 추종 해제");
    AppendMenuW(menu, MF_STRING | (gpu_.Valid() ? 0 : MF_GRAYED), ID_RECREATE, L"GPU 재생성 시뮬레이션");
    AppendMenuW(menu, MF_STRING | (pets_.empty() ? MF_GRAYED : 0), ID_HIDE, hidden_ ? L"펫 보이기" : L"펫 숨기기 (타이머 해제)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"종료");
    POINT pt;
    GetCursorPos(&pt);
    // 메뉴가 바깥 클릭으로 닫히도록: SetForegroundWindow → TrackPopupMenu → WM_NULL
    SetForegroundWindow(owner);
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ============================================================================
// P0-07 단일 창 추종
// ============================================================================

void App::BeginFollowPick(int delaySec) {
    followPickAt_ = Now() + delaySec;
    log::Info("{}초 후 전경 창을 추종 대상으로 선택", delaySec);
}

void App::StartFollow() {
    if (pets_.empty()) return;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!fg || pid == GetCurrentProcessId() || IsShellWindow(fg)) {
        log::Warn("추종 대상 부적합 (hwnd=0x{:X}) — 일반 앱 창을 전경으로 두고 다시 시도",
                  reinterpret_cast<uintptr_t>(fg));
        return;
    }
    wchar_t title[128] = {};
    GetWindowTextW(fg, title, 128);
    Pet& p = *pets_[0];
    RECT frame{};
    DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    p.attached = true;
    p.attachLocalX = (frame.right - frame.left) / 2;
    tracker_.Start(fg, [this](DWORD ev, HWND h) { OnWinEvent(ev, h); });
    log::Info("추종 대상 '{}' pid={}", ToUtf8(title), pid);
    SnapToTarget(p);
}

void App::SnapToTarget(Pet& p) {
    HWND target = tracker_.Target();
    if (!target) return;
    RECT ext{}, win{};
    DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &ext, sizeof(ext));
    GetWindowRect(target, &win);
    static bool loggedDiff = false;
    if (!loggedDiff) {
        log::Info("창 경계 비교 EXTENDED_FRAME_BOUNDS={} GetWindowRect={}", RectStr(ext), RectStr(win));
        loggedDiff = true;
    }
    p.attachLocalX = std::clamp(p.attachLocalX, 0, std::max(0, static_cast<int>(ext.right - ext.left)));
    p.footX = ext.left + p.attachLocalX;
    p.footY = ext.top;
    p.posDirty = true;
    overlayDirty_ = true;
    if (opt_.overlay == OverlayMode::Single) GlobalCounters().stateChanges++;
}

void App::DropToWorkArea(Pet& p) {
    RECT work = WorkAreaFor(p);
    p.footY = work.bottom;
    p.posDirty = true;
    overlayDirty_ = true;
    GlobalCounters().stateChanges++;
}

void App::OnWinEvent(DWORD event, HWND) {
    if (pets_.empty()) return;
    Pet& p = *pets_[0];
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    switch (event) {
        case EVENT_OBJECT_LOCATIONCHANGE:
            if (!p.attached || p.pressed) return;
            if (IsIconic(tracker_.Target())) return;
            if (!p.pendingFollowQpc) p.pendingFollowQpc = t.QuadPart;  // 병합: 첫 이벤트 기준
            SnapToTarget(p);
            break;
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_OBJECT_HIDE:
        case EVENT_OBJECT_CLOAKED:
            log::Info("추종 대상 숨김/최소화 event=0x{:X} — 작업 영역 하단으로 이동", event);
            DropToWorkArea(p);
            break;
        case EVENT_SYSTEM_MINIMIZEEND:
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_UNCLOAKED:
            if (p.attached) {
                log::Info("추종 대상 복원 event=0x{:X} — 재부착", event);
                SnapToTarget(p);
            }
            break;
        case EVENT_OBJECT_DESTROY:
            log::Info("추종 대상 종료 — 부착 해제");
            p.attached = false;
            tracker_.Stop();
            DropToWorkArea(p);
            break;
    }
}

// ============================================================================
// P0-12 Device Lost 복구
// ============================================================================

void App::RecreateGpu(const char* reason) {
    if (opt_.hitTest == HitTestMode::C) return;
    gpuRecreates_++;
    const HRESULT removed = gpu_.RemovedReason();
    auto resources = [] {
        DWORD handles = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles);
        PROCESS_MEMORY_COUNTERS_EX pmc{sizeof(pmc)};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
        return std::format("handles={} gdi={} user={} private={:.1f}MB", handles,
                           GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                           GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), pmc.PrivateUsage / 1048576.0);
    };
    log::Warn("GPU 재생성 시작 reason={} removed_reason={} {}", reason, log::HrToString(removed), resources());

    // 부분 상태를 남기지 않고 전부 재생성
    for (auto& p : pets_) p->target.reset();
    overlayTarget_.reset();
    sprite_.Release();
    cube_.Release();
    text_.Release();
    gpu_.Release();

    if (!InitGpu()) {
        log::Error("GPU 재생성 실패 — 종료");
        PostQuitMessage(3);
        return;
    }
    sampler_.SetAdapter(gpu_.Adapter());
    if (overlayWnd_) {
        overlayTarget_ = std::make_unique<CompositionTarget>();
        overlayTarget_->Create(gpu_, overlayWnd_, opt_.content, overlayRect_.right - overlayRect_.left,
                               overlayRect_.bottom - overlayRect_.top, opt_.cube, opt_.text);
    }
    for (auto& p : pets_) {
        if (p->hwnd) {
            const PetLayout L = ComputeLayout(*p);
            p->target = std::make_unique<CompositionTarget>();
            p->target->Create(gpu_, p->hwnd, opt_.content, L.window.right - L.window.left,
                              L.window.bottom - L.window.top, opt_.cube, opt_.text);
        }
        MarkContentDirty(*p);
    }
    if (opt_.text && !pets_.empty()) text_.Init(gpu_.D2D(), gpu_.DWrite(), FontPx(*pets_[0]));
    log::Warn("GPU 재생성 완료 {}", resources());
}

}  // namespace dp
