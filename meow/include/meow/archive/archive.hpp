#ifndef MEOWOS_ARCHIVE_H
#define MEOWOS_ARCHIVE_H

#include <filesystem>
#include <meow/types/types.hpp>

namespace meow::archive {
    struct Archive {
        std::filesystem::path path;
    };

    Archive openArchive(const std::filesystem::path& path);
    types::FileList listFiles(const Archive& archive);
    std::string readFile(const Archive& archive, const std::filesystem::path& filename);
    types::FileList extractAll(const Archive& archive, const std::filesystem::path& destination);
    void extractFile(const Archive& archive, const std::filesystem::path& file, const std::filesystem::path& destination);
}

#endif //MEOWOS_ARCHIVE_H
