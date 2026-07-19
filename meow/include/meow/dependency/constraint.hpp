#ifndef MEOWOS_CONSTRAINT_H
#define MEOWOS_CONSTRAINT_H

#include <meow/types/types.hpp>

namespace meow::dependency {

types::Dependency parseDependencyString(const std::string& input);
bool satisfiesConstraints(const types::PackageVersion& version, const std::vector<types::VersionConstraint>& constraints);
int compareVersions(const std::string& a, const std::string& b);

}

#endif
