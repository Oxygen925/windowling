#pragma once
// 127.0.0.1 전용 최소 HTTP/1.1 서버 조각 (Winsock). OAuth 루프백 콜백 수신과 모의 SIWC 서버가 함께 쓴다.
#include <winsock2.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dp::net {

struct MiniRequest {
    std::string method, target, path, query, body;
    std::map<std::string, std::string> headers;  // 이름은 소문자
    std::string Header(const std::string& lowerName) const;
};

bool EnsureWinsock();
bool ReadRequest(SOCKET s, MiniRequest& out, int timeoutMs);
bool SendAll(SOCKET s, std::string_view data);
std::string BuildResponse(int status, std::string_view contentType, std::string_view body, std::string_view extraHeaders = {});

// 127.0.0.1 에서만 받는 리스너. Accept 는 Cancel() 로 다른 스레드에서 깨울 수 있다.
class LoopbackListener {
public:
    LoopbackListener();
    ~LoopbackListener();
    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;

    bool Listen(uint16_t port, std::string& error);  // port 0 = 빈 포트 자동
    uint16_t Port() const { return port_; }
    HANDLE CancelEvent() const { return cancelEvent_; }
    // 연결 1개 수락. 타임아웃·취소 시 INVALID_SOCKET (cancelled 로 구분)
    SOCKET Accept(int timeoutMs, bool& cancelled);
    // 스레드 안전: 대기 중인 Accept 와 읽는 중인 연결(SetActive)을 모두 깨운다. Listen 전에 호출된 취소는
    // Listen 이 초기화하므로, 호출 측은 별도 플래그로 "Listen 전 취소"를 확인해야 한다 (SiwcClient)
    void Cancel();
    void Close();
    void SetActive(SOCKET s);  // 읽는 중인 연결 (INVALID_SOCKET = 없음)

private:
    SOCKET listen_ = INVALID_SOCKET;
    WSAEVENT acceptEvent_ = WSA_INVALID_EVENT;
    HANDLE cancelEvent_ = nullptr;
    uint16_t port_ = 0;
    std::atomic<SOCKET> active_{INVALID_SOCKET};
};

// OAuth 콜백 1회 수신: path 와 일치하는 GET 의 query 를 돌려주고 브라우저에 완료 페이지를 보낸다
// accept: 경로가 맞는 요청의 query 를 받아들일지 (state 확인). false 면 400 으로 답하고 계속 기다린다 (최대 20회).
// 로그인 대기 중 아무 프로세스나 콜백 주소를 한 번 두드려 로그인을 끝내지 못하게 (보안 점검 2026-10-20)
std::optional<std::string> WaitForCallback(LoopbackListener& listener, std::string_view path, int timeoutMs,
                                           bool& cancelled,
                                           const std::function<bool(const std::string&)>& accept = {});

}  // namespace dp::net
