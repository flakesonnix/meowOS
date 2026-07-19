#ifndef MEOWOS_KEYSTORE_H
#define MEOWOS_KEYSTORE_H

#include <filesystem>
#include <string>
#include <vector>

namespace meow::crypto {

struct TrustedKey {
    std::string id;
    std::filesystem::path path;
};

std::filesystem::path keysDir();
TrustedKey loadTrustedKey(const std::string& id);
std::vector<TrustedKey> listTrustedKeys();
void addTrustedKey(const std::filesystem::path& srcPath);

}

#endif
