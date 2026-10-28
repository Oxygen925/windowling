cmake_minimum_required(VERSION 3.25)
# 버전은 여기 한 곳 (실행 파일 VERSIONINFO·앱 표시·릴리스 latest.json, P6)
project(desktop_pet VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
  message(FATAL_ERROR "MSVC 전용")
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)

# ---- 버전 헤더 (생성) ----
# 업데이트 확인할 GitHub 저장소 ("소유자/저장소"). 비우면 확인하지 않는다 (릴리스 CI 가 지정, P6-03)
set(DP_RELEASE_REPO "" CACHE STRING "업데이트 확인 저장소 (소유자/저장소)")
# 실행 채널: 개발 빌드는 데이터 폴더·창 클래스·mutex·시작 등록 이름을 따로 써서 배포판과 섞이지 않는다. 배포 빌드(x64-dist)는 OFF
option(DP_DEV_CHANNEL "개발 채널 (DesktopPet-Dev 데이터 폴더)" ON)
configure_file(${CMAKE_SOURCE_DIR}/src/dp_version.h.in ${CMAKE_BINARY_DIR}/generated/dp_version.h @ONLY)

# 개발·측정 도구 (p0-spike·mock-siwc·ui-gallery·frame-grab·p0-counters). 배포 빌드(x64-dist)는 OFF
option(DP_BUILD_TOOLS "개발·측정 도구 빌드" ON)

# ---- 외부 의존성 (vcpkg manifest, P1-D3) ----
find_package(tomlplusplus CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- HLSL (fxc, 빌드 타임 컴파일 → 바이트코드 헤더) ----
file(GLOB _sdk_bins "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*/x64")
list(SORT _sdk_bins ORDER DESCENDING)
find_program(FXC_EXECUTABLE fxc HINTS ${_sdk_bins} REQUIRED)

set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/generated/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

function(add_shader out_list src entry profile var)
  set(out ${SHADER_OUT_DIR}/${var}.h)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${FXC_EXECUTABLE} /nologo /T ${profile} /E ${entry} /O3 /Vn ${var} /Fh ${out} ${CMAKE_SOURCE_DIR}/${src}
    DEPENDS ${CMAKE_SOURCE_DIR}/${src}
    COMMENT "fxc ${src}:${entry}"
    VERBATIM)
  set(${out_list} ${${out_list}} ${out} PARENT_SCOPE)
endfunction()

set(RENDER_SHADERS)
add_shader(RENDER_SHADERS src/rendering/d3d11/shaders/sprite.hlsl VSMain vs_5_0 g_SpriteVS)
add_shader(RENDER_SHADERS src/rendering/d3d11/shaders/sprite.hlsl PSMain ps_5_0 g_SpritePS)
add_shader(RENDER_SHADERS src/rendering/d3d11/shaders/model.hlsl VSMain vs_5_0 g_ModelVS)
add_shader(RENDER_SHADERS src/rendering/d3d11/shaders/model.hlsl PSMain ps_5_0 g_ModelPS)
add_custom_target(dp_render_shaders DEPENDS ${RENDER_SHADERS})

set(SPIKE_SHADERS)
add_shader(SPIKE_SHADERS apps/p0-spike/shaders/cube.hlsl VSMain vs_5_0 g_CubeVS)
add_shader(SPIKE_SHADERS apps/p0-spike/shaders/cube.hlsl PSMain ps_5_0 g_CubePS)
add_custom_target(dp_spike_shaders DEPENDS ${SPIKE_SHADERS})

# ---- 미리 컴파일된 헤더 (조사 B2): Windows 쪽 타깃만. dp_core·dp_ai·dp_script 는 windows.h 금지 계층이라 제외 ----
set(DP_WIN_PCH
  <windows.h> <d3d11_1.h> <dxgi1_2.h> <dcomp.h> <d2d1_1.h> <dwrite.h> <wrl/client.h> <wil/resource.h>
  <algorithm> <array> <cstdint> <filesystem> <format> <functional> <map> <memory> <mutex> <optional>
  <string> <string_view> <unordered_map> <vector>)

# ---- dp_core: Windows 비의존 (기준선 §3, P1 계획 §3.8) ----
add_library(dp_core STATIC
  src/core/geometry.cpp
  src/core/event_bus.cpp
  src/core/scheduler.cpp
  src/core/config.cpp
  src/core/i18n.cpp
  src/core/log.cpp
  src/core/world.cpp
  src/core/physics.cpp
  src/core/character/package.cpp
  src/core/character/zip_index.cpp
  src/core/release.cpp
  src/core/character/animation.cpp
  src/core/behavior/fsm.cpp
  src/core/behavior/social.cpp
)
target_include_directories(dp_core PUBLIC src)
target_link_libraries(dp_core PUBLIC tomlplusplus::tomlplusplus PRIVATE nlohmann_json::nlohmann_json)
# <windows.h> 를 빈 헤더로 만들어 dp_core 안의 Win32 타입 사용을 컴파일 오류로 막는다
target_compile_options(dp_core PRIVATE "/FI${CMAKE_SOURCE_DIR}/src/core/detail/forbid_windows.h")

