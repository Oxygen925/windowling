#include "characters_window.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "platform/windows/strings.h"
#include "ui/text.h"
#include "ui/ui_common.h"

namespace dp {

namespace {

std::wstring CrLf(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'\n') out += L'\r';
        out += c;
    }
    return out;
}

std::wstring OrDash(const std::wstring& s) { return s.empty() ? L"-" : s; }

HBITMAP MakeBitmap(const CharacterEntry& e) {
    if (e.thumb.empty() || e.thumbW <= 0 || e.thumbH <= 0) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = e.thumbW;
    bi.bmiHeader.biHeight = -e.thumbH;  // 위에서 아래
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) std::memcpy(bits, e.thumb.data(), std::min<size_t>(e.thumb.size(), static_cast<size_t>(e.thumbW) * e.thumbH * 4));
    return bmp;
}

}  // namespace

CharactersWindow::~CharactersWindow() { FreeBitmaps(); }

void CharactersWindow::Show(CharactersCallbacks callbacks) {
    cb_ = std::move(callbacks);
    if (IsOpen()) {
        Refresh();
        BringToFront();
        return;
    }
    Open({L"DesktopPet.Characters", ui::T("characters.title"), 780, 560, 660, 480});
}

void CharactersWindow::OnCreate() {
    auto note = [&](int id) { SetWindowLongPtrW(Add(L"Static", L"", SS_LEFT, id), GWLP_USERDATA, 1); };
    // 목록: 썸네일 + 두 줄을 직접 그린다
    Add(L"ListBox", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_TABSTOP, IDC_CHR_LIST,
        WS_EX_CLIENTEDGE);
    Add(L"Static", L"", SS_OWNERDRAW, IDC_CHR_PREVIEW);
    Add(L"Static", L"", SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE, IDC_CHR_NAME);
    Add(L"Edit", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_CHR_INFO, WS_EX_CLIENTEDGE);
    Add(L"Static", L"", SS_LEFT | SS_CENTERIMAGE, IDC_CHR_COUNT_L);
    Add(L"Edit", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, IDC_CHR_COUNT, WS_EX_CLIENTEDGE);
    SendMessageW(Item(IDC_CHR_COUNT), EM_SETLIMITTEXT, 1, 0);
    SetWindowLongPtrW(Add(L"Static", L"", SS_LEFT | SS_CENTERIMAGE, IDC_CHR_TOTAL), GWLP_USERDATA, 1);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_CHR_APPLY);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_CHR_DELETE);
    note(IDC_CHR_STATUS);
    note(IDC_CHR_DROP);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_CHR_INSTALL);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_CHR_FOLDER);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_CHR_RELOAD);
    Add(L"Button", L"", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
    DragAcceptFiles(hwnd_, TRUE);
    OnReloadTexts();
    Refresh();
}

void CharactersWindow::OnReloadTexts() {
    SetWindowTextW(hwnd_, ui::T("characters.title").c_str());
    const std::pair<int, const char*> texts[] = {
        {IDC_CHR_COUNT_L, "characters.count_label"}, {IDC_CHR_APPLY, "characters.apply"},
        {IDC_CHR_DELETE, "characters.delete"},       {IDC_CHR_DROP, "characters.drop_hint"},
        {IDC_CHR_INSTALL, "characters.install_zip"}, {IDC_CHR_FOLDER, "characters.open_folder"},
        {IDC_CHR_RELOAD, "characters.reload"},       {IDCANCEL, "characters.close"},
    };
    for (const auto& [id, key] : texts) SetWindowTextW(Item(id), ui::T(key).c_str());
    if (IsOpen() && !entries_.empty()) {
        UpdateDetails();
        UpdateTotal();
        InvalidateRect(Item(IDC_CHR_LIST), nullptr, TRUE);
    }
}

void CharactersWindow::OnClosed() {
    FreeBitmaps();
    entries_.clear();
}

void CharactersWindow::FreeBitmaps() {
    for (HBITMAP b : bitmaps_)
        if (b) DeleteObject(b);
    bitmaps_.clear();
}

int CharactersWindow::Selected() const {
    const LRESULT sel = SendMessageW(Item(IDC_CHR_LIST), LB_GETCURSEL, 0, 0);
    return sel >= 0 && sel < static_cast<LRESULT>(entries_.size()) ? static_cast<int>(sel) : -1;
}

