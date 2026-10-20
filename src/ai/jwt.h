#pragma once
// JWT(ID 토큰) 파싱과 클레임 검증 — Windows 비의존. nlohmann/json 이 필요한 코드만 이 헤더를 포함한다
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/siwc_protocol.h"

namespace dp::ai {

struct Jwt {
    nlohmann::json header, payload;
    std::string signingInput;  // base64url(header) "." base64url(payload)
    std::vector<uint8_t> signature;
};
std::optional<Jwt> ParseJwt(std::string_view token);
// 서명 검증은 호출자(플랫폼). 여기서는 alg·iss·aud·azp·exp·iat·nonce·sub 를 확인. 실패 시 사유, 성공 시 빈 문자열.
// nonce 가 비면 nonce 확인을 건너뛴다 (토큰 갱신 응답의 ID 토큰)
std::string ValidateIdTokenClaims(const Jwt& jwt, std::string_view issuer, std::string_view audience,
                                  std::string_view nonce, int64_t nowUnix, Identity& out, int64_t leewaySec = 60);

}  // namespace dp::ai
