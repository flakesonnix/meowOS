#ifndef MEOWOS_REPOSITORY_INDEX_H
#define MEOWOS_REPOSITORY_INDEX_H

#include <filesystem>
#include <string>
#include <vector>

#include <meow/types/types.hpp>

namespace meow::repository {

struct IndexEntry {
    types::PackageName name;
    types::Description description;
    std::vector<types::PackageVersion> versions;
};

struct RepositoryIndex {
    int schema;
    std::string generated;
    std::vector<IndexEntry> packages;
};

RepositoryIndex loadIndex(const std::filesystem::path& path);
RepositoryIndex fetchRepositoryIndex(const std::string& url);
const IndexEntry* findIndexEntry(const RepositoryIndex& index, const types::PackageName& name);

}

#endif
