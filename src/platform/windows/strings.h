#pragma once
#include <string>

namespace dp {

// UTF-16(Win32) ↔ UTF-8(로그·설정) 변환
std::string ToUtf8(const std::wstring& w);
std::wstring ToWide(const std::string& s);

}  // namespace dp
