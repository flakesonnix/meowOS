#include <meow/format/version.hpp>
#include <meow/error/error.hpp>
#include <sstream>

namespace meow::format {

void requireVersion(const std::string& name, int found, int expected) {
    if (found != expected) {
        std::ostringstream msg;
        msg << "unsupported " << name << " format\n"
            << "  format: " << name << "\n"
            << "  found: " << found << "\n"
            << "  supported: " << expected;
        throw error::MeowError(error::ErrorCode::InvalidManifest, msg.str());
    }
}

}
