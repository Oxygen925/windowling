#pragma once
// AI 대화창 (P3-06): 포커스를 받는 별도 창 (기준선 §5.2 — 펫 오버레이는 키보드 포커스를 취하지 않는다).
// 컨트롤 ID 는 시나리오 테스트가 WM_SETTEXT·BM_CLICK 으로 직접 조작하므로 고정한다.
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "ai/responses.h"

namespace dp {

enum ChatControlId : int {
    IDC_CHAT_ACCOUNT = 210,  // 계정 표시
    IDC_CHAT_SIGNIN = 211,   // Continue with ChatGPT / 로그아웃 / 로그인 취소
    IDC_CHAT_PLAN = 212,     // Using ChatGPT plan
    IDC_CHAT_USAGE = 213,    // Manage usage
    IDC_CHAT_MODEL = 214,    // 모델 선택
    IDC_CHAT_HISTORY = 215,  // 대화 기록 (읽기 전용)
    IDC_CHAT_INPUT = 216,    // 입력
    IDC_CHAT_SEND = 217,     // 보내기 / 중지 (기본 버튼: Enter)
    IDC_CHAT_STATUS = 218,   // 상태·오류 안내
    IDC_CHAT_WELCOME = 219,  // 첫 로그인 안내
    IDC_CHAT_GOTIT = 220,    // Got it
    IDC_CHAT_LOCALOUT = 221, // revoke 실패 시: 로컬에서만 로그아웃
    IDC_CHAT_MODEL_LABEL = 222,   // "모델"
    IDC_CHAT_WELCOME_TITLE = 223, // 첫 로그인 안내 제목 (You're using your ChatGPT plan)
};

struct ChatCallbacks {
    std::function<void(const std::wstring&)> send;
    std::function<void()> stop, signIn, cancelSignIn, signOut, signOutLocal, gotIt, manageUsage, closed;
    std::function<void(int)> modelChanged;
};

class ChatWindow {
public:
    bool Create(HINSTANCE instance, ChatCallbacks callbacks);
    void Destroy();
    HWND Hwnd() const { return hwnd_; }
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Show(bool activate);
    void Hide();
    bool IsDialogMsg(MSG* msg);  // Tab·Enter 처리

    void SetTarget(const std::wstring& petName);
    void SetHistory(const std::wstring& text);
    void AppendHistory(const std::wstring& text);
    // signedIn·planUsage·signingIn·이메일 → 버튼·라벨
    void SetAccount(bool signedIn, bool planUsage, bool signingIn, const std::wstring& email);
    void SetModels(const std::vector<ai::ModelInfo>& models, int selected);
    void SetBusy(bool streaming);
    void SetStatus(const std::wstring& text);
    void ShowWelcome(bool show);
    void ShowLocalSignOut(bool show);
    void SetObserveNote(const std::wstring& note);  // "Using ChatGPT plan" 옆 표시 (P4: 화면 관찰 상태)
    void ReloadTexts();   // 언어를 바꾼 뒤 고정 문구를 다시 읽는다 (UI-01)
    void ApplyTheme();    // 테마(밝게/어둡게)를 바꾼 뒤 (UI-04)

private:
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Proc(HWND, UINT, WPARAM, LPARAM);
    void Layout();
    HWND Item(int id) const { return GetDlgItem(hwnd_, id); }

    HWND hwnd_ = nullptr;
    ChatCallbacks cb_;
    bool signedIn_ = false, signingIn_ = false, streaming_ = false, welcome_ = false, localOut_ = false, planUsage_ = false;
    std::wstring target_, observeNote_, email_;
};

}  // namespace dp
