#pragma once
// P4 ScreenObserver (P4 계획 §2.3): Windows.Graphics.Capture 로 모니터 1프레임만 잡고 세션을 바로 닫는다.
// 계속 열어 두는 캡처 세션·장치는 없다 (쉬는 동안 비용 0). 이미지는 메모리에서만 다루고 디스크에 쓰지 않는다.
// 호출 스레드는 COM 이 초기화돼 있어야 한다 (AI 작업 스레드). 렌더러와 Device 를 공유하지 않도록 캡처마다 작은
// D3D11 Device 를 따로 만든다 (immediate context 경합 방지, 15~30분에 1번이라 비용이 작다).
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dp {

struct CaptureOptions {
    int maxLongSide = 1024;  // 축소 후 긴 변
    int jpegQuality = 80;
    int frameTimeoutMs = 1000;
};

struct CaptureResult {
    bool ok = false;
    std::string error;          // unsupported, item_failed, no_frame, d3d_failed, encode_failed …
    int srcWidth = 0, srcHeight = 0;
    int width = 0, height = 0;  // 축소 후
    std::vector<uint8_t> jpeg;
    double captureMs = 0, encodeMs = 0;
};

bool CaptureSupported();
CaptureResult CaptureMonitorJpeg(HMONITOR monitor, const CaptureOptions& options);

}  // namespace dp
