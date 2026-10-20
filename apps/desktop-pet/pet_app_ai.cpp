// PetApp 의 AI 대화 부분 (P3): 대화창·말풍선·단축키와 AiService 이벤트 처리.
// 대화 원문·토큰은 로그에 남기지 않는다 (P3-R13). 길이·결과·오류 코드만 기록.
#include "pet_app.h"

#include <shellapi.h>

#include <format>

#include "diagnostics/log.h"
#include "platform/windows/strings.h"
#include "ui/text.h"

namespace dp {

namespace {

constexpr int kHotkeyId = 1;
constexpr size_t kBubbleChars = 160;  // P3-R8

// 모델 출력의 \n 을 EDIT 컨트롤용 \r\n 으로
std::wstring ForEdit(const std::string& utf8) {
    std::wstring w = ToWide(utf8), out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) out += L'\r';
        out += w[i];
    }
    return out;
}

std::wstring Describe(const StreamResult& r, int maxChars) {
    switch (r.outcome) {
        case StreamOutcome::Completed: return L"";
        case StreamOutcome::Truncated: return ui::TF("status.truncated", maxChars);
        case StreamOutcome::Cancelled: return ui::T("status.cancelled");
        case StreamOutcome::Incomplete: return ui::T("status.incomplete");
        case StreamOutcome::Interrupted: return ui::T("status.interrupted");
        case StreamOutcome::TimedOut:
            return r.code == "first_token_timeout" ? ui::T("status.first_token_timeout") : ui::T("status.request_timeout");
        case StreamOutcome::Failed: break;
    }
    switch (r.recovery) {
        case ai::Recovery::UsageLimit: return ui::T("status.usage_limit");
        case ai::Recovery::ReSignIn: return ui::T("status.re_sign_in");
        case ai::Recovery::NotEligible:
            return r.code == "plan_usage_disabled" ? ui::T("status.plan_usage_disabled") : ui::T("status.not_eligible");
        case ai::Recovery::RetryBackoff: return ui::T("status.retry_backoff");
        case ai::Recovery::Restricted: return ui::T("status.restricted");
        case ai::Recovery::FixRequest: return ui::TF("status.fix_request", ToWide(r.code));
        case ai::Recovery::ClientConfig: return ui::TF("status.client_config", ToWide(r.code));
        case ai::Recovery::Network: return ui::T("status.network");
        default: return ui::TF("status.failed", ToWide(r.code));
    }
}

std::wstring DescribeSignIn(const AiResult& r, const AccountInfo& a) {
    if (r.ok) return a.planUsage ? ui::T("status.signin_ok") : ui::T("status.signin_no_plan");
    if (r.code == "access_denied") return ui::T("status.signin_denied");
    if (r.code == "port_in_use") return ui::T("status.signin_port");
    if (r.code == "timeout") return ui::T("status.signin_timeout");
    if (r.code == "cancelled") return ui::T("status.signin_cancelled");
    if (r.code == "account_mismatch") return ui::T("status.signin_mismatch");
    if (r.code == "browser_failed") return ui::T("status.signin_browser");
    if (r.recovery == ai::Recovery::Network) return ui::T("status.network");
    return ui::TF("status.signin_failed", ToWide(r.code));
}

}  // namespace

PetActor* PetApp::PetById(int id) {
    for (auto& p : pets_)
        if (p->id == id) return p.get();
    return nullptr;
}

