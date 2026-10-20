#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "options.h"
#include "diagnostics/metrics.h"

namespace dp {

// 계획서 §4.4 CSV 형식. '#' 으로 시작하는 헤더 줄에 측정 환경 메타데이터를 기록한다.
class CsvLogger {
public:
    ~CsvLogger() { Close(); }
    bool Open(const std::wstring& path, const std::vector<std::string>& metaLines);
    void WriteRow(const Options& o, int petCount, const MetricsSample& m, bool suspended, const std::string& notes);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }

private:
    FILE* file_ = nullptr;
};

// CPU·GPU·모니터·OS 정보 (CSV 헤더용)
std::vector<std::string> CollectEnvironmentMeta();

}  // namespace dp
