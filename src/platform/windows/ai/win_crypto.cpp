#include "platform/windows/ai/win_crypto.h"

#include <dpapi.h>

#include <cstdio>

#include "ai/siwc_protocol.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace dp::crypto {

namespace {
constexpr char kEntropy[] = "DesktopPet.SIWC.v1";
bool Ok(NTSTATUS s) { return s >= 0; }
}  // namespace

std::vector<uint8_t> RandomBytes(size_t n) {
    std::vector<uint8_t> out(n);
    if (!Ok(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) out.clear();
    return out;
}

std::string RandomToken(size_t bytes) {
    const auto r = RandomBytes(bytes);
    return ai::Base64UrlEncode(r.data(), r.size());
}

std::vector<uint8_t> Sha256(std::string_view data) {
    std::vector<uint8_t> out(32);
    if (!Ok(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), out.data(), 32)))
        out.clear();
    return out;
}

std::string PkceChallenge(std::string_view verifier) {
    const auto h = Sha256(verifier);
    return ai::Base64UrlEncode(h.data(), h.size());
}

std::string NewUuidV4() {
    auto b = RandomBytes(16);
    if (b.size() != 16) return {};
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
    char s[37];
    std::snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
                  b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

bool RsaVerifySha256(const std::vector<uint8_t>& n, const std::vector<uint8_t>& e, std::string_view message,
                     const std::vector<uint8_t>& signature) {
    if (n.empty() || e.empty() || signature.empty()) return false;
    // BCRYPT_RSAKEY_BLOB: 헤더 + 공개 지수 + 모듈러스 (빅엔디안)
    std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + e.size() + n.size());
    auto* hdr = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
    hdr->Magic = BCRYPT_RSAPUBLIC_MAGIC;
    hdr->BitLength = static_cast<ULONG>(n.size() * 8);
    hdr->cbPublicExp = static_cast<ULONG>(e.size());
    hdr->cbModulus = static_cast<ULONG>(n.size());
    std::copy(e.begin(), e.end(), blob.begin() + sizeof(BCRYPT_RSAKEY_BLOB));
    std::copy(n.begin(), n.end(), blob.begin() + sizeof(BCRYPT_RSAKEY_BLOB) + e.size());
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;
    if (Ok(BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, nullptr, 0)) &&
        Ok(BCryptImportKeyPair(alg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0))) {
        auto digest = Sha256(message);
        BCRYPT_PKCS1_PADDING_INFO pad{BCRYPT_SHA256_ALGORITHM};
        ok = digest.size() == 32 && Ok(BCryptVerifySignature(key, &pad, digest.data(), 32,
                                                              const_cast<PUCHAR>(signature.data()),
                                                              static_cast<ULONG>(signature.size()), BCRYPT_PAD_PKCS1));
    }
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

RsaKey::~RsaKey() {
    if (key_) BCryptDestroyKey(key_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
}

bool RsaKey::Generate(ULONG bits) {
    if (!Ok(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_RSA_ALGORITHM, nullptr, 0))) return false;
    if (!Ok(BCryptGenerateKeyPair(alg_, &key_, bits, 0)) || !Ok(BCryptFinalizeKeyPair(key_, 0))) return false;
    ULONG size = 0;
    if (!Ok(BCryptExportKey(key_, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0))) return false;
    std::vector<uint8_t> blob(size);
    if (!Ok(BCryptExportKey(key_, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0))) return false;
    const auto* hdr = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(blob.data());
    const uint8_t* p = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    e_.assign(p, p + hdr->cbPublicExp);
    n_.assign(p + hdr->cbPublicExp, p + hdr->cbPublicExp + hdr->cbModulus);
    return true;
}

std::vector<uint8_t> RsaKey::SignSha256(std::string_view message) const {
    auto digest = Sha256(message);
    BCRYPT_PKCS1_PADDING_INFO pad{BCRYPT_SHA256_ALGORITHM};
    ULONG size = 0;
    if (!key_ || !Ok(BCryptSignHash(key_, &pad, digest.data(), 32, nullptr, 0, &size, BCRYPT_PAD_PKCS1))) return {};
    std::vector<uint8_t> sig(size);
    if (!Ok(BCryptSignHash(key_, &pad, digest.data(), 32, sig.data(), size, &size, BCRYPT_PAD_PKCS1))) return {};
    sig.resize(size);
    return sig;
}

std::optional<std::vector<uint8_t>> ProtectForUser(std::string_view plain) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"DesktopPet SIWC", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return std::nullopt;
    std::vector<uint8_t> v(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return v;
}

std::optional<std::string> UnprotectForUser(const std::vector<uint8_t>& blob) {
    DATA_BLOB in{static_cast<DWORD>(blob.size()), const_cast<BYTE*>(blob.data())};
    DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string s(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

}  // namespace dp::crypto
