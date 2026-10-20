#include "platform/windows/ai/mini_http.h"

#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

namespace dp::net {

std::string MiniRequest::Header(const std::string& lowerName) const {
    auto it = headers.find(lowerName);
    return it == headers.end() ? std::string() : it->second;
}

bool EnsureWinsock() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    });
    return ok;
}

namespace {
bool WaitReadable(SOCKET s, int timeoutMs) {
    WSAPOLLFD p{s, POLLRDNORM, 0};
    return WSAPoll(&p, 1, timeoutMs) > 0;
}
}  // namespace

bool ReadRequest(SOCKET s, MiniRequest& out, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto remaining = [&] {
        return static_cast<int>(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()));
    };
    std::string buf;
    size_t headerEnd = std::string::npos;
    while ((headerEnd = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 64 * 1024 || !WaitReadable(s, remaining())) return false;
        char tmp[4096];
        const int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buf.append(tmp, n);
    }
    const std::string head = buf.substr(0, headerEnd);
    size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);
    const size_t sp1 = requestLine.find(' '), sp2 = requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    out.method = requestLine.substr(0, sp1);
    out.target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t qm = out.target.find('?');
    out.path = out.target.substr(0, qm);
    out.query = qm == std::string::npos ? std::string() : out.target.substr(qm + 1);
    size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        const std::string line = head.substr(pos, e - pos);
        const size_t c = line.find(':');
        if (c != std::string::npos) {
            std::string name = line.substr(0, c);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::string value = line.substr(c + 1);
            value.erase(0, value.find_first_not_of(' '));
            out.headers[name] = value;
        }
        pos = e + 2;
    }
    out.body = buf.substr(headerEnd + 4);
    const size_t length = static_cast<size_t>(std::strtoull(out.Header("content-length").c_str(), nullptr, 10));
    if (length > 4 * 1024 * 1024) return false;
    while (out.body.size() < length) {
        if (!WaitReadable(s, remaining())) return false;
        char tmp[4096];
        const int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        out.body.append(tmp, n);
    }
    out.body.resize(length);
    return true;
}

bool SendAll(SOCKET s, std::string_view data) {
    while (!data.empty()) {
        const int n = send(s, data.data(), static_cast<int>(std::min<size_t>(data.size(), 1 << 20)), 0);
        if (n <= 0) return false;
        data.remove_prefix(static_cast<size_t>(n));
    }
    return true;
}

std::string BuildResponse(int status, std::string_view contentType, std::string_view body, std::string_view extraHeaders) {
    const char* reason = status == 200 ? "OK" : status == 302 ? "Found" : status == 400 ? "Bad Request"
                       : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden" : status == 404 ? "Not Found"
                       : status == 429 ? "Too Many Requests" : status == 503 ? "Service Unavailable" : "Status";
    std::string r = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    if (!contentType.empty()) r += "Content-Type: " + std::string(contentType) + "\r\n";
    r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\nConnection: close\r\n";
    r += extraHeaders;
    r += "\r\n";
    r += body;
    return r;
}

// ---------------------------------------------------------------------------

