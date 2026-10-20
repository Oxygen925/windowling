// UI-08: 캐릭터 .zip 설치 전 검사 — 중앙 디렉터리 읽기와 항목 검사
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/character/zip_index.h"

using namespace dp::core;

namespace {

struct Item {
    std::string name, data;
    uint8_t host = 0;
    uint32_t attr = 0;
    uint16_t flags = 0;
};

void Put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>& b, uint32_t v) {
    Put16(b, v & 0xFFFF);
    Put16(b, v >> 16);
}

// 무압축(stored) zip 을 만든다 (CRC 는 검사하지 않으므로 0)
std::vector<uint8_t> MakeZip(const std::vector<Item>& items) {
    std::vector<uint8_t> b, cd;
    for (const auto& it : items) {
        const uint32_t offset = static_cast<uint32_t>(b.size());
        Put32(b, 0x04034b50);
        Put16(b, 20), Put16(b, it.flags), Put16(b, 0), Put16(b, 0), Put16(b, 0);
        Put32(b, 0), Put32(b, static_cast<uint32_t>(it.data.size())), Put32(b, static_cast<uint32_t>(it.data.size()));
        Put16(b, static_cast<uint32_t>(it.name.size())), Put16(b, 0);
        b.insert(b.end(), it.name.begin(), it.name.end());
        b.insert(b.end(), it.data.begin(), it.data.end());

        Put32(cd, 0x02014b50);
        Put16(cd, static_cast<uint32_t>(it.host) << 8 | 20), Put16(cd, 20), Put16(cd, it.flags), Put16(cd, 0);
        Put16(cd, 0), Put16(cd, 0);
        Put32(cd, 0), Put32(cd, static_cast<uint32_t>(it.data.size())), Put32(cd, static_cast<uint32_t>(it.data.size()));
        Put16(cd, static_cast<uint32_t>(it.name.size())), Put16(cd, 0), Put16(cd, 0), Put16(cd, 0), Put16(cd, 0);
        Put32(cd, it.attr), Put32(cd, offset);
        cd.insert(cd.end(), it.name.begin(), it.name.end());
    }
    const uint32_t cdOffset = static_cast<uint32_t>(b.size());
    b.insert(b.end(), cd.begin(), cd.end());
    Put32(b, 0x06054b50);
    Put16(b, 0), Put16(b, 0), Put16(b, static_cast<uint32_t>(items.size())), Put16(b, static_cast<uint32_t>(items.size()));
    Put32(b, static_cast<uint32_t>(cd.size())), Put32(b, cdOffset), Put16(b, 0);
    return b;
}

std::string Check(const std::vector<Item>& items, const ZipLimits& limits = {}) {
    std::vector<ZipEntry> entries;
    const std::string e = ParseZipIndex(MakeZip(items), entries);
    if (!e.empty()) return "parse: " + e;
    return CheckZipEntries(entries, limits);
}

}  // namespace

TEST_CASE("Zip index reads names and sizes", "[zip]") {
    std::vector<ZipEntry> entries;
    REQUIRE(ParseZipIndex(MakeZip({{"pkg/", ""}, {"pkg/manifest.toml", "id = 'x'"}, {"pkg/atlas.png", "12345"}}), entries).empty());
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].IsDirectory());
    CHECK(entries[1].name == "pkg/manifest.toml");
    CHECK(entries[1].size == 8);
    CHECK(entries[2].size == 5);
    CHECK(CheckZipEntries(entries).empty());
}

TEST_CASE("Zip index rejects unsafe entries", "[zip]") {
    CHECK_FALSE(Check({{"../evil.txt", "x"}}).empty());
    CHECK_FALSE(Check({{"pkg/../../evil.txt", "x"}}).empty());
    CHECK_FALSE(Check({{"pkg\\..\\evil.txt", "x"}}).empty());
    CHECK_FALSE(Check({{"/abs.txt", "x"}}).empty());
    CHECK_FALSE(Check({{"C:/win.txt", "x"}}).empty());
    CHECK_FALSE(Check({{"pkg/file.txt:stream", "x"}}).empty());
    CHECK_FALSE(Check({{"pkg/link", "target", 3, 0xA1FFu << 16}}).empty());     // Unix 심볼릭 링크
    CHECK(Check({{"pkg/file", "data", 3, 0x81A4u << 16}}).empty());             // Unix 일반 파일은 통과
    CHECK_FALSE(Check({{"pkg/secret", "x", 0, 0, 0x1}}).empty());               // 암호화
    ZipLimits small;
    small.maxFileBytes = 4;
    CHECK_FALSE(Check({{"pkg/big", "12345"}}, small).empty());
    small = {};
    small.maxTotalBytes = 6;
    CHECK_FALSE(Check({{"a", "1234"}, {"b", "1234"}}, small).empty());
    small = {};
    small.maxEntries = 1;
    CHECK_FALSE(Check({{"a", "1"}, {"b", "2"}}, small).empty());
    CHECK_FALSE(Check({{"a/b/c/d/e/f/g/h/i/j.txt", "x"}}).empty());           // 깊이 9 > 8
}

TEST_CASE("Zip index rejects broken archives", "[zip]") {
    std::vector<ZipEntry> entries;
    CHECK_FALSE(ParseZipIndex({}, entries).empty());
    CHECK_FALSE(ParseZipIndex(std::vector<uint8_t>(64, 0), entries).empty());
    auto z = MakeZip({{"a", "1"}});
    z.resize(z.size() - 4);  // 끝 레코드 잘림
    CHECK_FALSE(ParseZipIndex(z, entries).empty());
}

TEST_CASE("Reserved device names", "[zip]") {
    CHECK(IsReservedDeviceName("con"));
    CHECK(IsReservedDeviceName("NUL"));
    CHECK(IsReservedDeviceName("com1"));
    CHECK(IsReservedDeviceName("lpt9"));
    CHECK_FALSE(IsReservedDeviceName("com10"));
    CHECK_FALSE(IsReservedDeviceName("test-blue"));
    CHECK_FALSE(IsReservedDeviceName("console"));
}
