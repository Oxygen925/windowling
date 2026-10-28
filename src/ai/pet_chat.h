#pragma once
// 펫끼리 AI 대화 (2026-10-20, 설정으로 켬). 두 펫이 만나 인사할 때 한마디씩 주고받는다.
// 다른 펫의 말은 모델 출력이므로 그대로 넣지 않고 따옴표 데이터로 감싼다 (지시로 해석하지 않게)
#include <string>
#include <string_view>

#include "ai/responses.h"

namespace dp::ai {

inline constexpr size_t kPetChatChars = 60;   // 말풍선 한마디 상한 (코드 포인트)
inline constexpr size_t kPetNameChars = 40;

// 프롬프트에 넣을 짧은 텍스트: 줄바꿈·제어 문자 → 공백, 구분 태그 흉내 제거, maxChars 로 자름
std::string QuoteForPrompt(std::string_view text, size_t maxChars);

// name 의 한마디 요청. otherLine 이 비면 먼저 말 걸기, 아니면 otherName 이 한 말에 답하기.
// languageName 은 "Korean"·"English" 처럼 (UI 언어)
// 먼저 말할 때의 상황: 만남 · 잡기 놀이에서 잡음 · 코딩하는 상대의 화면을 들여다봄
enum class PetChatSituation { Met, Caught, PeekCode };
AiRequest BuildPetChatRequest(std::string_view model, std::string_view name, std::string_view persona,
                              std::string_view otherName, std::string_view otherLine, std::string_view languageName,
                              PetChatSituation situation = PetChatSituation::Met);

}  // namespace dp::ai
