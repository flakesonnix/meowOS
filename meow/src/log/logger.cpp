#include <meow/log/logger.hpp>
#include <iostream>

namespace meow::log {

void log(LogLevel level, const std::string& message) {
    switch (level) {
        case LogLevel::Debug:
            std::cout << "[DEBUG] " << message << "\n";
            break;
        case LogLevel::Info:
            std::cout << "[INFO] " << message << "\n";
            break;
        case LogLevel::Warning:
            std::cout << "[WARN] " << message << "\n";
            break;
        case LogLevel::Error:
            std::cout << "[ERROR] " << message << "\n";
            break;
    }
}

}
