#pragma once
// Responses API 스트림(SSE) 해석과 요청 본문 생성, 펫별 대화 이력 — Windows 비의존.
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace dp::ai {

// ---- SSE (text/event-stream) 증분 파서: 청크가 어디서 잘려도 같은 결과 ----
struct SseEvent {
    std::string event;  // event: 필드 (없으면 빈 문자열)
    std::string data;   // data: 줄들을 \n 으로 이은 값
};
class SseParser {
public:
    void Feed(std::string_view chunk, std::vector<SseEvent>& out);
    // 스트림 끝: 줄 끝 없이 남은 마지막 줄을 처리 (빈 줄로 끝나지 않은 이벤트는 버린다 — SSE 규칙)
    void Finish(std::vector<SseEvent>& out);

private:
    void Line(std::string_view line, std::vector<SseEvent>& out);
    std::string pending_;
    std::string event_, data_;
    bool hasData_ = false;
    bool lastWasCr_ = false;
};

// ---- 스트림 이벤트 해석 ----
struct StreamUpdate {
    enum class Kind { Ignore, Delta, Completed, Failed, Incomplete, Error } kind = Kind::Ignore;
    std::string text;  // Delta
    std::string code;  // Failed/Error: error.code
    std::string message;
};
StreamUpdate InterpretEvent(const SseEvent& e);

// ---- 요청 ----
struct ChatTurn {
    std::string role;  // "user" | "assistant"
    std::string text;
};
// store:false, stream:true 필수. system role 항목은 거부되므로 instructions 로 보낸다 (preview-limitations)
std::string BuildResponsesRequest(std::string_view model, std::string_view instructions, const std::vector<ChatTurn>& turns);

// 공급자 중립 요청 (P4). 공급자가 자기 형식으로 바꾼다 (SIWC = Responses API)
struct AiRequest {
    std::string model, instructions;
    std::vector<ChatTurn> turns;
    std::string imageDataUrl;           // 비지 않으면 마지막 user 항목에 이미지 첨부 ("data:image/jpeg;base64,…")
    std::string imageDetail = "low";
    std::string jsonSchemaName, jsonSchema;  // 비지 않으면 구조화 출력 (text.format json_schema, strict)
};
std::string BuildResponsesRequest(const AiRequest& request);

struct ModelInfo {
    std::string slug, displayName;
};
// GET /v1/models → models[] 중 visibility == "list"
std::vector<ModelInfo> ParseModels(std::string_view body);

// ---- 대화 이력 (펫마다, 메모리만 — P3-R5) ----
class Conversation {
public:
    explicit Conversation(int maxTurns = 10) : maxTurns_(maxTurns) {}
    void SetMaxTurns(int n) { maxTurns_ = n; Trim(); }
    // 사용자 메시지 추가 → 요청에 쓸 이력(방금 메시지 포함)을 돌려준다
    std::vector<ChatTurn> BeginTurn(std::string userText);
    void AppendReply(std::string_view delta) { reply_ += delta; }
    // keepReply: 응답(일부라도)을 이력에 남길지. 응답이 비면 사용자 메시지도 지워 이력이 user/assistant 교대로 유지된다
    void EndTurn(bool keepReply);
    // 펫이 먼저 한 말 (P4 자율 발화). 진행 중인 턴이 있으면 무시
    void AddAssistant(std::string text);
    bool InTurn() const { return inTurn_; }
    const std::string& Reply() const { return reply_; }
    const std::deque<ChatTurn>& History() const { return history_; }
    void Clear();

private:
    void Trim();
    int maxTurns_;
    std::deque<ChatTurn> history_;
    std::string reply_;
    bool inTurn_ = false;
};

// 앱 고정 안내문 + 캐릭터 페르소나 (P3-R6)
// 캐릭터 패키지의 persona(제3자 작성)를 구분자로 감싸 성격·말투로만 쓰게 하고, 규칙을 바꿀 수 없다고 못박은 뒤
// reminder(앞 규칙의 요약)를 다시 붙인다 (보안 점검 2026-10-20: 규칙 뒤에 지시처럼 붙던 것을 고침)
std::string PersonaSection(std::string_view persona, std::string_view reminder);
std::string BuildInstructions(std::string_view characterName, std::string_view persona);

// ---- UTF-8 ----
size_t Utf8Length(std::string_view s);                       // 코드 포인트 수
std::string Utf8Head(std::string_view s, size_t maxChars);   // 앞에서 maxChars 개
std::string Utf8Tail(std::string_view s, size_t maxChars);   // 뒤에서 maxChars 개
std::string Utf8Sanitize(std::string_view s);                // 잘못된 바이트 → U+FFFD

}  // namespace dp::ai