void CharactersWindow::Refresh() {
    if (!IsOpen() || !cb_.list) return;
    const int old = Selected();
    const std::string keep = old >= 0 ? entries_[old].id : std::string();
    FreeBitmaps();
    entries_ = cb_.list();
    counts_.clear();
    for (const auto& e : entries_) {
        bitmaps_.push_back(MakeBitmap(e));
        if (!e.rejected) counts_[e.id] = e.count;
    }
    HWND list = Item(IDC_CHR_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int select = entries_.empty() ? -1 : 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        SendMessageW(list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
        if (!keep.empty() && entries_[i].id == keep) select = static_cast<int>(i);
    }
    Select(select);
    UpdateTotal();
}

void CharactersWindow::Select(int index) {
    SendMessageW(Item(IDC_CHR_LIST), LB_SETCURSEL, index, 0);
    UpdateDetails();
}

void CharactersWindow::UpdateDetails() {
    const int i = Selected();
    filling_ = true;
    if (i < 0) {
        SetWindowTextW(Item(IDC_CHR_NAME), L"");
        SetWindowTextW(Item(IDC_CHR_INFO), L"");
        SetWindowTextW(Item(IDC_CHR_COUNT), L"");
    } else {
        const CharacterEntry& e = entries_[i];
        SetWindowTextW(Item(IDC_CHR_NAME), e.name.c_str());
        const std::wstring where = e.user ? ui::T("characters.location_user") : ui::T("characters.location_builtin");
        std::wstring info;
        if (e.rejected) {
            std::wstring reasons;
            for (const auto& err : e.errors) reasons += L"- " + ToWide(err) + L"\n";
            info = ui::TF("characters.info_rejected", reasons, e.root.wstring());
        } else {
            info = ui::TF("characters.info", ToWide(e.id), OrDash(e.version), OrDash(e.author), OrDash(e.license),
                          OrDash(e.source), e.model ? ui::T("characters.renderer_model") : ui::T("characters.renderer_sprite"),
                          where, e.root.wstring());
        }
        SetWindowTextW(Item(IDC_CHR_INFO), CrLf(info).c_str());
        SetWindowTextW(Item(IDC_CHR_COUNT), e.rejected ? L"" : std::to_wstring(counts_[e.id]).c_str());
    }
    filling_ = false;
    const bool ok = i >= 0 && !entries_[i].rejected;
    EnableWindow(Item(IDC_CHR_COUNT), ok);
    EnableWindow(Item(IDC_CHR_DELETE), i >= 0 && entries_[i].user);
    InvalidateRect(Item(IDC_CHR_PREVIEW), nullptr, TRUE);
}

void CharactersWindow::UpdateTotal() {
    int total = 0;
    for (const auto& [_, n] : counts_) total += n;
    SetWindowTextW(Item(IDC_CHR_TOTAL), ui::TF("characters.total", total, core::Config::kMaxPets).c_str());
}

void CharactersWindow::SetStatus(const std::wstring& text) { SetWindowTextW(Item(IDC_CHR_STATUS), text.c_str()); }

void CharactersWindow::Install(const std::filesystem::path& source) {
    if (!cb_.install) return;
    std::string id;
    const std::wstring msg = cb_.install(source, id);
    if (!IsOpen()) return;
    Refresh();
    for (size_t i = 0; i < entries_.size() && !id.empty(); ++i)
        if (entries_[i].id == id && !entries_[i].rejected) Select(static_cast<int>(i));
    SetStatus(msg);
}

void CharactersWindow::ChooseZip() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    const std::wstring filterName = ui::T("characters.zip_filter");
    const COMDLG_FILTERSPEC spec[] = {{filterName.c_str(), L"*.zip"}};
    dlg->SetFileTypes(1, spec);
    if (FAILED(dlg->Show(hwnd_))) return;  // 취소
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        const std::filesystem::path p = path;
        CoTaskMemFree(path);
        Install(p);
    }
}

void CharactersWindow::ApplyCounts() {
    if (!cb_.applyCounts) return;
    std::vector<core::PetSpawn> list;
    for (const auto& e : entries_)  // 목록 순서대로
        if (!e.rejected && counts_[e.id] > 0) list.push_back({e.id, counts_[e.id]});
    const std::wstring msg = cb_.applyCounts(list);
    if (!IsOpen()) return;
    Refresh();
    SetStatus(msg);
}