# ---- dp_script: Lua 행동 스크립트 샌드박스 (Windows 비의존, P2-R2~R4) ----
add_library(dp_script STATIC
  src/script/lua_behavior.cpp
)
target_include_directories(dp_script PUBLIC src PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(dp_script PUBLIC dp_core PRIVATE ${LUA_LIBRARIES})
target_compile_options(dp_script PRIVATE "/FI${CMAKE_SOURCE_DIR}/src/core/detail/forbid_windows.h")

# ---- dp_ai: SIWC 프로토콜·Responses 스트림·대화 (Windows 비의존, P3-01) ----
add_library(dp_ai STATIC
  src/ai/siwc_protocol.cpp
  src/ai/responses.cpp
  src/ai/observe.cpp
  src/ai/pet_chat.cpp
)
target_include_directories(dp_ai PUBLIC src)
target_link_libraries(dp_ai PUBLIC nlohmann_json::nlohmann_json)
target_compile_options(dp_ai PRIVATE "/FI${CMAKE_SOURCE_DIR}/src/core/detail/forbid_windows.h")

# ---- dp_model: glTF(.glb) 로드·애니메이션 포즈 (Windows·GPU 비의존, P5) ----
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
add_library(dp_model STATIC
  src/model/gltf_model.cpp
  src/model/cgltf_impl.cpp
)
target_include_directories(dp_model PUBLIC src PRIVATE ${CGLTF_INCLUDE_DIR})
target_compile_options(dp_model PRIVATE "/FI${CMAKE_SOURCE_DIR}/src/core/detail/forbid_windows.h")

# ---- dp_diagnostics:로그·메트릭 ----
add_library(dp_diagnostics STATIC
  src/diagnostics/log.cpp
  src/diagnostics/metrics.cpp
)
target_include_directories(dp_diagnostics PUBLIC src)
target_link_libraries(dp_diagnostics PUBLIC dxgi)

# ---- dp_platform_win: Win32 어댑터 ----
add_library(dp_platform_win STATIC
  src/platform/windows/strings.cpp
  src/platform/windows/tray_icon.cpp
  src/platform/windows/sprite_sheet.cpp
  src/platform/windows/window_observer.cpp
  src/platform/windows/fullscreen_detector.cpp
  src/platform/windows/observe_signals.cpp
)
target_include_directories(dp_platform_win PUBLIC src)
target_link_libraries(dp_platform_win PUBLIC dp_core dp_diagnostics WIL::WIL
  windowscodecs dwmapi shcore user32 gdi32 shell32 ole32 advapi32 wtsapi32)

# ---- dp_ai_win: WinHTTP·루프백·BCrypt·DPAPI·SIWC 클라이언트·AI 작업 스레드 (P3-02, P3-03) ----
add_library(dp_ai_win STATIC
  src/platform/windows/ai/win_crypto.cpp
  src/platform/windows/ai/http_client.cpp
  src/platform/windows/ai/mini_http.cpp
  src/platform/windows/ai/siwc_client.cpp
  src/platform/windows/ai/ai_service.cpp
)
target_link_libraries(dp_ai_win PUBLIC dp_ai dp_platform_win dp_diagnostics winhttp bcrypt crypt32 ws2_32)

# ---- dp_media_win: 화면 캡처(WGC 1프레임)·로컬 TTS(WinRT SpeechSynthesizer + XAudio2) (P4) ----
# C++/WinRT 헤더는 Windows SDK 에 들어 있다 (vcpkg 의존성 없음). PCH 는 쓰지 않는다 (WinRT 헤더 분리)
add_library(dp_media_win STATIC
  src/platform/windows/capture/screen_capture.cpp
  src/platform/windows/tts/tts.cpp
)
target_include_directories(dp_media_win PUBLIC src)
target_link_libraries(dp_media_win PUBLIC dp_ai_win dp_platform_win dp_ai dp_diagnostics d3d11 dxgi windowscodecs windowsapp xaudio2)

# ---- dp_render: D3D11 / DComp / D2D ----
add_library(dp_render STATIC
  src/rendering/hit_mask.cpp
  src/rendering/d3d11/gpu_context.cpp
  src/rendering/d3d11/composition_target.cpp
  src/rendering/d3d11/sprite_renderer.cpp
  src/rendering/d3d11/text_renderer.cpp
  src/rendering/d3d11/bubble_renderer.cpp
  src/rendering/d3d11/model_renderer.cpp
)
add_dependencies(dp_render dp_render_shaders)
target_include_directories(dp_render PUBLIC src PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(dp_render PUBLIC dp_platform_win dp_model WIL::WIL d3d11 dxgi dcomp d2d1 dwrite)

# ---- 정식 앱 (P1·P2) ----
add_executable(desktop-pet WIN32
  apps/desktop-pet/main.cpp
  apps/desktop-pet/pet_app.cpp
  apps/desktop-pet/pet_app_ai.cpp
  apps/desktop-pet/pet_app_observe.cpp
  apps/desktop-pet/pet_app_behavior.cpp
  apps/desktop-pet/pet_app_social.cpp
  apps/desktop-pet/pet_app_settings.cpp
  apps/desktop-pet/pet_app_characters.cpp
  apps/desktop-pet/pet_app_model.cpp
  apps/desktop-pet/ui/ui_common.cpp
  apps/desktop-pet/ui/app_window.cpp
  apps/desktop-pet/ui/text.cpp
  apps/desktop-pet/chat_window.cpp
  apps/desktop-pet/consent_window.cpp
  apps/desktop-pet/settings_window.cpp
  apps/desktop-pet/characters_window.cpp
  apps/desktop-pet/welcome_window.cpp
  apps/desktop-pet/update_checker.cpp
  apps/desktop-pet/character_install.cpp
  apps/desktop-pet/startup_run.cpp
  apps/desktop-pet/bubble.cpp
  apps/desktop-pet/desktop-pet.manifest
  apps/desktop-pet/app.rc
)
target_include_directories(desktop-pet PRIVATE apps/desktop-pet)
target_link_libraries(desktop-pet PRIVATE dp_render dp_ai_win dp_media_win dp_platform_win dp_script dp_core comctl32 uxtheme msimg32)
# UI 문구 (UI-01): 빌드마다 실행 파일 옆 lang/ 로 복사
add_custom_target(dp_lang ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:desktop-pet>/lang
  VERBATIM)
add_dependencies(dp_lang desktop-pet)
# 매니페스트(desktop-pet.manifest)에 asInvoker 를 직접 적는다. /DEPENDENTLOADFLAG:0x800 = 로더가 main 전에 여는
# 정적 가져오기 DLL 도 System32 에서만 찾는다 (앱 폴더에 놓인 같은 이름 DLL 무시, P6·보안 점검 2026-10-20)
target_link_options(desktop-pet PRIVATE /MANIFESTUAC:NO /DEPENDENTLOADFLAG:0x800)
target_include_directories(desktop-pet PRIVATE ${CMAKE_BINARY_DIR}/generated)
# rc 는 .ico 의존성을 추적하지 않는다 → 아이콘만 바꿔도 리소스를 다시 컴파일하게
set_source_files_properties(apps/desktop-pet/app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/apps/desktop-pet/app.ico)
target_precompile_headers(desktop-pet PRIVATE ${DP_WIN_PCH})
target_precompile_headers(dp_render PRIVATE ${DP_WIN_PCH})
target_precompile_headers(dp_platform_win PRIVATE ${DP_WIN_PCH})
target_precompile_headers(dp_ai_win PRIVATE ${DP_WIN_PCH})
add_custom_command(TARGET desktop-pet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/test $<TARGET_FILE_DIR:desktop-pet>/assets/test
  VERBATIM)
# 테스트 캐릭터(characters/)는 개발용으로만 쓴다. 일반 배포판에는 기본 캐릭터팩을 넣지 않는다 (2026-10-14 결정).
# 배포 패키지(P6)는 이 옵션을 OFF 로 빌드하거나 characters/ 를 제외한다.
# 시험용 명령줄 인자·훅(--ai-endpoint, --observe-dump, --test-hooks, --render-dump …). 배포 빌드(P6)는 OFF (보안 점검 2026-10-20)
option(DP_TEST_HOOKS "시험용 명령줄 인자·훅 포함" ON)
if(DP_TEST_HOOKS)
  target_compile_definitions(desktop-pet PRIVATE DP_TEST_HOOKS=1)
endif()
option(DP_DEV_CHARACTERS "개발용 테스트 캐릭터를 실행 파일 옆 characters/ 로 복사" ON)
if(DP_DEV_CHARACTERS)
  # 빌드마다 복사 (POST_BUILD 는 실행 파일을 다시 링크할 때만 돌아 패키지만 바꾸면 반영되지 않았다)
  add_custom_target(dp_dev_characters ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/characters $<TARGET_FILE_DIR:desktop-pet>/characters
    VERBATIM)
  add_dependencies(dp_dev_characters desktop-pet)
endif()

if(DP_BUILD_TOOLS)
# ---- P0 spike (측정·비교 전용, 기능 추가 없음) ----
add_executable(p0-spike WIN32
  apps/p0-spike/main.cpp
  apps/p0-spike/app.cpp
  apps/p0-spike/options.cpp
  apps/p0-spike/csv_logger.cpp
  apps/p0-spike/cube_renderer.cpp
  apps/p0-spike/layered_blitter.cpp
  apps/p0-spike/win_event_tracker.cpp
  apps/p0-spike/p0-spike.manifest
)
add_dependencies(p0-spike dp_spike_shaders)
target_include_directories(p0-spike PRIVATE apps/p0-spike ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(p0-spike PRIVATE dp_render dp_platform_win dp_diagnostics)
# .manifest 소스는 CMake가 링커 manifest 병합으로 포함한다
target_link_options(p0-spike PRIVATE /MANIFESTUAC:NO)
add_custom_command(TARGET p0-spike POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/test $<TARGET_FILE_DIR:p0-spike>/assets/test
  VERBATIM)

# ---- 모의 SIWC 서버 (P3-04, 시나리오 테스트 전용, 127.0.0.1) ----
add_executable(mock-siwc tools/mock-siwc/main.cpp)
target_link_libraries(mock-siwc PRIVATE dp_ai_win)


# ---- 확인용: UI 컨트롤 밝게/어둡게 렌더 (UI-04) ----
add_executable(ui-gallery WIN32 tools/ui-gallery/main.cpp apps/desktop-pet/ui/ui_common.cpp apps/desktop-pet/desktop-pet.manifest)
target_include_directories(ui-gallery PRIVATE apps/desktop-pet src)
target_link_libraries(ui-gallery PRIVATE comctl32 dwmapi uxtheme windowscodecs ole32)
target_link_options(ui-gallery PRIVATE /MANIFESTUAC:NO /ENTRY:wmainCRTStartup)

# ---- 진단: 화면 사각형 프레임별 검은 픽셀 수 (P5, 개발용) ----
add_executable(frame-grab tools/frame-grab/main.cpp)
target_link_libraries(frame-grab PRIVATE d3d11 dxgi windowscodecs ole32)

# ---- 외부 성능 카운터 수집기 (측정 대상 프로세스와 분리) ----
add_executable(p0-counters tools/p0-counters/main.cpp)
target_link_libraries(p0-counters PRIVATE pdh)
endif()  # DP_BUILD_TOOLS

# ---- 캐릭터 패키지 검증 도구 (A6, 외부 제작자용 — 배포 zip 에도 넣는다) ----
add_executable(pack-check tools/pack-check/main.cpp)
target_link_libraries(pack-check PRIVATE dp_platform_win dp_script dp_core dp_model)
target_link_options(pack-check PRIVATE /DEPENDENTLOADFLAG:0x800)

# ---- 단위 테스트 (Catch2 는 테스트 전용, P1-D3) ----
enable_testing()
add_executable(dp_core_tests
  tests/unit/geometry_tests.cpp
  tests/unit/core_tests.cpp
  tests/unit/world_physics_tests.cpp
  tests/unit/social_tests.cpp
  tests/unit/character_tests.cpp
  tests/unit/lua_tests.cpp
  tests/unit/ai_tests.cpp
  tests/unit/observe_tests.cpp
  tests/unit/model_tests.cpp
  tests/unit/i18n_tests.cpp
  tests/unit/zip_index_tests.cpp
  tests/unit/release_tests.cpp
)
target_link_libraries(dp_core_tests PRIVATE dp_core dp_script dp_ai dp_model Catch2::Catch2WithMain)
# 실제 배포 테스트 캐릭터(characters/)를 검증하는 테스트용
target_compile_definitions(dp_core_tests PRIVATE DP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(Catch)
catch_discover_tests(dp_core_tests)

# 플랫폼 단위 테스트 (BCrypt·DPAPI·루프백·WinHTTP 왕복, P3-02)
add_executable(dp_platform_tests tests/unit/platform_ai_tests.cpp)
target_link_libraries(dp_platform_tests PRIVATE dp_ai_win dp_media_win Catch2::Catch2WithMain)
catch_discover_tests(dp_platform_tests)
