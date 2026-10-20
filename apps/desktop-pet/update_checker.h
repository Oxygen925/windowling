#pragma once
// 업데이트 확인 (P6-03): 별도 스레드에서 latest.json 을 받아 검사하고, 끝나면 notifyMsg 를 보낸다 (lParam 없음 —
// 결과는 Take() 로 가져간다. 다른 프로세스가 같은 메시지를 보내도 결과가 없으면 아무 일도 없다).
// 리디렉션은 core::IsAllowedUpdateRedirect 를 통과할 때만 최대 3번 따라간다. 본문 64KB 상한.
#include <windows.h>

#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "core/release.h"

namespace dp {

struct UpdateResult {
    bool ok = false;
    std::string code;          // 실패 사유 (로그용: http_404, redirect_denied, invalid: … )
    core::ReleaseInfo info;
};

class UpdateChecker {
public:
    ~UpdateChecker();
    // 이미 확인 중이면 false
    bool Start(HWND notifyWnd, UINT notifyMsg, std::string url, std::string repo);
    std::optional<UpdateResult> Take();

private:
    std::thread thread_;
    std::mutex mu_;
    std::optional<UpdateResult> result_;
    bool busy_ = false;
};

}  // namespace dp
