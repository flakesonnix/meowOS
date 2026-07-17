#ifndef MEOWOS_PARSER_H
#define MEOWOS_PARSER_H

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>

namespace meow::package {
   PackageMetadata ParsePackageManifest(const std::string& path);
}

#endif //MEOWOS_PARSER_H
