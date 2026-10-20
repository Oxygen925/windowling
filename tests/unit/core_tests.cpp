#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/config.h"
#include "core/event_bus.h"
#include "core/scheduler.h"

using namespace dp::core;

namespace {
struct Ping {
    int value;
};
struct Pong {};
}  // namespace

TEST_CASE("EventBus publish and unsubscribe", "[event_bus]") {
    EventBus bus;
    int sum = 0;
    auto sub = bus.Subscribe<Ping>([&](const Ping& p) { sum += p.value; });
    bus.Publish(Ping{2});
    bus.Publish(Pong{});  // 다른 타입은 영향 없음
    CHECK(sum == 2);

    SECTION("토큰 해제 후에는 호출되지 않는다") {
        sub.Reset();
        bus.Publish(Ping{5});
        CHECK(sum == 2);
        CHECK(bus.SubscriberCount<Ping>() == 0);
    }
    SECTION("발행 중 구독 해제해도 안전하다") {
        EventBus::Subscription second;
        second = bus.Subscribe<Ping>([&](const Ping&) { sub.Reset(); });
        bus.Publish(Ping{1});
        bus.Publish(Ping{1});
        CHECK(sum <= 4);
    }
}

TEST_CASE("FixedStepClock steps and resets", "[scheduler]") {
    FixedStepClock clock(60.0, 0.1);
    CHECK(clock.Advance(10.0) == 0);  // 첫 호출은 기준점만 잡는다
    CHECK(clock.Advance(10.0 + 1.0 / 60.0) == 1);
    CHECK(clock.Advance(10.0 + 3.0 / 60.0) == 2);

    SECTION("긴 정지 후에는 최대 누적 시간만큼만 진행 (폭주 방지)") {
        CHECK(clock.Advance(20.0) == 6);  // 0.1초 / (1/60) = 6
    }
    SECTION("Reset 은 누적 시간을 버린다 (세션 복귀)") {
        clock.Reset(30.0);
        CHECK(clock.Advance(30.0 + 0.5 / 60.0) == 0);
    }
}

TEST_CASE("Deadlines next and pop", "[scheduler]") {
    Deadlines d;
    CHECK_FALSE(d.Next().has_value());  // 마감 없음 = 이벤트 기반 대기
    d.Set(1, 5.0);
    d.Set(2, 3.0);
    CHECK(*d.Next() == 3.0);
    CHECK_FALSE(d.PopDue(1, 4.0));
    CHECK(d.PopDue(2, 4.0));
    CHECK(*d.Next() == 5.0);
}

TEST_CASE("Config parse defaults and validation", "[config]") {
    SECTION("빈 문자열이면 기본값") {
        auto r = ParseConfig("");
        CHECK(r.parsed);
        CHECK(r.config.physicsHz == Catch::Approx(60.0));
        CHECK(r.config.resyncIntervalSec == 30);
        CHECK(r.config.occlusionGraceMs == 250);
        CHECK(r.warnings.empty());
    }
    SECTION("정상 값과 범위 밖 값") {
        auto r = ParseConfig(R"(
[physics]
hz = 30.0
[world]
occlusion_grace_ms = 999999
[fullscreen]
always_show_exe = ["VLC.exe"]
)");
        CHECK(r.config.physicsHz == Catch::Approx(30.0));
        CHECK(r.config.occlusionGraceMs == 250);  // 범위 밖 → 기본값
        REQUIRE(r.config.alwaysShowExe.size() == 1);
        CHECK(r.config.alwaysShowExe[0] == "vlc.exe");  // 소문자 정규화
        CHECK(r.warnings.size() == 1);
    }
    SECTION("문법 오류면 전체 기본값과 경고") {
        auto r = ParseConfig("[physics\nhz = ");
        CHECK_FALSE(r.parsed);
        CHECK_FALSE(r.warnings.empty());
    }
    SECTION("알 수 없는 키는 경고") {
        auto r = ParseConfig("[physics]\nhzz = 60\n");
        CHECK(r.warnings.size() == 1);
    }
    SECTION("직렬화 → 재파싱 왕복") {
        Config c;
        c.scale = 1.5;
        c.excludedSurfaceExe = {"game.exe"};
        auto r = ParseConfig(SerializeConfig(c));
        CHECK(r.parsed);
        CHECK(r.warnings.empty());
        CHECK(r.config.scale == Catch::Approx(1.5));
        CHECK(r.config.excludedSurfaceExe == std::vector<std::string>{"game.exe"});
    }
}