void PetApp::EnableAi(bool on) {
    if (on == aiEnabled_) return;
    if (!on) {
        ObserveInterrupted("ai_disabled");
        StopChat();
        ai_.Stop(2000);
        if (hotkey_) UnregisterHotKey(appWnd_.get(), kHotkeyId);
        hotkey_ = false;
        chat_.Hide();
        for (auto& p : pets_) {
            p->speaking = false;
            if (p->bubble) p->bubble->Hide();
        }
        aiEnabled_ = signingIn_ = modelsRequested_ = false;
        models_.clear();
        modelSlug_.clear();
        log::Info("[ai] disabled");
        return;
    }
    SiwcOptions o;
    o.authDir = configPath_.parent_path() / L"auth";
    o.callbackPort = static_cast<uint16_t>(config_.ai.callbackPort);
    bool test = false;
    if (!options_.aiEndpoint.empty()) {
        // 테스트 전용 재정의: 127.0.0.1 만 허용 (실제 계정 토큰은 issuer 별 파일이라 이 주소로 가지 않는다)
        if (options_.aiEndpoint.rfind("http://127.0.0.1:", 0) != 0 || !core::IsLoopbackUrl(options_.aiEndpoint)) {
            log::Error("[ai] --ai-endpoint 는 http://127.0.0.1:<port> 만 허용 — AI 를 켜지 않음");
            return;
        }
        o.endpoints = ai::SiwcEndpoints::ForBase(options_.aiEndpoint);
        test = true;
        if (!options_.aiOpenUrlFile.empty()) o.openUrlFile = options_.aiOpenUrlFile;
    } else if (!options_.aiOpenUrlFile.empty()) {
        log::Warn("[ai] --ai-open-url-file 은 --ai-endpoint 와 함께만 쓸 수 있음 — 무시");
    }
    if (!chat_.Hwnd()) {
        ChatCallbacks cb;
        cb.send = [this](const std::wstring& t) { SendChat(t); };
        cb.stop = [this] { StopChat(); };
        cb.signIn = [this] {
            signingIn_ = true;
            UpdateChatAccount();
            chat_.SetStatus(ui::T("status.signin_progress"));
            ai_.SignIn();
        };
        cb.cancelSignIn = [this] { ai_.CancelSignIn(); };
        cb.signOut = [this] {
            chat_.SetStatus(ui::T("status.signing_out"));
            ai_.SignOut(false);
        };
        cb.signOutLocal = [this] { ai_.SignOut(true); };
        cb.gotIt = [this] {
            chat_.ShowWelcome(false);
            account_.welcomeShown = true;
            ai_.MarkWelcomeShown();
        };
        cb.manageUsage = [] { ShellExecuteW(nullptr, L"open", L"https://chatgpt.com/settings/usage", nullptr, nullptr, SW_SHOWNORMAL); };
        cb.modelChanged = [this](int i) {
            if (i >= 0 && i < static_cast<int>(models_.size())) {
                modelSlug_ = models_[i].slug;
                log::Info("[ai] model selected index={}", i);
            }
        };
        if (!chat_.Create(instance_, std::move(cb))) log::Error("[ai] 대화창 생성 실패");
        UpdateObserveIndicators();
    }
    ai_.Start(appWnd_.get(), WM_AI_EVENT, o);
    aiEnabled_ = true;
    RegisterAiHotkey();
    log::Info("[ai] enabled endpoint={} hotkey={}", test ? "loopback-test" : "production", hotkey_ ? "on" : "off");
}

// 설정 창에서 단축키를 바꿨을 때 (AI 가 켜져 있을 때만 등록되어 있다)
void PetApp::ReRegisterAiHotkey() {
    if (!aiEnabled_) return;
    if (hotkey_) UnregisterHotKey(appWnd_.get(), kHotkeyId);
    hotkey_ = false;
    RegisterAiHotkey();
    log::Info("[ai] hotkey re-registered ok={}", hotkey_);
}

void PetApp::RegisterAiHotkey() {
    const auto hk = core::ParseHotkey(config_.ai.hotkey);
    if (!hk) return;
    UINT mods = MOD_NOREPEAT;
    if (hk->ctrl) mods |= MOD_CONTROL;
    if (hk->alt) mods |= MOD_ALT;
    if (hk->shift) mods |= MOD_SHIFT;
    if (hk->win) mods |= MOD_WIN;
    const UINT vk = hk->fkey ? VK_F1 + hk->fkey - 1 : static_cast<UINT>(hk->key);
    hotkey_ = RegisterHotKey(appWnd_.get(), kHotkeyId, mods, vk) != 0;
    if (!hotkey_)
        log::Warn("[ai] 단축키 {} 등록 실패 err={} (다른 앱이 쓰는 중일 수 있음, [ai] hotkey 로 변경)", config_.ai.hotkey,
                  GetLastError());
}

void PetApp::UpdateChatAccount() {
    chat_.SetAccount(account_.state == AccountState::SignedIn, account_.planUsage, signingIn_, ToWide(account_.email));
    chat_.ShowWelcome(account_.state == AccountState::SignedIn && account_.planUsage && !account_.welcomeShown);
}

void PetApp::RenderChatHistory() {
    PetActor* p = PetById(chatPetId_);
    if (!p) {
        chat_.SetHistory(L"");
        return;
    }
    const std::wstring name = ToWide(p->asset->pkg.name);
    std::wstring text;
    for (const auto& t : p->conv.History())
        text += (t.role == "user" ? ui::T("chat.you") + L": " : name + L": ") + ForEdit(t.text) +
                (t.role == "user" ? L"\r\n" : L"\r\n\r\n");
    if (p->conv.InTurn()) text += name + L": " + ForEdit(p->conv.Reply());
    chat_.SetHistory(text);
}

