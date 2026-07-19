#include <meow/crypto/keystore.hpp>
#include <meow/error/error.hpp>
#include <cstdlib>
#include <fstream>

namespace meow::crypto {

std::filesystem::path keysDir() {
    const char* home = std::getenv("HOME");
    if (!home) throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
    auto dir = std::filesystem::path(home) / ".config" / "meow" / "keys";
    std::filesystem::create_directories(dir);
    return dir;
}

TrustedKey loadTrustedKey(const std::string& id) {
    auto dir = keysDir();
    auto keyPath = dir / (id + ".pem");

    if (!std::filesystem::exists(keyPath)) {
        throw error::MeowError(
            error::ErrorCode::TrustedKeyNotFound,
            "trusted key not found: " + id + "\n  path: " + keyPath.string()
        );
    }

    return TrustedKey{id, keyPath};
}

std::vector<TrustedKey> listTrustedKeys() {
    std::vector<TrustedKey> keys;
    auto dir = keysDir();
    if (!std::filesystem::is_directory(dir)) return keys;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".pem") continue;
        auto id = entry.path().stem().string();
        keys.push_back(TrustedKey{id, entry.path()});
    }
    return keys;
}

void addTrustedKey(const std::filesystem::path& srcPath) {
    if (!std::filesystem::exists(srcPath)) {
        throw error::MeowError(
            error::ErrorCode::FileNotFound,
            "key file not found: " + srcPath.string()
        );
    }

    auto filename = srcPath.filename().string();
    auto dst = keysDir() / filename;

    if (filename.empty()) {
        throw error::MeowError(error::ErrorCode::Internal, "invalid key filename");
    }

    std::filesystem::copy_file(srcPath, dst, std::filesystem::copy_options::overwrite_existing);
}

}
