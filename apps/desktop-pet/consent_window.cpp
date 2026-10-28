#include "consent_window.h"

#include <algorithm>

#include "ui/text.h"
#include "ui/ui_common.h"
#include "dp_version.h"

namespace dp {

namespace {
// EDIT 컨트롤은 줄바꿈에 \r\n 이 필요하다
std::wstring ToCrLf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 32);
    for (wchar_t c : s) {
        if (c == L'\n') out += L'\r';
        out += c;
    }
    return out;
}
}  // namespace

void ConsentWindow::Ask(const std::wstring& body, Answer answer) {
    if (IsOpen()) {
        BringToFront();
        return;
    }
    body_ = body;
    answer_ = std::move(answer);
    Open({DP_CLASS_PREFIX L"Consent", ui::T("observe.consent_title"), 540, 500, 0, 0, /*topmost*/ true});
    SetFocus(Item(IDC_CONSENT_CHECK));
}

void ConsentWindow::OnCreate() {
    Add(L"Static", ui::T("observe.consent_heading"), SS_LEFT, IDC_CONSENT_HEADING);
    Add(L"Edit", ToCrLf(body_), ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP, IDC_CONSENT_BODY, WS_EX_CLIENTEDGE);
    Add(L"Static", ui::T("observe.consent_warning"), SS_LEFT, IDC_CONSENT_WARNING);
    ui::MakeCheck(hwnd_, ui::T("observe.consent_check").c_str(), IDC_CONSENT_CHECK);
    HWND agree = Add(L"Button", ui::T("observe.consent_agree"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CONSENT_AGREE);
    EnableWindow(agree, FALSE);  // 체크해야 켜진다
    Add(L"Button", ui::T("observe.consent_cancel"), BS_DEFPUSHBUTTON | WS_TABSTOP, IDCANCEL);
}

void ConsentWindow::OnLayout(int width, int height) {
    const int m = S(16), gap = S(10), row = S(30), btnW = S(150), cancelW = S(96);
    const int W = width - 2 * m;
    int y = m;
    MoveWindow(Item(IDC_CONSENT_HEADING), m, y, W, S(24), TRUE);
    y += S(24) + gap;
    const int bottomBlock = S(24) + gap + row + gap + row;  // 경고 · 체크 · 버튼
    MoveWindow(Item(IDC_CONSENT_BODY), m, y, W, std::max(S(80), height - m - bottomBlock - gap - y), TRUE);
    int yy = height - m - bottomBlock;
    MoveWindow(Item(IDC_CONSENT_WARNING), m, yy, W, S(24), TRUE);
    yy += S(24) + gap;
    const int box = S(20);
    MoveWindow(Item(IDC_CONSENT_CHECK), m, yy + (row - box) / 2, box, box, TRUE);
    MoveWindow(Item(IDC_CONSENT_CHECK + ui::kLabelOffset), m + box + S(6), yy, W - box - S(6), row, TRUE);
    yy += row + gap;
    MoveWindow(Item(IDCANCEL), width - m - cancelW, yy, cancelW, row, TRUE);
    MoveWindow(Item(IDC_CONSENT_AGREE), width - m - cancelW - gap - btnW, yy, btnW, row, TRUE);
}

void ConsentWindow::OnCommand(int id, int code, HWND) {
    if (code != BN_CLICKED) return;
    const bool checked = SendMessageW(Item(IDC_CONSENT_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED;
    switch (id) {
        case IDC_CONSENT_CHECK: EnableWindow(Item(IDC_CONSENT_AGREE), checked); break;
        case IDC_CONSENT_AGREE:
            if (checked) Finish(true);  // 비활성 버튼에 명령을 직접 보내도 체크 없이는 동의하지 않는다
            break;
        case IDCANCEL: Finish(false); break;
    }
}

void ConsentWindow::OnReloadTexts() {
    SetWindowTextW(hwnd_, ui::T("observe.consent_title").c_str());
    SetWindowTextW(Item(IDC_CONSENT_HEADING), ui::T("observe.consent_heading").c_str());
    SetWindowTextW(Item(IDC_CONSENT_WARNING), ui::T("observe.consent_warning").c_str());
    SetWindowTextW(Item(IDC_CONSENT_CHECK + ui::kLabelOffset), ui::T("observe.consent_check").c_str());
    SetWindowTextW(Item(IDC_CONSENT_AGREE), ui::T("observe.consent_agree").c_str());
    SetWindowTextW(Item(IDCANCEL), ui::T("observe.consent_cancel").c_str());
}

void ConsentWindow::Finish(bool agreed) {
    Answer a = std::move(answer_);
    answer_ = nullptr;
    Close();
    if (a) a(agreed);
}

void ConsentWindow::OnClosed() {
    if (answer_) Finish(false);  // 답 없이 닫힘 = 동의 안 함
}

}  // namespace dp
