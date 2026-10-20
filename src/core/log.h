#pragma once
#include <format>
#include <functional>
#include <string>

namespace dp::core {

// Core 는 파일·OutputDebugString 을 모른다. 앱이 시작할 때 출력 함수를 연결한다.
enum class LogLevel { Info, Warn, Error };
using LogSink = std::function<void(LogLevel, const std::string&)>;

void SetLogSink(LogSink sink);
void Log(LogLevel level, const std::string& message);

template <class... Args>
void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
    Log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void LogWarn(std::format_string<Args...> fmt, Args&&... args) {
    Log(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace dp::core
