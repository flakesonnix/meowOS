#ifndef MEOWOS_FORMAT_VERSION_H
#define MEOWOS_FORMAT_VERSION_H

#include <string>

namespace meow::format {

constexpr int CurrentPackageFormat = 1;
constexpr int CurrentRepositoryFormat = 1;
constexpr int CurrentLockfileFormat = 1;
constexpr int CurrentDatabaseSchema = 2;

void requireVersion(
    const std::string& name,
    int found,
    int expected
);

}

#endif