LoopbackListener::LoopbackListener() { cancelEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

LoopbackListener::~LoopbackListener() {
    Close();
    if (cancelEvent_) CloseHandle(cancelEvent_);
}

bool LoopbackListener::Listen(uint16_t port, std::string& error) {
    Close();
    if (!EnsureWinsock()) {
        error = "WSAStartup 실패";
        return false;
    }
    ResetEvent(cancelEvent_);
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) {
        error = "socket 실패 " + std::to_string(WSAGetLastError());
        return false;
    }
    BOOL exclusive = TRUE;  // 같은 포트를 다른 프로세스가 가로채지 못하게
    setsockopt(listen_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 전용
    if (bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int e = WSAGetLastError();
        error = e == WSAEADDRINUSE || e == WSAEACCES ? "port_in_use" : "bind 실패 " + std::to_string(e);
        Close();
        return false;
    }
    if (listen(listen_, 8) != 0) {
        error = "listen 실패 " + std::to_string(WSAGetLastError());
        Close();
        return false;
    }
    int len = sizeof(addr);
    getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    acceptEvent_ = WSACreateEvent();
    WSAEventSelect(listen_, acceptEvent_, FD_ACCEPT);
    return true;
}

SOCKET LoopbackListener::Accept(int timeoutMs, bool& cancelled) {
    cancelled = false;
    if (listen_ == INVALID_SOCKET) return INVALID_SOCKET;
    for (;;) {
        SOCKET c = accept(listen_, nullptr, nullptr);
        if (c != INVALID_SOCKET) {
            // 리스너의 이벤트 선택이 상속되므로 해제하고 블로킹으로 되돌린다
            WSAEventSelect(c, nullptr, 0);
            u_long nb = 0;
            ioctlsocket(c, FIONBIO, &nb);
            return c;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK) return INVALID_SOCKET;
        HANDLE hs[2] = {acceptEvent_, cancelEvent_};
        const DWORD w = WSAWaitForMultipleEvents(2, hs, FALSE, timeoutMs < 0 ? WSA_INFINITE : static_cast<DWORD>(timeoutMs), FALSE);
        if (w == WSA_WAIT_EVENT_0 + 1) {
            cancelled = true;
            return INVALID_SOCKET;
        }
        if (w != WSA_WAIT_EVENT_0) return INVALID_SOCKET;  // 타임아웃
        WSAResetEvent(acceptEvent_);
    }
}

void LoopbackListener::Cancel() {
    if (cancelEvent_) SetEvent(cancelEvent_);
    const SOCKET a = active_.load();
    if (a != INVALID_SOCKET) shutdown(a, SD_BOTH);  // 읽기 대기(recv)를 즉시 끝낸다. 닫기는 소유 스레드가 한다
}

void LoopbackListener::SetActive(SOCKET s) { active_ = s; }

void LoopbackListener::Close() {
    if (listen_ != INVALID_SOCKET) closesocket(listen_);
    listen_ = INVALID_SOCKET;
    if (acceptEvent_ != WSA_INVALID_EVENT) WSACloseEvent(acceptEvent_);
    acceptEvent_ = WSA_INVALID_EVENT;
    port_ = 0;
}

std::optional<std::string> WaitForCallback(LoopbackListener& listener, std::string_view path, int timeoutMs,
                                           bool& cancelled, const std::function<bool(const std::string&)>& accept) {
    int rejected = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return std::nullopt;
        SOCKET c = listener.Accept(static_cast<int>(left), cancelled);
        if (c == INVALID_SOCKET) return std::nullopt;
        MiniRequest req;
        listener.SetActive(c);
        const bool ok = ReadRequest(c, req, 5000);
        listener.SetActive(INVALID_SOCKET);
        if (!ok && WaitForSingleObject(listener.CancelEvent(), 0) == WAIT_OBJECT_0) {
            closesocket(c);
            cancelled = true;
            return std::nullopt;
        }
        if (ok && req.method == "GET" && req.path == path && accept && !accept(req.query)) {
            SendAll(c, BuildResponse(400, "text/plain", "invalid request"));
            closesocket(c);
            if (++rejected >= 20) return std::nullopt;
            continue;
        }
        if (ok && req.method == "GET" && req.path == path) {
            static constexpr char kPage[] =
                "<!doctype html><meta charset=\"utf-8\"><title>Desktop Pet</title>"
                "<body style=\"font-family:sans-serif;padding:2em\"><h2>Desktop Pet</h2>"
                "<p>로그인 결과를 앱으로 보냈습니다. 이 탭은 닫아도 됩니다.</p></body>";
            SendAll(c, BuildResponse(200, "text/html; charset=utf-8", kPage));
            closesocket(c);
            return req.query;
        }
        SendAll(c, BuildResponse(404, "text/plain", "not found"));
        closesocket(c);
    }
}

}  // namespace dp::net
