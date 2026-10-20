#pragma once
// dp_core 는 Windows API에 의존하지 않는다 (기준선 §3 의존성 규칙, P1 계획 §3.8).
// 이 헤더는 dp_core 소스에 /FI 로 강제 포함되어 <windows.h> 의 include guard 를 미리 정의한다.
// 그 결과 dp_core 안에서 <windows.h> 를 포함해도 내용이 비어, HWND 등 Win32 타입을 쓰면 컴파일 오류가 난다.
#define _WINDOWS_
#define DP_CORE_FORBID_WINDOWS 1
