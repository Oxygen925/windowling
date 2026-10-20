#pragma once
// 캐릭터 패키지 설치·삭제의 파일 작업 (UI-08). 판단(패키지 검사·확인)은 PetApp, 여기는 안전한 파일 조작만.
// - 트리 검사: 재분석 지점(심볼릭 링크·정션)을 거부하고 개수·크기·깊이 상한을 본다. 폴더 설치와 zip 해제 결과에 같이 쓴다
// - 복사: std::filesystem 은 링크를 따라가므로 쓰지 않고 CreateDirectoryW/CopyFileW 로 직접
// - zip 해제: 시스템 폴더의 tar.exe (zip 서명·중앙 디렉터리 검사를 통과한 파일만)
#include <filesystem>
#include <string>
#include <vector>

#include "core/character/zip_index.h"

namespace dp::install {

// 실패하면 이유 (빈 문자열 = 통과)
std::string CheckTree(const std::filesystem::path& dir, const core::ZipLimits& limits = {});
std::string CopyTree(const std::filesystem::path& from, const std::filesystem::path& to);
// 푸는 동안 to 의 크기·개수를 0.2초마다 확인해 상한을 넘으면 tar 를 멈춘다 (zip 목록의 크기는 거짓일 수 있다)
std::string ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& to, const core::ZipLimits& limits = {},
                       int timeoutMs = 30000);
// dir 아래 파일의 상대 경로 (소문자, \\ 구분). 재분석 지점은 따라가지 않는다
std::vector<std::wstring> ListFiles(const std::filesystem::path& dir);
// manifest.toml 이 있는 폴더: dir 자체, 아니면 하위 폴더가 하나뿐이고 거기에 있으면 그 폴더
std::filesystem::path FindManifestDir(const std::filesystem::path& dir);
// 휴지통으로 (hardDelete = 시험용: 바로 지움 — 사용자 휴지통에 시험 파일을 남기지 않는다)
bool RemoveTree(const std::filesystem::path& dir, bool hardDelete);
bool IsInside(const std::filesystem::path& child, const std::filesystem::path& parent);

}  // namespace dp::install
