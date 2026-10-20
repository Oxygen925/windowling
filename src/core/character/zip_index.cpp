#include "core/character/zip_index.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace dp::core {

namespace {

constexpr uint32_t kLocalSig = 0x04034b50, kCentralSig = 0x02014b50, kEndSig = 0x06054b50;
constexpr size_t kEndSize = 22;

uint16_t U16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b[at] | b[at + 1] << 8); }
uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 | static_cast<uint32_t>(b[at + 2]) << 16 |
           static_cast<uint32_t>(b[at + 3]) << 24;
}

}  // namespace

std::string ParseZipIndex(const std::vector<uint8_t>& b, std::vector<ZipEntry>& out) {
    out.clear();
    if (b.size() < kEndSize) return "zip 이 아님 (너무 짧음)";
    if (U32(b, 0) != kLocalSig && U32(b, 0) != kEndSig) return "zip 이 아님 (서명)";
    // 끝 레코드: 뒤에서부터 찾는다 (주석 최대 65535바이트)
    size_t end = std::string::npos;
    const size_t lowest = b.size() > kEndSize + 65535 ? b.size() - kEndSize - 65535 : 0;
    for (size_t at = b.size() - kEndSize + 1; at-- > lowest;)
        if (U32(b, at) == kEndSig && at + kEndSize + U16(b, at + 20) == b.size()) {
            end = at;
            break;
        }
    if (end == std::string::npos) return "zip 끝 레코드 없음";
    const uint16_t disk = U16(b, end + 4), cdDisk = U16(b, end + 6);
    const uint16_t entriesHere = U16(b, end + 8), entries = U16(b, end + 10);
    const uint32_t cdSize = U32(b, end + 12), cdOffset = U32(b, end + 16);
    if (entries == 0xFFFF || cdSize == 0xFFFFFFFF || cdOffset == 0xFFFFFFFF) return "zip64 는 지원 안 함";
    if (disk != 0 || cdDisk != 0 || entriesHere != entries) return "여러 디스크 zip 은 지원 안 함";
    if (static_cast<uint64_t>(cdOffset) + cdSize > end) return "중앙 디렉터리 위치 오류";
    size_t at = cdOffset;
    for (uint16_t i = 0; i < entries; ++i) {
        if (at + 46 > end || U32(b, at) != kCentralSig) return "중앙 디렉터리 항목 오류";
        ZipEntry e;
        e.hostOs = static_cast<uint8_t>(U16(b, at + 4) >> 8);
        e.flags = U16(b, at + 8);
        e.method = U16(b, at + 10);
        const uint32_t comp = U32(b, at + 20), size = U32(b, at + 24);
        const uint16_t nameLen = U16(b, at + 28), extraLen = U16(b, at + 30), commentLen = U16(b, at + 32);
        e.externalAttr = U32(b, at + 38);
        if (comp == 0xFFFFFFFF || size == 0xFFFFFFFF) return "zip64 는 지원 안 함";
        e.compressedSize = comp;
        e.size = size;
        const size_t next = at + 46 + nameLen + extraLen + commentLen;
        if (next > end) return "중앙 디렉터리 항목 오류";
        e.name.assign(reinterpret_cast<const char*>(&b[at + 46]), nameLen);
        out.push_back(std::move(e));
        at = next;
    }
    return "";
}

std::string ReadZipIndex(const std::filesystem::path& path, std::vector<ZipEntry>& out, const ZipLimits& limits) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return "파일을 읽을 수 없음";
    if (size > limits.maxArchiveBytes) return "zip 이 너무 큼";
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return "파일을 읽을 수 없음";
    if (bytes.size() < 4 || U32(bytes, 0) != kLocalSig) return "zip 이 아님 (서명)";
    return ParseZipIndex(bytes, out);
}

std::string CheckZipEntries(const std::vector<ZipEntry>& entries, const ZipLimits& limits) {
    if (entries.empty()) return "빈 zip";
    if (entries.size() > limits.maxEntries) return "항목이 너무 많음";
    uint64_t total = 0;
    for (const auto& e : entries) {
        const std::string& n = e.name;
        if (n.empty()) return "이름 없는 항목";
        if (e.flags & 0x1) return "암호화된 항목: " + n;
        if (e.hostOs == 3 && ((e.externalAttr >> 16) & 0xF000) == 0xA000) return "심볼릭 링크 항목: " + n;
        if (n.front() == '/' || n.front() == '\\') return "절대 경로 항목: " + n;
        for (unsigned char c : n)
            if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return "쓸 수 없는 문자가 든 이름: " + n;
        int depth = 0;
        size_t start = 0;
        while (start <= n.size()) {
            size_t sep = n.find_first_of("/\\", start);
            if (sep == std::string::npos) sep = n.size();
            const std::string part = n.substr(start, sep - start);
            if (part == "..") return "상위 폴더로 나가는 항목: " + n;
            if (!part.empty()) ++depth;
            start = sep + 1;
        }
        if (depth > limits.maxDepth) return "폴더가 너무 깊음: " + n;
        if (e.size > limits.maxFileBytes) return "파일이 너무 큼: " + n;
        total += e.size;
        if (total > limits.maxTotalBytes) return "압축을 푼 크기가 너무 큼";
    }
    return "";
}

bool IsReservedDeviceName(const std::string& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "con" || n == "prn" || n == "aux" || n == "nul") return true;
    return n.size() == 4 && (n.starts_with("com") || n.starts_with("lpt")) && n[3] >= '1' && n[3] <= '9';
}

}  // namespace dp::core
