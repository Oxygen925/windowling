#pragma once
#include <format>
#include <string>

namespace dp::log {

// directory 아래 <실행 파일 이름>_<시각>_<pid>.log 에 기록. OutputDebugString에도 출력.
void Init(const std::wstring& directory);
void Shutdown();
void Write(const char* level, const std::string& message);

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write("INFO", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write("WARN", std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write("ERROR", std::format(fmt, std::forward<Args>(args)...));
}

std::string HrToString(long hr);

}  // namespace dp::log
