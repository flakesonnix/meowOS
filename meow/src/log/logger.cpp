#include <meow/log/logger.hpp>
#include <iostream>

namespace meow::log {

namespace {
    LogLevel g_minLevel = LogLevel::Warning;
}

void setLevel(LogLevel level) {
    g_minLevel = level;
}

LogLevel getLevel() {
    return g_minLevel;
}

void log(LogLevel level, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(g_minLevel)) return;
    switch (level) {
        case LogLevel::Debug:
            std::cerr << "[DEBUG] " << message << "\n";
            break;
        case LogLevel::Info:
            std::cerr << "[INFO] " << message << "\n";
            break;
        case LogLevel::Warning:
            std::cerr << "[WARN] " << message << "\n";
            break;
        case LogLevel::Error:
            std::cerr << "[ERROR] " << message << "\n";
            break;
    }
}

}
