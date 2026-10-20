#pragma once
// 캐릭터 패키지 .zip 설치 전 검사 (UI-08): 압축을 풀기 전에 zip 의 중앙 디렉터리를 직접 읽어
// 항목 이름·크기·개수·종류를 확인한다. 압축 해제(Windows tar.exe) 뒤에는 플랫폼 계층이 파일 트리를 다시 검사한다.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dp::core {

struct ZipEntry {
    std::string name;            // zip 안 경로 (원본 바이트. UTF-8 플래그가 없으면 코드 페이지 불명)
    uint64_t compressedSize = 0, size = 0;
    uint16_t flags = 0, method = 0;
    uint8_t hostOs = 0;          // versionMadeBy 상위 바이트 (3 = Unix)
    uint32_t externalAttr = 0;
    bool IsDirectory() const { return !name.empty() && (name.back() == '/' || name.back() == '\\'); }
};

struct ZipLimits {
    uint64_t maxArchiveBytes = 64ull * 1024 * 1024;
    uint64_t maxFileBytes = 32ull * 1024 * 1024;   // PackageLimits 의 가장 큰 파일(모델 .glb)과 같다
    uint64_t maxTotalBytes = 128ull * 1024 * 1024;
    size_t maxEntries = 1000;
    int maxDepth = 8;
};

// zip 파일 바이트 → 항목 목록. 실패하면 빈 문자열이 아닌 이유. zip64·여러 디스크·암호화는 지원하지 않는다
std::string ParseZipIndex(const std::vector<uint8_t>& bytes, std::vector<ZipEntry>& out);
// 파일에서 읽기 (크기 상한 먼저 확인, 첫 4바이트가 zip 로컬 헤더 서명이어야 함)
std::string ReadZipIndex(const std::filesystem::path& path, std::vector<ZipEntry>& out, const ZipLimits& limits = {});
// 항목 검사: 경로 이탈('..'·절대 경로·드라이브·':'), 심볼릭 링크, 암호화, 크기·개수·깊이 상한
std::string CheckZipEntries(const std::vector<ZipEntry>& entries, const ZipLimits& limits = {});

// Windows 예약 장치 이름(con, nul, com1 …)이면 true — 폴더 이름으로 쓸 수 없다
bool IsReservedDeviceName(const std::string& name);

}  // namespace dp::core
