//
// Created by lucy on 17.07.26.
//

#include <toml++/toml.hpp>
#include <meow/package/parser.hpp>
#include <meow/package/package.hpp>

namespace meow::package {
    PackageMetadata ParsePackageManifest(const std::string& path) {
        toml::table tbl = toml::parse_file(path);



        std::string name = tbl["metadata"]["name"].value_or("");
        std::string version = tbl["metadata"]["version"].value_or("");
        std::string cpu_arch = tbl["metadata"]["architecture"].value_or("");
        std::string description = tbl["metadata"]["description"].value_or("");

        types::CpuArch arch = types::CpuArch::AMD64;
        if (cpu_arch == "AARCH64") {
            arch = types::CpuArch::AARCH64;
        }

        std::vector<types::PackageName> deps;
        if (auto* deps_arr = tbl["metadata"]["dependencies"].as_array()) {
            for (auto&& node : *deps_arr) {
                if (auto val = node.value<std::string>()) {
                    deps.push_back(types::PackageName{ *val });
                }
            }
        }

        PackageMetadata metadata;

        metadata.name = types::PackageName { name };
        metadata.version = types::PackageVersion { version };
        metadata.CpuArch = arch;
        metadata.description = types::Description { description  };
        metadata.dependencies = types::Dependencies { deps };

        return metadata;
    }
}
