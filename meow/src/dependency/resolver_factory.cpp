#include <meow/dependency/iresolver.hpp>
#include <meow/dependency/legacy_resolver.hpp>
#include <meow/dependency/sat_resolver.hpp>

namespace meow::dependency {

std::unique_ptr<IResolver> makeResolver(config::ResolverEngine engine) {
    switch (engine) {
        case config::ResolverEngine::Sat:
            return std::make_unique<SatResolver>();
        case config::ResolverEngine::Legacy:
        case config::ResolverEngine::Auto:
        default:
            return std::make_unique<LegacyResolver>();
    }
}

}  // namespace meow::dependency
