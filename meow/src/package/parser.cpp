#include <toml++/toml.hpp>
#include <meow/package/parser.hpp>
#include <meow/package/package.hpp>
#include <meow/dependency/constraint.hpp>
#include <meow/format/version.hpp>

namespace meow::package {
    static types::Dependencies parseDeps(const toml::table& tbl, const std::string& key) {
        std::vector<types::Dependency> deps;
        if (auto* arr = tbl[key].as_array()) {
            for (auto&& node : *arr) {
                if (auto val = node.value<std::string>()) {
                    deps.push_back(dependency::parseDependencyString(*val));
                }
            }
        }
        return types::Dependencies{std::move(deps)};
    }

    PackageMetadata parsePackageManifest(const std::string& tomlContent) {
        toml::table tbl = toml::parse(tomlContent);

        auto formatVersion = tbl["format_version"].value_or(1);
        format::requireVersion("package", formatVersion, format::CurrentPackageFormat);

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

        metadata.dependencies = parseDeps(tbl, "depends");
        metadata.conflicts = parseDeps(tbl, "conflicts");
        metadata.provides = parseDeps(tbl, "provides");
        metadata.replaces = parseDeps(tbl, "replaces");

        if (auto* build = tbl["build"].as_table()) {
            metadata.build.reproducible = (*build)["reproducible"].value_or(true);
            if (auto epoch = (*build)["source_date_epoch"].value<long long>()) {
                metadata.build.sourceDateEpoch = *epoch;
            }
        }

        return metadata;
    }
}