TEST_CASE("Config pets parse and clamp", "[config]") {
    // 기본값: 예제 캐릭터 windowling 1마리
    auto d = ParseConfig("");
    REQUIRE(d.config.pets.size() == 1);
    CHECK(d.config.pets[0].character == "windowling");
    CHECK(d.config.pets[0].count == 1);

    auto r = ParseConfig("[[pets]]\ncharacter = \"Test-Blue\"\ncount = 3\n[[pets]]\ncharacter = \"test-orange\"\ncount = 2\n");
    REQUIRE(r.config.pets.size() == 2);
    CHECK(r.config.pets[0].character == "test-blue");  // 소문자화
    CHECK(r.config.pets[0].count == 3);
    CHECK(r.config.pets[1].count == 2);
    CHECK(r.warnings.empty());

    // 합계 8마리 초과는 잘라낸다
    auto c = ParseConfig("[[pets]]\ncharacter = \"a-b-c\"\ncount = 6\n[[pets]]\ncharacter = \"d-e-f\"\ncount = 5\n");
    REQUIRE(c.config.pets.size() == 2);
    CHECK(c.config.pets[1].count == 2);
    CHECK_FALSE(c.warnings.empty());

    // 잘못된 항목은 무시, 잘못된 형식은 기본값
    auto bad = ParseConfig("[[pets]]\ncount = 2\n[[pets]]\ncharacter = \"x-y-z\"\ncount = 0\n");
    CHECK(bad.config.pets.empty());
    CHECK(bad.warnings.size() == 2);
    auto wrong = ParseConfig("pets = 3\n");
    CHECK(wrong.config.pets.size() == 1);
    CHECK_FALSE(wrong.warnings.empty());

    // 직렬화 왕복
    auto round = ParseConfig(SerializeConfig(r.config));
    CHECK(round.warnings.empty());
    REQUIRE(round.config.pets.size() == 2);
    CHECK(round.config.pets[0].character == "test-blue");
    CHECK(round.config.pets[0].count == 3);
}

TEST_CASE("Config frame rate and hit region", "[config]") {
    auto d = ParseConfig("");
    CHECK(d.config.frameRate == 30);
    CHECK(d.config.hitRegion == "frame");
    auto r = ParseConfig("[display]\nframe_rate = 20\n[input]\nhit_region = \"clip\"\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.frameRate == 20);
    CHECK(r.config.hitRegion == "clip");
    auto bad = ParseConfig("[display]\nframe_rate = 200\n[input]\nhit_region = \"pixel\"\n");
    CHECK(bad.config.frameRate == 30);
    CHECK(bad.config.hitRegion == "frame");
    CHECK(bad.warnings.size() == 2);
    auto round = ParseConfig(SerializeConfig(r.config));
    CHECK(round.warnings.empty());
    CHECK(round.config.frameRate == 20);
    CHECK(round.config.hitRegion == "clip");
}

// UI-07: 설정 창 저장 = 디스크 설정 + 창이 다루는 항목만 덮어쓰기 → 직렬화 → 다시 읽어 같아야 한다
TEST_CASE("Config UI fields round trip and copy", "[config]") {
    Config ui;
    ui.language = "en";
    ui.theme = "dark";
    ui.scale = 1.25;
    ui.frameRate = 60;
    ui.proximityReaction = false;
    ui.updateCheck = false;
    ui.ai.enabled = true;
    ui.ai.hotkey = "Ctrl+Alt+F9";
    ui.ai.historyTurns = 10;
    ui.ai.bubbleSeconds = 4;
    ui.ai.firstTokenTimeoutSec = 30;
    ui.ai.requestTimeoutSec = 90;
    ui.observe.intervalMinMin = 5;
    ui.observe.intervalMaxMin = 9;
    ui.observe.maxPerHour = 4;
    ui.observe.maxPerDay = 20;
    ui.observe.requireActivityMin = 0;
    ui.observe.sendAppName = false;
    ui.observe.excludeExe = {"a.exe", "b.exe"};
    ui.observe.allowedBehaviors = {"sit"};
    ui.tts.enabled = true;
    ui.tts.engine = "http";
    ui.tts.voice = "Heami";
    ui.tts.rate = 1.5;
    ui.tts.volume = 0.5;
    ui.tts.readChatReplies = true;
    ui.tts.httpUrl = "http://127.0.0.1:9000/v1/audio/speech";
    ui.tts.httpModel = "m";
    ui.tts.httpVoice = "v";
    ui.tts.commandPath = "C:\\tts\\say.exe";
    ui.tts.commandArgs = "--out {out} {text}";
    ui.alwaysShowExe = {"show.exe"};
    ui.alwaysHideExe = {"hide.exe"};

    // 디스크 쪽: 창이 다루지 않는 값 (펫 목록·물리·입력)
    auto disk = ParseConfig("[physics]\nhz = 30\n[input]\nhit_region = \"clip\"\n[[pets]]\ncharacter = \"test-blue\"\ncount = 2\n");
    REQUIRE(disk.warnings.empty());
    Config merged = disk.config;
    CopyUiFields(ui, merged);
    CHECK(merged.pets == disk.config.pets);
    CHECK(merged.physicsHz == 30.0);
    CHECK(merged.hitRegion == "clip");
    CHECK(merged.language == "en");
    CHECK(merged.tts.httpModel == "m");
    CHECK(DiffUiFields(ui, merged).empty());
    CHECK(DiffUiFields(Config{}, merged).size() == 33);  // 모든 항목을 바꿨다

    auto round = ParseConfig(SerializeConfig(merged));
    CHECK(round.warnings.empty());
    CHECK(round.config == merged);

    // 기본값도 그대로 돌아온다
    auto defaults = ParseConfig(SerializeConfig(Config{}));
    CHECK(defaults.warnings.empty());
    CHECK(defaults.config == Config{});

    // 범위 밖 값 → section.key 로 시작하는 경고 (설정 창이 항목을 찾는 데 쓴다)
    Config bad = merged;
    bad.observe.maxPerHour = 500;
    auto r = ParseConfig(SerializeConfig(bad));
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].rfind("observe.max_per_hour", 0) == 0);
}

