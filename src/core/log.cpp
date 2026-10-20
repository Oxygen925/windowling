#include "core/log.h"

namespace dp::core {

namespace {
LogSink& Sink() {
    static LogSink sink;
    return sink;
}
}  // namespace

void SetLogSink(LogSink sink) { Sink() = std::move(sink); }

void Log(LogLevel level, const std::string& message) {
    if (auto& s = Sink()) s(level, message);
}

}  // namespace dp::core