void PetApp::OpenChat(PetActor* pet) {
    if (!aiEnabled_ || !pet) return;
    chatPetId_ = pet->id;
    lastPetId_ = pet->id;
    chat_.SetTarget(ToWide(pet->asset->pkg.name));
    RenderChatHistory();
    UpdateChatAccount();
    chat_.SetBusy(activeReq_ != 0 && activePetId_ == pet->id);
    chat_.Show(true);
    if (account_.state == AccountState::SignedIn && account_.planUsage && models_.empty() && !modelsRequested_) {
        modelsRequested_ = true;
        ai_.RefreshModels();
    }
    log::Info("[ai] chat open pet={} foreground={}", pet->id, GetForegroundWindow() == chat_.Hwnd());
}

void PetApp::SendChat(const std::wstring& text) {
    PetActor* p = PetById(chatPetId_);
    if (!p) return chat_.SetStatus(ui::T("status.no_pet"));
    if (activeReq_) return;
    if (account_.state != AccountState::SignedIn) return chat_.SetStatus(ui::T("status.sign_in_first"));
    if (!account_.planUsage) {
        StreamResult r;
        r.recovery = ai::Recovery::NotEligible;
        r.code = "plan_usage_disabled";
        return chat_.SetStatus(Describe(r, config_.ai.maxReplyChars));
    }
    if (modelSlug_.empty()) {
        chat_.SetStatus(ui::T("status.models_loading"));
        if (!modelsRequested_) {
            modelsRequested_ = true;
            ai_.RefreshModels();
        }
        return;
    }
    p->conv.SetMaxTurns(config_.ai.historyTurns);
    const std::string utf8 = ai::Utf8Sanitize(ToUtf8(text));
    const auto turns = p->conv.BeginTurn(utf8);
    const auto& pkg = p->asset->pkg;
    if (observeReq_) ObserveInterrupted("chat");  // 대화가 우선 (단일 요청 슬롯)
    ai::AiRequest request;
    request.model = modelSlug_;
    request.instructions = ai::BuildInstructions(pkg.name, pkg.persona);
    request.turns = turns;
    activeReq_ = ai_.Send(p->id, std::move(request), static_cast<size_t>(config_.ai.maxReplyChars),
                          StreamTimeouts{config_.ai.firstTokenTimeoutSec, config_.ai.requestTimeoutSec});
    activePetId_ = p->id;
    p->speaking = true;
    if (!p->bubble) p->bubble = std::make_unique<SpeechBubble>();
    p->bubble->SetText(L"…");
    chat_.AppendHistory(ui::T("chat.you") + L": " + text + L"\r\n" + ToWide(pkg.name) + L": ");
    chat_.SetBusy(true);
    chat_.SetStatus(ui::T("status.receiving"));
    log::Info("[ai] send pet={} request={} turns={} chars={}", p->id, activeReq_, turns.size(), ai::Utf8Length(utf8));
}

void PetApp::StopChat() {
    if (activeReq_) {
        log::Info("[ai] stop request={}", activeReq_);
        ai_.Cancel(activeReq_);  // StreamDone(cancelled) 이 오면 정리
    }
}