void CharactersWindow::OnCommand(int id, int code, HWND) {
    switch (id) {
        case IDC_CHR_LIST:
            if (code == LBN_SELCHANGE) UpdateDetails();
            break;
        case IDC_CHR_COUNT:
            if (code == EN_CHANGE && !filling_) {
                const int i = Selected();
                if (i < 0 || entries_[i].rejected) break;
                wchar_t buf[4] = {};
                GetWindowTextW(Item(IDC_CHR_COUNT), buf, 4);
                counts_[entries_[i].id] = std::clamp(static_cast<int>(std::wcstol(buf, nullptr, 10)), 0, core::Config::kMaxPets);
                UpdateTotal();
                InvalidateRect(Item(IDC_CHR_LIST), nullptr, FALSE);
            }
            break;
        case IDOK:  // 띄울 수 칸에서 Enter
        case IDC_CHR_APPLY:
            if (code == BN_CLICKED) ApplyCounts();
            break;
        case IDC_CHR_DELETE:
            if (code == BN_CLICKED && cb_.remove) {
                const int i = Selected();
                if (i < 0) break;
                const CharacterEntry e = entries_[i];
                const std::wstring msg = cb_.remove(e);
                if (!IsOpen()) break;
                Refresh();
                SetStatus(msg);
            }
            break;
        case IDC_CHR_INSTALL:
            if (code == BN_CLICKED) ChooseZip();
            break;
        case IDC_CHR_FOLDER:
            if (code == BN_CLICKED && cb_.openFolder) cb_.openFolder();
            break;
        case IDC_CHR_RELOAD:
            if (code == BN_CLICKED && cb_.reload) {
                cb_.reload();
                Refresh();
            }
            break;
        case IDCANCEL:
            if (code == BN_CLICKED) Close();
            break;
    }
}

void CharactersWindow::OnLayout(int width, int height) {
    const int m = S(14), gap = S(8), row = S(28), btnH = S(30), listW = std::min(S(300), width * 40 / 100);
    auto mv = [&](int id, int x, int y, int w, int h) { MoveWindow(Item(id), x, y, std::max(0, w), std::max(0, h), TRUE); };
    SendMessageW(Item(IDC_CHR_LIST), LB_SETITEMHEIGHT, 0, S(58));

    // 아래: [zip 에서 설치…] [폴더 열기] [다시 읽기] ……… [닫기], 그 위 끌어다 놓기 안내, 상태
    const int bottomY = height - m - btnH;
    const int b1 = S(150), b2 = S(150), b3 = S(110), bc = S(100);
    mv(IDC_CHR_INSTALL, m, bottomY, b1, btnH);
    mv(IDC_CHR_FOLDER, m + b1 + gap, bottomY, b2, btnH);
    mv(IDC_CHR_RELOAD, m + b1 + b2 + 2 * gap, bottomY, b3, btnH);
    mv(IDCANCEL, width - m - bc, bottomY, bc, btnH);
    const int dropY = bottomY - gap - S(20);
    mv(IDC_CHR_DROP, m, dropY, width - 2 * m, S(20));
    const int statusY = dropY - S(4) - S(38);
    mv(IDC_CHR_STATUS, m, statusY, width - 2 * m, S(38));
    const int contentBottom = statusY - gap;

    mv(IDC_CHR_LIST, m, m, listW, contentBottom - m);
    const int x0 = m + listW + S(16), cw = width - x0 - m;
    const int preview = S(128);
    mv(IDC_CHR_PREVIEW, x0, m, preview, preview);
    mv(IDC_CHR_NAME, x0 + preview + S(14), m + S(8), cw - preview - S(14), S(30));
    // 띄울 수·버튼은 아래쪽, 정보는 그 사이를 채운다
    const int actionsY = contentBottom - btnH;
    const int countY = actionsY - gap - row;
    const int labelW = std::min(S(180), cw / 2);
    mv(IDC_CHR_COUNT_L, x0, countY, labelW, row);
    mv(IDC_CHR_COUNT, x0 + labelW + gap, countY + S(1), S(48), row - S(2));
    mv(IDC_CHR_TOTAL, x0 + labelW + gap + S(48) + S(12), countY, cw - labelW - S(68) - gap, row);
    mv(IDC_CHR_APPLY, x0, actionsY, S(190), btnH);
    mv(IDC_CHR_DELETE, x0 + S(190) + gap, actionsY, S(100), btnH);
    const int infoY = m + preview + gap;
    mv(IDC_CHR_INFO, x0, infoY, cw, std::max(S(60), countY - gap - infoY));
}

