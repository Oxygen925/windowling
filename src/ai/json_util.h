#pragma once
// 신뢰할 수 없는 JSON(서버 응답·모델 출력·로컬 상태 파일)에서 값 읽기. nlohmann 의 value(key, default) 는
// 키가 있는데 null 이거나 타입이 다르면 예외를 던져(작업 스레드면 프로세스 종료) 쓰지 않는다 (보안 점검 2026-10-20).
// .cpp 에서만 포함한다 (공개 헤더에 nlohmann 을 노출하지 않음).
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace dp::ai {

inline const nlohmann::json* JField(const nlohmann::json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

// 문자열이 아니면 기본값
inline std::string JStr(const nlohmann::json& j, const char* key, const std::string& def = {}) {
    const auto* v = JField(j, key);
    return v && v->is_string() ? v->get<std::string>() : def;
}

// 정수가 아니면 기본값 (실수는 잘라서)
inline int64_t JInt(const nlohmann::json& j, const char* key, int64_t def = 0) {
    const auto* v = JField(j, key);
    if (!v) return def;
    if (v->is_number_integer()) return v->get<int64_t>();
    if (v->is_number_float()) return static_cast<int64_t>(v->get<double>());
    return def;
}

inline bool JBool(const nlohmann::json& j, const char* key, bool def = false) {
    const auto* v = JField(j, key);
    return v && v->is_boolean() ? v->get<bool>() : def;
}

}  // namespace dp::ai
