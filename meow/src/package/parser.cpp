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

        // Optional dependencies: metadata only. Each entry is a [[optional_depends]]
        // table with `package` (name) and an optional `description`.
        if (auto* arr = tbl["optional_depends"].as_array()) {
            for (auto&& node : *arr) {
                if (auto* t = node.as_table()) {
                    types::OptionalDependency od;
                    if (auto pkg = (*t)["package"].value<std::string>()) {
                        od.package = types::PackageName{*pkg};
                    }
                    od.description = (*t)["description"].value_or("");
                    if (!od.package.value.empty()) {
                        metadata.optionalDependencies.push_back(std::move(od));
                    }
                }
            }
        }

        if (auto* build = tbl["build"].as_table()) {
            metadata.build.reproducible = (*build)["reproducible"].value_or(true);
            if (auto epoch = (*build)["source_date_epoch"].value<long long>()) {
                metadata.build.sourceDateEpoch = *epoch;
            }
        }

        // Bootstrap stage metadata (0=normal, 1=stage1, 2=stage2, 3=final)
        if (auto stage = tbl["bootstrapStage"].value<int>()) {
            if (*stage >= 0 && *stage <= 3) {
                metadata.bootstrapStage = *stage;
            }
        }

        return metadata;
    }
}