// 보안 점검 (2026-10-20): TTS 명령 종류와 배치 파일 인수 제한
TEST_CASE("TTS command kind and batch argument rules", "[config][security]") {
    CHECK(CommandKindOf("C:\\tts\\say.exe") == CommandKind::Exe);
    CHECK(CommandKindOf("C:\\tts\\SAY.EXE") == CommandKind::Exe);
    CHECK(CommandKindOf("C:\\tts\\run.bat") == CommandKind::Batch);
    CHECK(CommandKindOf("C:\\tts\\run.CMD") == CommandKind::Batch);
    CHECK(CommandKindOf("C:\\tts\\run.ps1") == CommandKind::Unsupported);
    CHECK(CommandKindOf("C:\\tts.d\\run") == CommandKind::Unsupported);
    CHECK(CommandKindOf("say") == CommandKind::Unsupported);
    auto warns = [](const std::string& path, const std::string& args) {
        return ParseConfig("[tts]\nengine = \"command\"\ncommand_path = '" + path + "'\ncommand_args = '" + args + "'\n").warnings;
    };
    CHECK(warns("C:\\tts\\say.exe", "--out {out} {text}").empty());
    CHECK(warns("C:\\tts\\run.bat", "{text_file} {out}").empty());
    const auto bat = warns("C:\\tts\\run.bat", "{out} {text}");
    REQUIRE(bat.size() == 1);
    CHECK(bat[0].rfind("tts.command_args", 0) == 0);
    REQUIRE(warns("C:\\tts\\run.bat", "{out} {voice}").size() == 1);
    const auto ps = warns("C:\\tts\\run.ps1", "{out}");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0].rfind("tts.command_path", 0) == 0);
}

// 보안 점검 (2026-10-20): 루프백 주소 검사 — userinfo·역슬래시로 다른 호스트를 가리키는 주소 거부
TEST_CASE("Loopback URL check parses the authority", "[config][security]") {
    for (const char* ok : {"http://127.0.0.1:7788/v1/audio/speech", "http://localhost:8080/", "http://[::1]:1/a",
                           "http://127.0.0.1/", "http://LOCALHOST:5000/x?y=1", "http://127.0.0.1:65535"})
        CHECK(IsLoopbackUrl(ok));
    for (const char* bad : {"http://127.0.0.1:80@evil.example/v1/audio/speech", "http://localhost:x@evil.example/",
                            "http://[::1]:1@evil.example:8080/a", "http://localhost:7788\\@evil.example/",
                            "http://127.0.0.1.evil.com/", "http://localhost@evil/", "https://127.0.0.1/", "HTTP://127.0.0.1/",
                            "http://127.0.0.1:0/", "http://127.0.0.1:70000/", "http://127.0.0.1:12ab/", "http://[::1/",
                            "http://127.0.0.2/", "http://127.0.0.1 /", "http:///"})
        CHECK_FALSE(IsLoopbackUrl(bad));
}
