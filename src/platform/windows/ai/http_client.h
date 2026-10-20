#pragma once
// WinHTTP 동기 클라이언트 (P3-R2). AI 작업 스레드에서만 호출하고, Cancel() 은 다른 스레드(UI)에서 호출할 수 있다.
// 취소는 진행 중인 요청 핸들을 닫아 막혀 있는 송수신 호출을 즉시 실패시키는 방식이다.
#include <windows.h>

#include <winhttp.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dp {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 30000;  // 연결·송신·수신(읽기 1회) 각각
};

struct HttpResponse {
    int status = 0;          // 0 = 응답 없음 (네트워크 오류·취소)
    std::string body;        // 스트림 모드에서는 비어 있음 (오류 응답이면 본문 일부)
    DWORD winError = 0;
    bool cancelled = false;
    std::string requestId;   // x-request-id 응답 헤더 (오류 진단용, 비밀 아님)
    std::string location;    // 3xx 의 Location (리디렉션은 따르지 않으므로 호출 측이 판단)
    bool Ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse Send(const HttpRequest& req);
    // 2xx 응답이면 본문을 조각마다 onChunk 로 넘긴다 (false 를 돌려주면 중단). 2xx 가 아니면 본문을 모아 돌려준다.
    HttpResponse SendStreaming(const HttpRequest& req, const std::function<bool(std::string_view)>& onChunk);
    void Cancel();         // 스레드 안전: 진행 중인 요청 취소
    void ResetCancel();    // 다음 요청 전에 호출

private:
    HttpResponse Execute(const HttpRequest& req, const std::function<bool(std::string_view)>* onChunk);
    HINTERNET session_ = nullptr;
    std::mutex mu_;
    HINTERNET current_ = nullptr;  // 진행 중인 요청 핸들 (mu_ 보호)
    std::atomic<bool> cancel_{false};
};

}  // namespace dp
