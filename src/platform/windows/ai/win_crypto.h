#pragma once
// BCrypt(CNG) 래퍼: 난수, SHA-256, RSA PKCS#1 v1.5 + SHA-256 서명 검증(JWK n/e) / 서명(모의 서버·테스트용)
#include <windows.h>

#include <bcrypt.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dp::crypto {

std::vector<uint8_t> RandomBytes(size_t n);
std::string RandomToken(size_t bytes = 32);  // base64url (state·nonce·PKCE verifier)
std::vector<uint8_t> Sha256(std::string_view data);
std::string PkceChallenge(std::string_view verifier);  // base64url(SHA256(verifier)), 패딩 없음
std::string NewUuidV4();                                // 8-4-4-4-12 소문자

// n·e: 빅엔디안 (JWK base64url 디코딩 결과)
bool RsaVerifySha256(const std::vector<uint8_t>& n, const std::vector<uint8_t>& e, std::string_view message,
                     const std::vector<uint8_t>& signature);

// 모의 SIWC 서버·단위 테스트용 RSA 키 (RS256 서명)
class RsaKey {
public:
    RsaKey() = default;
    RsaKey(const RsaKey&) = delete;
    RsaKey& operator=(const RsaKey&) = delete;
    ~RsaKey();
    bool Generate(ULONG bits = 2048);
    std::vector<uint8_t> SignSha256(std::string_view message) const;
    const std::vector<uint8_t>& N() const { return n_; }
    const std::vector<uint8_t>& E() const { return e_; }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    std::vector<uint8_t> n_, e_;
};

// DPAPI (CurrentUser 범위)
std::optional<std::vector<uint8_t>> ProtectForUser(std::string_view plain);
std::optional<std::string> UnprotectForUser(const std::vector<uint8_t>& blob);

}  // namespace dp::crypto
