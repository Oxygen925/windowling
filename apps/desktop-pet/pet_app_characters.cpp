// 캐릭터 관리 (UI-08): 목록·썸네일, 시작할 때 띄울 수, 설치(폴더·.zip), 삭제(사용자 패키지)
#include "pet_app.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <cstring>

#include "character_install.h"
#include "core/character/zip_index.h"
#include "diagnostics/log.h"
#include "platform/windows/strings.h"
#include "ui/text.h"

namespace dp {

namespace fs = std::filesystem;

void PetApp::OpenCharacters() {
    CharactersCallbacks cb;
    cb.list = [this] { return CharacterEntries(); };
    cb.applyCounts = [this](const std::vector<core::PetSpawn>& list) { return ApplyPetCounts(list); };
    cb.install = [this](const fs::path& source, std::string& id) { return InstallCharacter(source, id); };
    cb.remove = [this](const CharacterEntry& e) { return DeleteCharacter(e); };
    cb.openFolder = [this] { ShellExecuteW(nullptr, L"open", UserCharactersDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    cb.reload = [this] { ReloadCharacters(); };
    characters_.Show(std::move(cb));
}

// 썸네일: 2D = idle 첫 프레임(없으면 0번), 3D = 화면 밖 텍스처에 한 번 그린 기본(idle) 자세
const CharacterThumb& PetApp::ThumbnailFor(const CharacterAsset& asset) {
    auto [it, added] = thumbs_.try_emplace(asset.pkg.id);
    CharacterThumb& t = it->second;
    if (!added) return t;
    if (asset.model) {
        if (!RenderModelThumbnail(const_cast<CharacterAsset&>(asset), 128, t)) {
            t = {};
            log::Warn("[characters] {} 3D 썸네일 실패 — 자리표시", asset.pkg.id);
        }
        return t;
    }
    const SpriteSheet& s = asset.sheet;
    const int fw = s.FrameWidth(), fh = s.FrameHeight();
    if (fw <= 0 || fh <= 0 || !s.Pixels()) return t;
    int frame = 0;
    if (auto c = asset.pkg.sprite.clips.find("idle"); c != asset.pkg.sprite.clips.end() && !c->second.frames.empty())
        frame = std::clamp(c->second.frames.front(), 0, std::max(0, s.FrameCount() - 1));
    t.width = fw;
    t.height = fh;
    t.bgra.resize(static_cast<size_t>(fw) * fh * 4);
    for (int y = 0; y < fh; ++y)
        std::memcpy(&t.bgra[static_cast<size_t>(y) * fw * 4], s.Pixels() + static_cast<size_t>(y) * s.Width() + frame * fw,
                    static_cast<size_t>(fw) * 4);
    return t;
}

std::vector<CharacterEntry> PetApp::CharacterEntries() {
    const fs::path user = UserCharactersDir();
    std::vector<CharacterEntry> out;
    for (const auto& id : libraryOrder_) {
        const auto& asset = library_.at(id);
        const auto& p = asset->pkg;
        CharacterEntry e;
        e.id = p.id;
        e.name = ToWide(p.name);
        e.version = ToWide(p.version);
        e.author = ToWide(p.license.author);
        e.license = ToWide(p.license.name);
        e.source = ToWide(p.license.source);
        e.model = asset->model != nullptr;
        e.root = p.root;
        e.user = install::IsInside(p.root, user);
        for (const auto& s : config_.pets)
            if (s.character == p.id) e.count += s.count;
        const CharacterThumb& t = ThumbnailFor(*asset);
        e.thumb = t.bgra;
        e.thumbW = t.width;
        e.thumbH = t.height;
        out.push_back(std::move(e));
    }
    for (const auto& r : rejected_) {
        CharacterEntry e;
        e.id = ToUtf8(r.dir.filename().wstring());
        e.name = r.dir.filename().wstring();
        e.rejected = true;
        e.errors = r.errors;
        e.root = r.dir;
        e.user = install::IsInside(r.dir, user);
        out.push_back(std::move(e));
    }
    return out;
}

// 시작할 때 띄울 수 → 설정 파일 [[pets]] 저장 → 그 목록으로 다시 띄운다 (명령줄 --pets 재정의는 쓰지 않음)
std::wstring PetApp::ApplyPetCounts(const std::vector<core::PetSpawn>& list) {
    int total = 0;
    for (const auto& s : list) total += s.count;
    if (total > core::Config::kMaxPets) return ui::TF("characters.counts_range", core::Config::kMaxPets);
    DWORD err = 0;
    if (!SaveConfigFile([&](core::Config& c) { c.pets = list; }, err)) return ui::TF("characters.save_failed", static_cast<int>(err));
    config_.pets = list;
    while (!pets_.empty()) RemovePet(pets_.back().get());
    SpawnPets(config_.pets);
    clock_.Reset(Now());
    log::Info("[characters] counts saved total={} kinds={}", total, list.size());
    return ui::TF("characters.counts_saved", total);
}

std::wstring PetApp::InstallCharacter(const fs::path& source, std::string& installedId) {
    installedId.clear();
    const bool fromFolder = (GetFileAttributesW(source.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
    auto fail = [&](const std::wstring& why) {
        log::Warn("[characters] install rejected from={} reason={}", fromFolder ? "folder" : "zip", ToUtf8(why));
        return ui::TF("characters.install_failed", why);
    };
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return fail(ui::T("characters.err_not_found"));
    const fs::path user = UserCharactersDir();
    if (install::IsInside(source, user) || install::IsInside(user, source)) return fail(ui::T("characters.err_inside"));

    // 작업 폴더: 사용자 캐릭터 폴더와 같은 볼륨, 그 밖 (다시 읽기가 반쯤 복사된 패키지를 보지 않게). 끝나면 지운다
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t gs[64] = {};
    StringFromGUID2(guid, gs, 64);
    const fs::path staging = configPath_.parent_path() / L"install_tmp" / gs;
    std::error_code ec;
    fs::create_directories(staging, ec);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};

    fs::path pkgDir;
    if (fromFolder) {
        if (std::string e = install::CheckTree(source); !e.empty()) return fail(ui::TF("characters.err_unsafe", ToWide(e)));
        const fs::path m = install::FindManifestDir(source);
        if (m.empty()) return fail(ui::T("characters.err_not_package"));
        pkgDir = staging / L"pkg";
        if (std::string e = install::CopyTree(m, pkgDir); !e.empty()) return fail(ui::TF("characters.err_copy", ToWide(e)));
    } else {
        // zip 서명·중앙 디렉터리를 먼저 검사한 파일만 tar.exe 로 푼다 (tar 는 다른 형식도 읽으므로)
        std::vector<core::ZipEntry> entries;
        if (std::string e = core::ReadZipIndex(source, entries); !e.empty()) return fail(ui::TF("characters.err_zip", ToWide(e)));
        if (std::string e = core::CheckZipEntries(entries); !e.empty()) return fail(ui::TF("characters.err_unsafe", ToWide(e)));
        const fs::path x = staging / L"x";
        fs::create_directories(x, ec);
        if (std::string e = install::ExtractZip(source, x); !e.empty()) return fail(ui::TF("characters.err_extract", ToWide(e)));
        if (std::string e = install::CheckTree(x); !e.empty()) return fail(ui::TF("characters.err_unsafe", ToWide(e)));
        // 푼 파일이 검사한 zip 목록(중앙 디렉터리)에 있는 것뿐인지: 로컬 헤더 이름이 목록과 다른 zip 을 거부
        {
            std::vector<std::wstring> listed;
            for (const auto& e : entries) {
                if (e.IsDirectory()) continue;
                std::wstring n = ToWide(e.name);
                std::replace(n.begin(), n.end(), L'/', L'\\');
                for (auto& c : n) c = static_cast<wchar_t>(std::towlower(c));
                listed.push_back(n);
            }
            std::sort(listed.begin(), listed.end());
            for (const auto& f : install::ListFiles(x))
                if (!std::binary_search(listed.begin(), listed.end(), f))
                    return fail(ui::TF("characters.err_unsafe", std::wstring(L"zip 목록에 없는 파일: ") + f));
        }
        pkgDir = install::FindManifestDir(x);
        if (pkgDir.empty()) return fail(ui::T("characters.err_not_package"));
    }

    std::vector<std::string> errors;
    std::string id;
    std::wstring name;
    {
        auto asset = LoadAsset(pkgDir, errors, false);
        if (!asset)
            return fail(ui::TF("characters.err_invalid", ToWide(errors.empty() ? std::string("?") : errors.front()),
                               static_cast<int>(errors.size())));
        id = asset->pkg.id;
        name = ToWide(asset->pkg.name);
    }
    if (core::IsReservedDeviceName(id)) return fail(ui::T("characters.err_reserved"));

    const fs::path dest = user / ToWide(id);
    const bool exists = fs::exists(dest, ec);
    const auto known = library_.find(id);
    if (exists || known != library_.end()) {
        const bool builtIn = known != library_.end() && !install::IsInside(known->second->pkg.root, user);
        std::wstring q = ui::TF("characters.confirm_overwrite", ToWide(id));
        if (exists) q += ui::T("characters.confirm_replace_user");
        if (builtIn) q += ui::T("characters.confirm_shadow");
        HWND owner = characters_.IsOpen() ? characters_.Hwnd() : appWnd_.get();
        if (MessageBoxW(owner, q.c_str(), ui::T("characters.title").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            log::Info("[characters] install cancelled id={}", id);
            return ui::T("characters.cancelled");
        }
        // 기존 사용자 패키지는 휴지통으로 (시험은 바로 지움). 실패하면 그대로 두고 멈춘다
        if (exists && !install::RemoveTree(dest, options_.testHooks)) return fail(ui::T("characters.err_recycle"));
    }
    fs::create_directories(user, ec);
    if (!MoveFileExW(pkgDir.c_str(), dest.c_str(), 0))
        return fail(ui::TF("characters.err_copy", L"MoveFileEx " + std::to_wstring(GetLastError())));
    installedId = id;
    log::Info("[characters] installed id={} from={} overwrite={}", id, fromFolder ? "folder" : "zip", exists);
    ReloadCharacters();
    return ui::TF("characters.installed", name);
}

std::wstring PetApp::DeleteCharacter(const CharacterEntry& entry) {
    const fs::path user = UserCharactersDir();
    if (!install::IsInside(entry.root, user)) return ui::T("characters.err_not_user");
    const std::wstring q = ui::TF("characters.confirm_delete", entry.name);
    HWND owner = characters_.IsOpen() ? characters_.Hwnd() : appWnd_.get();
    if (MessageBoxW(owner, q.c_str(), ui::T("characters.title").c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return ui::T("characters.cancelled");
    // 이 패키지의 펫을 먼저 없애고, 시작 목록에서도 뺀다 → 휴지통 → 다시 읽기
    std::string id;
    for (const auto& [k, a] : library_)
        if (a->pkg.root == entry.root) id = k;
    if (!id.empty()) {
        for (size_t i = pets_.size(); i-- > 0;)
            if (pets_[i]->asset->pkg.id == id) RemovePet(pets_[i].get());
        std::vector<core::PetSpawn> list;
        for (const auto& s : config_.pets)
            if (s.character != id) list.push_back(s);
        if (list.size() != config_.pets.size()) {
            DWORD err = 0;
            if (SaveConfigFile([&](core::Config& c) { c.pets = list; }, err)) config_.pets = list;
        }
    }
    if (!install::RemoveTree(entry.root, options_.testHooks)) {
        log::Warn("[characters] delete failed id={}", id.empty() ? "-" : id);
        ReloadCharacters();
        return ui::T("characters.delete_failed");
    }
    log::Info("[characters] deleted id={} recycle={}", id.empty() ? "(rejected)" : id, !options_.testHooks);
    ReloadCharacters();
    return ui::TF("characters.deleted", entry.name);
}

}  // namespace dp
