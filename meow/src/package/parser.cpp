#include <toml++/toml.hpp>
#include <meow/package/parser.hpp>
#include <meow/package/package.hpp>

namespace meow::package {
    static std::vector<types::PackageName> parseDepArray(const toml::table& tbl, const std::string& key) {
        std::vector<types::PackageName> deps;
        if (auto* arr = tbl[key].as_array()) {
            for (auto&& node : *arr) {
                if (auto val = node.value<std::string>()) {
                    deps.push_back(types::PackageName{*val});
                }
            }
        }
        return deps;
    }

    PackageMetadata parsePackageManifest(const std::string& tomlContent) {
        toml::table tbl = toml::parse(tomlContent);

        PackageMetadata metadata;
        metadata.name = types::PackageName{tbl["name"].value_or("")};
        metadata.version = types::PackageVersion{tbl["version"].value_or("")};

        auto cpuArch = tbl["architecture"].value_or("");
        metadata.architecture = (cpuArch == "aarch64" || cpuArch == "AARCH64")
            ? types::CpuArch::AARCH64 : types::CpuArch::AMD64;

        metadata.description = types::Description{tbl["description"].value_or("")};
        metadata.license = tbl["license"].value_or("");
        metadata.homepage = tbl["homepage"].value_or("");
        metadata.maintainer = tbl["maintainer"].value_or("");

        metadata.dependencies = types::Dependencies{parseDepArray(tbl, "depends")};
        metadata.conflicts = types::Dependencies{parseDepArray(tbl, "conflicts")};
        metadata.provides = types::Dependencies{parseDepArray(tbl, "provides")};
        metadata.replaces = types::Dependencies{parseDepArray(tbl, "replaces")};

        return metadata;
    }
}
