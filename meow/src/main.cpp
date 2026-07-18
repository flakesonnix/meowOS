//
// Created by lucy on 17.07.26.
//


#include <iostream>

#include "meow/package/parser.hpp"

std::ostream& operator<<(std::ostream& os, const meow::package::PackageMetadata& meta) {
    os << "--- Package Metadata ---\n"
       << "Name:         " << meta.name.value << "\n"
       << "Version:      " << meta.version.value << "\n"
       << "Architecture: " << (meta.CpuArch == meow::types::CpuArch::AMD64 ? "AMD64" : "AARCH64") << "\n"
       << "Description:  " << meta.description.value << "\n"
       << "Dependencies: ";

    if (meta.dependencies.value.empty()) {
        os << "none";
    } else {
        for (const auto& dep : meta.dependencies.value) {
            os << "\n  - " << dep.value;
        }
    }
    return os;
}

int main() {
    const auto test = meow::package::ParsePackageManifest("./examples/hello.toml");

    std::cout <<test << std::endl;

    return 0;
}
