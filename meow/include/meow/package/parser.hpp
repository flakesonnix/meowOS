#ifndef MEOWOS_PARSER_H
#define MEOWOS_PARSER_H

#include <meow/types/types.hpp>
#include <meow/package/package.hpp>

namespace meow::package {
   PackageMetadata parsePackageManifest(const std::string& tomlContent);
}

#endif //MEOWOS_PARSER_H
