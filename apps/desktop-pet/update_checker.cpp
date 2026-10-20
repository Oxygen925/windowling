#include "update_checker.h"

#include "dp_version.h"
#include "platform/windows/ai/http_client.h"

namespace dp {

UpdateChecker::~UpdateChecker() {
    if (thread_.joinable()) thread_.join();  // 요청은 시간 제한(10초)으로 끝난다
}

bool UpdateChecker::Start(HWND notifyWnd, UINT notifyMsg, std::string url, std::string repo) {
    {
        std::lock_guard lock(mu_);
        if (busy_) return false;
        busy_ = true;
    }
    if (thread_.joinable()) thread_.join();
    thread_ = std::thread([this, notifyWnd, notifyMsg, url = std::move(url), repo = std::move(repo)] {
        UpdateResult r;
        HttpClient http;
        std::string current = url;
        for (int hop = 0;; ++hop) {
            HttpRequest req;
            req.url = current;
            req.timeoutMs = 10000;
            req.headers = {{"User-Agent", std::string("DesktopPet/") + DP_VERSION_STRING},
                           {"Accept", "application/json, application/octet-stream"}};
            const HttpResponse res = http.Send(req);
            if (res.status >= 300 && res.status < 400 && !res.location.empty()) {
                if (hop >= 3) {
                    r.code = "too_many_redirects";
                    break;
                }
                if (!core::IsAllowedUpdateRedirect(res.location, current)) {
                    r.code = "redirect_denied";
                    break;
                }
                current = res.location;
                continue;
            }
            if (!res.Ok()) {
                r.code = res.status ? "http_" + std::to_string(res.status) : "network_" + std::to_string(res.winError);
                break;
            }
            if (res.body.size() > 64 * 1024) {
                r.code = "too_large";
                break;
            }
            const std::string err = core::ParseLatestJson(res.body, repo, r.info);
            if (!err.empty()) {
                r.code = "invalid: " + err;
                break;
            }
            r.ok = true;
            break;
        }
        {
            std::lock_guard lock(mu_);
            result_ = std::move(r);
            busy_ = false;
        }
        PostMessageW(notifyWnd, notifyMsg, 0, 0);
    });
    return true;
}

std::optional<UpdateResult> UpdateChecker::Take() {
    std::lock_guard lock(mu_);
    std::optional<UpdateResult> r = std::move(result_);
    result_.reset();
    return r;
}

}  // namespace dp
