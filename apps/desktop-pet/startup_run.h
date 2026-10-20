#pragma once
// Windows 를 시작할 때 실행 (UI-07): HKCU\Software\Microsoft\Windows\CurrentVersion\Run 의 "DesktopPet" 값.
// 사용자가 설정 창에서 켤 때만 쓴다. 시험(--test-hooks)에서는 HKCU\Software\DesktopPet\TestRun 을 대신 쓴다.

namespace dp::startup {

void UseTestKey(bool on);
bool IsEnabled();
bool SetEnabled(bool on);  // 성공하면 true

}  // namespace dp::startup
