//
// Created by lucy on 17.07.26.
//

#ifndef MEOWOS_ARCHIVE_H
#define MEOWOS_ARCHIVE_H
#include <filesystem>


namespace meow::archive {
    struct Archive {
        std::filesystem::path path1;
    };

    Archive OpenArchive(const std::filesystem::path &path) {};
}

#endif //MEOWOS_ARCHIVE_H
