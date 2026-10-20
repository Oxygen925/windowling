#pragma once
// P4 Privacy Guard 에 넘길 Windows 신호 (P4 계획 §2.2). 판정 자체는 ai::EvaluateGuard (Windows 비의존).
#include <windows.h>

#include <string>
#include <vector>

namespace dp {

std::string ExeOfWindow(HWND hwnd);  // 실행 파일 이름 (소문자), 실패하면 빈 값

// G6: 이 모니터에 보이는(최소화·cloak 아님) 최상위 창 중 exesLower 목록의 앱이 있나. 이 프로세스 창은 제외
bool ExcludedAppVisibleOn(HMONITOR monitor, const std::vector<std::string>& exesLower);

// 전경 창이 이 모니터에 있으면 그 실행 파일 이름, 아니면(또는 이 앱 자신이면) 빈 값
std::string ForegroundExeOn(HMONITOR monitor);

// G5: SHQueryUserNotificationState 가 프레젠테이션·전체화면 D3D·바쁨 상태 [검증]
bool NotificationsBusy();

// G7: 마지막 사용자 입력 이후 초
double IdleSeconds();

// 사용자 UI 언어 ("ko-KR")
std::string UserLocaleName();

}  // namespace dp
