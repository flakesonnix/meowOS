#include <meow/repository/index.hpp>
#include <meow/download/downloader.hpp>
#include <meow/error/error.hpp>
#include <toml++/toml.hpp>

namespace meow::repository {

RepositoryIndex loadIndex(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw error::MeowError(
            error::ErrorCode::FileNotFound,
            "repository index not found: " + path.string()
        );
    }

    auto tbl = toml::parse_file(path.string());

    RepositoryIndex index;
    index.schema = tbl["schema"].value_or(1);
    index.generated = tbl["generated"].value_or("");

    auto* packagesArr = tbl["package"].as_array();
    if (!packagesArr) return index;

    for (const auto& elem : *packagesArr) {
        auto* pkgTbl = elem.as_table();
        if (!pkgTbl) continue;

        IndexEntry entry;
        entry.name = types::PackageName{pkgTbl->at("name").value_or("")};
        entry.description = types::Description{pkgTbl->at("description").value_or("")};

        if (auto* verArr = (*pkgTbl)["versions"].as_array()) {
            for (const auto& v : *verArr) {
                if (auto ver = v.value<std::string>()) {
                    entry.versions.push_back(types::PackageVersion{*ver});
                }
            }
        }

        index.packages.push_back(std::move(entry));
    }

    return index;
}

RepositoryIndex fetchRepositoryIndex(const std::string& url) {
    auto tmpDir = std::filesystem::temp_directory_path() / "meow";
    std::filesystem::create_directories(tmpDir);
    auto dest = tmpDir / "index.toml";

    download::downloadFile(url, dest);
    return loadIndex(dest);
}

const IndexEntry* findIndexEntry(const RepositoryIndex& index, const types::PackageName& name) {
    for (const auto& entry : index.packages) {
        if (entry.name.value == name.value) {
            return &entry;
        }
    }
    return nullptr;
}

}