void PetApp::OnAiEvent(std::unique_ptr<AiEvent> e) {
    if (!aiEnabled_) return;  // 끈 뒤 늦게 도착한 이벤트
    const double now = Now();
    switch (e->kind) {
        case AiEvent::Kind::Account:
            account_ = e->account;
            UpdateChatAccount();
            break;
        case AiEvent::Kind::SignInDone:
            signingIn_ = false;
            account_ = e->account;
            UpdateChatAccount();
            chat_.SetStatus(DescribeSignIn(e->result, account_));
            log::Info("[ai] ui signin ok={} code={} plan_usage={}", e->result.ok, e->result.code, account_.planUsage);
            if (e->result.ok && account_.planUsage) {
                models_.clear();
                modelsRequested_ = true;
                ai_.RefreshModels();
            }
            break;
        case AiEvent::Kind::Models: {
            modelsRequested_ = false;
            account_ = e->account;
            if (!e->result.ok) {
                StreamResult r;
                r.recovery = e->result.recovery;
                r.code = e->result.code;
                chat_.SetStatus(ui::TF("status.models_failed", Describe(r, config_.ai.maxReplyChars)));
                UpdateChatAccount();
                break;
            }
            models_ = e->models;
            int sel = models_.empty() ? -1 : 0;
            for (size_t i = 0; i < models_.size(); ++i)
                if (models_[i].slug == config_.ai.model) sel = static_cast<int>(i);
            modelSlug_ = sel >= 0 ? models_[sel].slug : "";
            chat_.SetModels(models_, sel);
            if (models_.empty()) chat_.SetStatus(ui::T("status.no_models"));
            else chat_.SetStatus(L"");
            log::Info("[ai] ui models count={} selected={}", models_.size(), sel);
            break;
        }
        case AiEvent::Kind::Delta: {
            if (e->requestId != activeReq_) break;
            PetActor* p = PetById(e->petId);
            if (!p) break;
            p->conv.AppendReply(e->text);
            if (p->bubble) p->bubble->SetText(ToWide(ai::Utf8Tail(p->conv.Reply(), kBubbleChars)));
            if (chatPetId_ == p->id) chat_.AppendHistory(ForEdit(e->text));
            break;
        }
        case AiEvent::Kind::StreamDone: {
            if (e->requestId != activeReq_) break;
            activeReq_ = 0;
            activePetId_ = -1;
            const auto& r = e->stream;
            account_ = e->account;
            PetActor* p = PetById(e->petId);
            size_t replyChars = 0;
            if (p) {
                replyChars = ai::Utf8Length(p->conv.Reply());
                const bool hadReply = replyChars > 0;
                if (hadReply && config_.tts.readChatReplies) SpeakTts(*p, p->conv.Reply());
                p->conv.EndTurn(true);  // 일부라도 받은 응답은 이력에 남긴다 (빈 응답이면 질문도 제거)
                p->speaking = false;
                if (p->bubble) {
                    if (hadReply) p->bubble->hideAt = now + config_.ai.bubbleSeconds;
                    else p->bubble->Hide();
                }
                if (chatPetId_ == p->id) {
                    const std::wstring note = Describe(r, config_.ai.maxReplyChars);
                    chat_.AppendHistory(hadReply ? (note.empty() ? L"\r\n\r\n" : L"\r\n[" + note + L"]\r\n\r\n")
                                                 : ui::T("chat.no_reply") + L"\r\n\r\n");
                }
            }
            chat_.SetBusy(false);
            chat_.SetStatus(Describe(r, config_.ai.maxReplyChars));
            UpdateChatAccount();
            log::Info("[ai] stream done pet={} request={} outcome={} chars={} attempts={} http={} code={} recovery={} "
                      "first_delta={:.2f}s request_id={} bubble_draws={}",
                      e->petId, e->requestId, ToString(r.outcome), replyChars, r.attempts, r.http, r.code,
                      ai::ToString(r.recovery), r.firstDeltaSec, r.requestId, SpeechBubble::DrawCount());
            break;
        }
        case AiEvent::Kind::Diagnose:
            account_ = e->account;
            UpdateChatAccount();
            chat_.SetStatus(e->result.ok ? ui::TF("status.diag_ok", e->models.size())
                                         : ui::TF("status.diag_failed", ToWide(e->result.code.empty() ? "unknown" : e->result.code)));
            log::Info("[ai] ui diag ok={} code={} models={}", e->result.ok, e->result.code, e->models.size());
            break;
        case AiEvent::Kind::ObserveCaptured:
        case AiEvent::Kind::ObserveDone:
            OnObserveEvent(std::move(e));
            break;
        case AiEvent::Kind::SignOutDone:
            account_ = e->account;
            if (e->result.ok) {
                models_.clear();
                modelSlug_.clear();
                chat_.SetModels(models_, -1);
                chat_.ShowLocalSignOut(false);
                chat_.SetStatus(e->result.code == "revoke_unconfirmed" ? ui::T("status.signed_out_unconfirmed")
                                                                       : ui::T("status.signed_out"));
            } else {
                chat_.ShowLocalSignOut(true);
                chat_.SetStatus(ui::T("status.signout_failed"));
            }
            UpdateChatAccount();
            log::Info("[ai] ui signout ok={} code={}", e->result.ok, e->result.code);
            if (wipePending_) FinishWipe();  // 모든 데이터 지우기: 로그아웃(토큰 폐기) 뒤 계속
            break;
    }
}

}  // namespace dp
