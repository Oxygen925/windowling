// PetApp 의 3D 캐릭터 부분 (P5): 모델 준비, 그리기, hit-test Region.
// 물리·행동 코어는 그대로 쓴다 (ADR-03): 모델은 펫 사각형(body.width·height, 발 = 아래 가장자리 가운데) 안에 직교 투영으로 그린다.
#include "pet_app.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "diagnostics/log.h"
#include "platform/windows/strings.h"
#include "rendering/hit_mask.h"

#include <wincodec.h>
#include <wrl/client.h>

namespace dp {

namespace {
// 확인용: premultiplied BGRA → PNG (펫 창의 픽셀만, 화면 캡처 아님)
void SavePng(const std::wstring& path, const std::vector<uint8_t>& px, int pitch, int w, int h) {
    using Microsoft::WRL::ComPtr;
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
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(h, pitch, static_cast<UINT>(px.size()), const_cast<BYTE*>(px.data()));
    frame->Commit();
    enc->Commit();
}
}  // namespace

std::unique_ptr<ModelAsset> PetApp::BuildModelAsset(const core::CharacterPackage& pkg, std::vector<std::string>& errors) {
    auto m = std::make_unique<ModelAsset>();
    if (const std::string e = model::LoadGlb(pkg.model.path, {}, m->data); !e.empty()) {
        errors.push_back("모델: " + e);
        return nullptr;
    }
    for (const auto& w : m->data.warnings) log::Warn("[library] {} 모델 경고: {}", pkg.id, w);
    for (const auto& [action, name] : pkg.model.animations) {
        const int idx = m->data.FindAnimation(name);
        if (idx < 0) log::Warn("[library] {} 애니메이션 '{}' 없음 (행동 {}) — idle 로 대체", pkg.id, name, action);
        else m->actions[action] = idx;
    }
    // 카메라: 기본 자세의 바운딩 박스로 크기를 고정한다 (애니메이션마다 크기·발 위치가 흔들리지 않게)
    const float yaw = static_cast<float>(pkg.model.yawDeg * std::numbers::pi / 180.0);
    const model::Bounds r = model::RestBounds(m->data, yaw), l = model::RestBounds(m->data, -yaw);
    const float height = std::max(r.Height(), l.Height());
    if (!(height > 0)) {
        errors.push_back("모델: 크기가 0");
        return nullptr;
    }
    const float bottom = std::min(r.min[1], l.min[1]);
    const float worldHeight = height * 1.15f;  // 위쪽 여유 (애니메이션으로 머리·꼬리가 올라가는 만큼)
    auto halfWidth = [](const model::Bounds& b) { return (b.max[0] - b.min[0]) / 2; };
    const float half = std::max(halfWidth(r), halfWidth(l)) * 1.15f;  // 옆 여유 (다리·꼬리)
    m->aspect = 2 * half / worldHeight;
    const float zMin = std::min(r.min[2], l.min[2]), zMax = std::max(r.max[2], l.max[2]);
    const float zPad = std::max(zMax - zMin, height) * 0.5f;
    auto cam = [&](float y, const model::Bounds& b) {
        ModelCamera c;
        c.yaw = y;
        c.centerX = (b.min[0] + b.max[0]) / 2;
        c.bottomY = bottom;
        c.worldHeight = worldHeight;
        c.aspect = m->aspect;
        c.zMin = zMin - zPad;
        c.zMax = zMax + zPad;
        return c;
    };
    m->right = cam(yaw, r);
    m->left = cam(-yaw, l);
    log::Info("[library] {} 모델 triangles={} joints={} animations={} aspect={:.2f}", pkg.id, m->data.triangles,
              m->data.skinJoints.size(), m->data.animations.size(), m->aspect);
    return m;
}

// 캐릭터 관리 창 썸네일 (UI-08): 화면 밖 텍스처에 idle(없으면 기본 자세) 첫 프레임을 오른쪽 시점으로 한 번 그려 읽는다.
// 펫 그리기와 같은 렌더러·readback 경로 (FrameTarget 은 RTV 와 크기뿐이라 스왑체인이 필요 없다)
bool PetApp::RenderModelThumbnail(CharacterAsset& asset, int height, CharacterThumb& out) {
    using Microsoft::WRL::ComPtr;
    ModelAsset& m = *asset.model;
    ID3D11Device* device = gpu_.Device();
    if (!device || !m.renderer.Ready()) return false;
    const int h = height, w = std::max(8, static_cast<int>(std::lround(height * m.aspect)));
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &tex)) || FAILED(device->CreateRenderTargetView(tex.Get(), nullptr, &rtv)))
        return false;
    ModelView view;
    if (FAILED(view.Resize(device, w, h, m.renderer.Samples()))) return false;
    model::EvaluateWorld(m.data, m.AnimationFor("idle"), 0.0, poseWorld_);
    model::SkinPalette(m.data, poseWorld_, posePalette_);
    FrameTarget ft;
    ft.rtv = rtv.Get();
    ft.width = w;
    ft.height = h;
    if (FAILED(m.renderer.Draw(gpu_.Context(), ft, view, m.data, poseWorld_, posePalette_, m.right, true))) return false;
    std::vector<uint8_t> px;
    int pitch = 0;
    if (!m.renderer.ReadAlpha(gpu_.Context(), view, px, pitch, true)) return false;
    out.width = w;
    out.height = h;
    out.bgra.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) std::memcpy(&out.bgra[static_cast<size_t>(y) * w * 4], &px[static_cast<size_t>(y) * pitch], static_cast<size_t>(w) * 4);
    return true;
}

