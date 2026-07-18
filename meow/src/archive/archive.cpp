#include <meow/archive/archive.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <cstdio>

namespace meow::archive {
    Archive openArchive(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            std::fprintf(stderr, "OpenArchive: %s does not exist\n", path.c_str());
            return {};
        }

        struct archive *a = archive_read_new();
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);

        if (archive_read_open_filename(a, path.c_str(), 10240) != ARCHIVE_OK) {
            std::fprintf(stderr, "OpenArchive: cannot open %s: %s\n",
                         path.c_str(), archive_error_string(a));
            archive_read_free(a);
            return {};
        }

        archive_read_free(a);
        return Archive{path};
    }
}