void CharactersWindow::DrawThumb(HDC dc, int index, const RECT& box) const {
    const int bw = box.right - box.left, bh = box.bottom - box.top;
    HBITMAP bmp = index >= 0 && index < static_cast<int>(bitmaps_.size()) ? bitmaps_[index] : nullptr;
    if (!bmp) {  // 자리표시: 테두리 + "?" (거부된 패키지·썸네일 없음)
        HBRUSH border = CreateSolidBrush(ui::Colors().border);
        FrameRect(dc, &box, border);
        DeleteObject(border);
        RECT r = box;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ui::Colors().subtle);
        DrawTextW(dc, L"?", 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        return;
    }
    const CharacterEntry& e = entries_[index];
    const double s = std::min(static_cast<double>(bw) / e.thumbW, static_cast<double>(bh) / e.thumbH);
    const int w = std::max(1, static_cast<int>(e.thumbW * s)), h = std::max(1, static_cast<int>(e.thumbH * s));
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, box.left + (bw - w) / 2, box.top + (bh - h) / 2, w, h, mem, 0, 0, e.thumbW, e.thumbH, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

LRESULT CharactersWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
    handled = true;
    switch (msg) {
        case WM_MEASUREITEM:
            if (wp == IDC_CHR_LIST) {
                reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight = S(58);
                return TRUE;
            }
            break;
        case WM_DRAWITEM: {
            const auto* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            const ui::Palette& p = ui::Colors();
            if (wp == IDC_CHR_PREVIEW) {
                FillRect(d->hDC, &d->rcItem, ui::ControlBrush());
                DrawThumb(d->hDC, Selected(), d->rcItem);
                return TRUE;
            }
            if (wp != IDC_CHR_LIST) break;
            if (d->itemID == static_cast<UINT>(-1) || d->itemID >= entries_.size()) return TRUE;
            const int i = static_cast<int>(d->itemID);
            const CharacterEntry& e = entries_[i];
            const bool selected = (d->itemState & ODS_SELECTED) != 0;
            HBRUSH bg = selected ? CreateSolidBrush(p.accent) : nullptr;
            FillRect(d->hDC, &d->rcItem, selected ? bg : ui::ControlBrush());
            if (bg) DeleteObject(bg);
            const int pad = S(6), thumb = (d->rcItem.bottom - d->rcItem.top) - 2 * pad;
            RECT tb{d->rcItem.left + pad, d->rcItem.top + pad, d->rcItem.left + pad + thumb, d->rcItem.top + pad + thumb};
            DrawThumb(d->hDC, i, tb);
            const UINT dpi = GetDpiForWindow(hwnd_);
            RECT t1{tb.right + S(10), d->rcItem.top + pad, d->rcItem.right - pad, d->rcItem.top + pad + thumb / 2};
            RECT t2{t1.left, t1.bottom, t1.right, tb.bottom};
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, selected ? p.window : p.controlText);
            HGDIOBJ old = SelectObject(d->hDC, ui::UiFont(dpi, 0, true));
            DrawTextW(d->hDC, e.name.c_str(), -1, &t1, DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX);
            std::wstring sub;
            if (e.rejected) {
                sub = ui::TF("characters.subtitle_rejected", e.errors.empty() ? std::wstring(L"-") : ToWide(e.errors.front()));
            } else {
                auto it = counts_.find(e.id);
                sub = ui::TF("characters.subtitle", ToWide(e.id),
                             e.model ? ui::T("characters.renderer_model") : ui::T("characters.renderer_sprite"),
                             it == counts_.end() ? 0 : it->second);
            }
            SelectObject(d->hDC, ui::UiFont(dpi, -1));
            SetTextColor(d->hDC, selected ? p.window : p.subtle);
            DrawTextW(d->hDC, sub.c_str(), -1, &t2, DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(d->hDC, old);
            if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(d->hDC, &d->rcItem);
            return TRUE;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            std::vector<std::filesystem::path> paths;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT k = 0; k < n; ++k) {
                std::wstring path(DragQueryFileW(drop, k, nullptr, 0), L'\0');
                DragQueryFileW(drop, k, path.data(), static_cast<UINT>(path.size()) + 1);
                paths.emplace_back(path);
            }
            DragFinish(drop);
            for (const auto& path : paths) Install(path);  // 끌어다 놓기는 사용자 행동 → 하나씩 확인·설치
            return 0;
        }
    }
    handled = false;
    return 0;
}

}  // namespace dp
