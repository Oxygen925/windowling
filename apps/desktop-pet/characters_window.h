#pragma once
// 캐릭터 관리 창 (UI-08): 목록(썸네일·이름·종류·마릿수, 거부된 패키지 포함) / 미리보기·정보 / 시작할 때 띄울 수 /
// 설치(폴더·.zip 끌어다 놓기, zip 고르기) / 삭제(사용자 패키지만). 판단·파일 작업은 콜백(PetApp)이 한다.
// 컨트롤 ID 는 시험 스크립트가 쓰므로 바꾸지 않는다.
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/config.h"
#include "ui/app_window.h"

namespace dp {

enum CharactersControlId {
    IDC_CHR_LIST = 1001,
    IDC_CHR_PREVIEW,
    IDC_CHR_NAME,
    IDC_CHR_INFO,
    IDC_CHR_COUNT_L,
    IDC_CHR_COUNT,
    IDC_CHR_TOTAL,
    IDC_CHR_APPLY,
    IDC_CHR_DELETE,
    IDC_CHR_DROP,
    IDC_CHR_FOLDER,
    IDC_CHR_RELOAD,
    IDC_CHR_STATUS,
    IDC_CHR_INSTALL,
    // 닫기 = IDCANCEL (2)
};

struct CharacterEntry {
    std::string id;
    std::wstring name, version, author, license, source;
    bool model = false;      // 3D
    bool user = false;       // 사용자 캐릭터 폴더 (삭제 가능)
    bool rejected = false;   // 검사를 통과하지 못한 패키지
    std::vector<std::string> errors;
    std::filesystem::path root;
    int count = 0;           // 시작할 때 띄울 수 ([[pets]])
    std::vector<uint8_t> thumb;  // premultiplied BGRA, 위에서 아래, pitch = thumbW * 4. 비면 자리표시
    int thumbW = 0, thumbH = 0;
};

struct CharactersCallbacks {
    std::function<std::vector<CharacterEntry>()> list;
    std::function<std::wstring(const std::vector<core::PetSpawn>&)> applyCounts;
    std::function<std::wstring(const std::filesystem::path& source, std::string& installedId)> install;
    std::function<std::wstring(const CharacterEntry&)> remove;
    std::function<void()> openFolder;
    std::function<void()> reload;
};

class CharactersWindow : public ui::AppWindow {
public:
    ~CharactersWindow() override;
    void Show(CharactersCallbacks callbacks);
    void Refresh();  // 목록 다시 받기 (선택 유지)

protected:
    void OnCreate() override;
    void OnLayout(int width, int height) override;
    void OnCommand(int id, int code, HWND control) override;
    void OnClosed() override;
    void OnReloadTexts() override;
    bool IsBoldControl(int id) const override { return id == IDC_CHR_NAME; }
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
    void Select(int index);
    void UpdateDetails();
    void UpdateTotal();
    void Install(const std::filesystem::path& source);
    void ChooseZip();
    void ApplyCounts();
    void SetStatus(const std::wstring& text);
    void FreeBitmaps();
    void DrawThumb(HDC dc, int index, const RECT& box) const;
    int Selected() const;

    CharactersCallbacks cb_;
    std::vector<CharacterEntry> entries_;
    std::vector<HBITMAP> bitmaps_;      // entries_ 와 같은 순서 (썸네일 없으면 nullptr)
    std::map<std::string, int> counts_;  // 편집 중인 띄울 수
    bool filling_ = false;               // 코드가 칸을 채우는 중 (EN_CHANGE 무시)
};

}  // namespace dp