HRESULT PetApp::DrawModel(PetActor& p, const RECT& r, double now, bool sizeChanged) {
    ModelAsset& m = *p.asset->model;
    const int anim = m.AnimationFor(ActionFor(p));
    const bool animChanged = anim != p.modelAnim;
    if (animChanged) {
        p.modelAnim = anim;
        p.modelAnimStart = now;
        log::Info("[pet{}] model anim={} action={}", p.id, anim >= 0 ? m.data.animations[anim].name : "(rest)", ActionFor(p));
    }
    const bool assetChanged = p.shownAsset != p.asset.get();
    if (p.modelView) ApplyModelRegion(p, false);  // 직전 프레임의 readback (기다리지 않음)
    // 애니메이션 중에는 display.frame_rate 간격마다 (P5-R5 결정: 30fps), 정지 자세는 바뀔 때만
    const bool due = anim >= 0 && (p.lastPresentAt < 0 || now - p.lastPresentAt >= frameIntervalSec_ - 0.002);
    if (!(animChanged || assetChanged || sizeChanged || p.facingLeft != p.shownFlip || due)) return S_OK;

    const int w = r.right - r.left, h = r.bottom - r.top;
    if (!p.modelView) p.modelView = std::make_unique<ModelView>();
    HRESULT hr = p.modelView->Resize(gpu_.Device(), w, h, m.renderer.Samples());
    if (FAILED(hr)) return hr;
    model::EvaluateWorld(m.data, anim, now - p.modelAnimStart, poseWorld_);
    model::SkinPalette(m.data, poseWorld_, posePalette_);
    // Region 은 보이는 부분도 자른다. 자세가 크게 바뀌는 때(애니메이션 전환·낙하·드래그·착지 직후)에는 0.1초 늦은 Region 이
    // 새 자세의 다리 등을 잘라 바닥이 비쳐 보였다(착지 때 검게 깜빡임 보고) → 이때는 같은 프레임에서 바로 맞춘다
    const bool moving = p.body.mode == core::BodyMode::Falling || p.body.mode == core::BodyMode::Dragged;
    const bool syncRegion = animChanged || moving || p.regionMoving;
    p.regionMoving = moving;
    const bool readback = now >= p.regionDueAt || assetChanged || sizeChanged || syncRegion;
    if (readback) p.regionDueAt = now + 0.1;  // P5-R2: 평소 Region 최대 10Hz
    // 서브픽셀 보정: 창은 정수 픽셀로만 움직여 걷기 속도 2.33px/프레임이 2·2·3px 로 번갈아 보였다(끊김).
    // 창 위치의 반올림 나머지만큼 모델을 창 안에서 밀어 그려 화면상 이동을 고르게 한다
    // 3D 펫은 그린 반복에서 창을 r 로 옮긴다(RenderAndMove) → 보정은 반올림 나머지(1px 미만)만 남는다
    ModelCamera cam = p.facingLeft ? m.left : m.right;
    const RECT win = r;
    const double unitsPerPx = cam.worldHeight / h;
    cam.centerX -= static_cast<float>((p.body.foot.x - w / 2.0 - win.left) * unitsPerPx);  // 오른쪽으로 밀기
    cam.bottomY += static_cast<float>((p.body.foot.y - h - win.top) * unitsPerPx);         // 아래로 밀기
    FrameTarget ft;
    hr = p.target->Begin(ft);
    if (SUCCEEDED(hr)) {
        hr = m.renderer.Draw(gpu_.Context(), ft, *p.modelView, m.data, poseWorld_, posePalette_, cam, readback);
        const HRESULT end = p.target->End();
        if (SUCCEEDED(hr)) hr = end;
    }
    FrameLog("present", p, win, now);  // 진단: 이 프레임이 기준으로 삼은 창 위치
    p.lastPresentAt = now;
    p.presentPendingAt = -1;
    p.shownFlip = p.facingLeft;
    p.shownAsset = p.asset.get();
    p.shownSize = w;
    // 정지 자세는 다음 그리기가 없으므로 바로 읽는다 (애니메이션 중에는 다음 프레임에 기다리지 않고 읽음)
    if (SUCCEEDED(hr) && readback && (anim < 0 || syncRegion)) ApplyModelRegion(p, true);
    return hr;
}

