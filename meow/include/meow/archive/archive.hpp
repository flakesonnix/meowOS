#ifndef MEOWOS_ARCHIVE_H
#define MEOWOS_ARCHIVE_H

#include <filesystem>

namespace meow::archive {
    struct Archive {
        std::filesystem::path path;
    };

    Archive openArchive(const std::filesystem::path& path);
}

#endif //MEOWOS_ARCHIVE_H
