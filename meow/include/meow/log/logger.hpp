#ifndef MEOWOS_LOGGER_H
#define MEOWOS_LOGGER_H

#include <string>

namespace meow::log {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

void log(LogLevel level, const std::string& message);

// Minimum level that is emitted. Defaults to Debug (everything printed).
void setLevel(LogLevel level);
LogLevel getLevel();

}

#endif
