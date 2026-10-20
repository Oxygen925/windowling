#include "platform/windows/ai/http_client.h"

#include <algorithm>

#include "platform/windows/strings.h"

#pragma comment(lib, "winhttp.lib")

namespace dp {

HttpClient::HttpClient() {
    // 프록시: Windows 11 자동 프록시 사용 (WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 8.1+)
    session_ = WinHttpOpen(L"DesktopPet/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
}

HttpClient::~HttpClient() {
    Cancel();
    if (session_) WinHttpCloseHandle(session_);
}

void HttpClient::Cancel() {
    cancel_ = true;
    std::lock_guard lock(mu_);
    if (current_) {
        WinHttpCloseHandle(current_);  // 막혀 있는 WinHttp 호출이 ERROR_WINHTTP_OPERATION_CANCELLED 로 반환
        current_ = nullptr;
    }
}

void HttpClient::ResetCancel() { cancel_ = false; }

HttpResponse HttpClient::Send(const HttpRequest& req) { return Execute(req, nullptr); }

HttpResponse HttpClient::SendStreaming(const HttpRequest& req, const std::function<bool(std::string_view)>& onChunk) {
    return Execute(req, &onChunk);
}

HttpResponse HttpClient::Execute(const HttpRequest& req, const std::function<bool(std::string_view)>* onChunk) {
    HttpResponse res;
    if (!session_) {
        res.winError = ERROR_INVALID_HANDLE;
        return res;
    }
    const std::wstring url = ToWide(req.url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256]{}, path[4096]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[4096]{};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        res.winError = GetLastError();
        return res;
    }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET connect = WinHttpConnect(session_, host, uc.nPort, 0);
    if (!connect) {
        res.winError = GetLastError();
        return res;
    }
    const std::wstring object = std::wstring(path) + extra;
    HINTERNET request = WinHttpOpenRequest(connect, ToWide(req.method).c_str(), object.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        res.winError = GetLastError();
        WinHttpCloseHandle(connect);
        return res;
    }
    {
        std::lock_guard lock(mu_);
        if (cancel_) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            res.cancelled = true;
            return res;
        }
        current_ = request;
    }
    // 핸들 정리: Cancel() 이 이미 닫았으면 다시 닫지 않는다
    auto closeRequest = [&] {
        std::lock_guard lock(mu_);
        if (current_ == request) {
            WinHttpCloseHandle(request);
            current_ = nullptr;
        }
    };
    auto fail = [&] {
        res.winError = GetLastError();
        res.cancelled = cancel_.load();
        res.status = 0;
    };

    WinHttpSetTimeouts(request, req.timeoutMs, req.timeoutMs, req.timeoutMs, req.timeoutMs);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;  // 인증 흐름의 리디렉션은 따르지 않는다
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    std::wstring headers;
    for (const auto& [k, v] : req.headers) headers += ToWide(k) + L": " + ToWide(v) + L"\r\n";
    const BOOL sent = WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                         headers.empty() ? 0 : static_cast<DWORD>(-1L),
                                         req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data()),
                                         static_cast<DWORD>(req.body.size()), static_cast<DWORD>(req.body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        fail();
        closeRequest();
        WinHttpCloseHandle(connect);
        return res;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    res.status = static_cast<int>(status);
    {
        wchar_t rid[128]{};
        DWORD ridSize = sizeof(rid);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"x-request-id", rid, &ridSize, WINHTTP_NO_HEADER_INDEX))
            res.requestId = ToUtf8(rid);
    }
    if (res.status >= 300 && res.status < 400) {
        DWORD locSize = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &locSize, WINHTTP_NO_HEADER_INDEX);
        if (locSize > 0 && locSize < 8192) {
            std::wstring loc(locSize / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc.data(), &locSize,
                                    WINHTTP_NO_HEADER_INDEX)) {
                loc.resize(locSize / sizeof(wchar_t));
                res.location = ToUtf8(loc);
            }
        }
    }
    const bool stream = onChunk && res.Ok();
    std::string buf;
    for (;;) {
        // 동기 모드의 WinHttpReadData 는 버퍼를 채울 때까지 기다릴 수 있다 → 도착한 만큼만 읽는다 (스트림 실시간성)
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            const int keep = res.status;
            fail();
            res.status = stream ? keep : 0;
            break;
        }
        if (avail == 0) break;  // 응답 끝
        char chunk[8192];
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk, std::min<DWORD>(avail, sizeof(chunk)), &read)) {
            const int keep = res.status;
            fail();
            res.status = stream ? keep : 0;  // 스트림 도중 끊김: 상태 코드는 유지하고 winError 로 구분
            break;
        }
        if (read == 0) break;
        if (stream) {
            if (!(*onChunk)(std::string_view(chunk, read))) break;
        } else if (res.body.size() < 4 * 1024 * 1024) {
            res.body.append(chunk, read);
        }
    }
    closeRequest();
    WinHttpCloseHandle(connect);
    return res;
}

}  // namespace dp
