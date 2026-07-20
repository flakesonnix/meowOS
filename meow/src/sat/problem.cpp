#include <meow/sat/problem.hpp>

namespace meow::sat {

Variable Problem::declare(const std::string& name) {
    auto it = names_.find(name);
    if (it != names_.end()) return it->second;
    Variable id = nextVar_++;
    names_.emplace(name, id);
    return id;
}

Variable Problem::lookup(const std::string& name) const {
    auto it = names_.find(name);
    if (it == names_.end()) return 0;
    return it->second;
}

bool Problem::has(const std::string& name) const {
    return names_.find(name) != names_.end();
}

}  // namespace meow::sat