// GPU 알파 → hit-test Region (ADR-09 방식 B, P5-R2). Region 은 창의 보이는 부분도 자르므로, 최대 0.1초 늦게
// 따라오는 동안 움직이는 다리·꼬리가 잘리지 않게 몇 픽셀 넓힌다. 넓힌 만큼은 투명해도 클릭을 받는다 [검증: 잘림이 안 보이는지]
void PetApp::ApplyModelRegion(PetActor& p, bool wait) {
    std::vector<uint8_t> px;
    int pitch = 0;
    if (!p.asset->model->renderer.ReadAlpha(gpu_.Context(), *p.modelView, px, pitch, wait)) return;
    CostMeter::Scope cs(cost_, CostPhase::Region);
    const int w = p.modelView->Width(), h = p.modelView->Height();
    if (!options_.renderDump.empty() && renderDumps_ < 40 && renderReadbacks_++ % options_.renderDumpStride == 0) {  // 테스트·확인용
        std::wstring path = options_.renderDump;
        const size_t dot = path.find_last_of(L'.');
        path.insert(dot == std::wstring::npos ? path.size() : dot, L"_" + std::to_wstring(renderDumps_++));
        SavePng(path, px, pitch, w, h);
    }
    const int radius = std::max(2, static_cast<int>(std::lround(4.0 * p.dpi / 96.0)));
    // 알파 → 마스크 → 가로·세로 팽창 (분리 가능한 최대값 필터)
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h), tmp(mask.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) mask[y * w + x] = px[static_cast<size_t>(y) * pitch + x * 4 + 3] >= config_.alphaThreshold;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = 0;
            for (int k = std::max(0, x - radius); k <= std::min(w - 1, x + radius) && !v; ++k) v = mask[y * w + k];
            tmp[y * w + x] = v;
        }
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = 0;
            for (int k = std::max(0, y - radius); k <= std::min(h - 1, y + radius) && !v; ++k) v = tmp[k * w + x];
            bgra[(static_cast<size_t>(y) * w + x) * 4 + 3] = v ? 255 : 0;
        }
    std::vector<RECT> rects;
    AppendOpaqueRectsFromBgra(bgra.data(), w * 4, w, h, 128, rects);
    size_t hash = 1469598103934665603ull;  // FNV-1a
    for (const RECT& rc : rects)
        for (LONG v : {rc.left, rc.top, rc.right, rc.bottom}) hash = (hash ^ static_cast<size_t>(v)) * 1099511628211ull;
    if (hash == p.regionHash) {
        regionCacheHits_++;
        return;
    }
    RECT wr{};
    GetWindowRect(p.hwnd, &wr);
    FrameLog("region", p, wr, Now());
    SetWindowRgn(p.hwnd, CreateRegionFromRects(rects), TRUE);
    p.regionHash = hash;
    regionBuilds_++;
}

}  // namespace dp
