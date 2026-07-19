#ifndef MEOWOS_HOOKS_POLICY_H
#define MEOWOS_HOOKS_POLICY_H

#include <chrono>

namespace meow::hooks {

// Global policy for package scripts. Per-package overrides can be added
// later; for now these are sourced from config ([hooks]).
struct HookPolicy {
    std::chrono::seconds timeout{30};
    bool allowNetwork = false;       // enforced only when OS support exists
    bool inheritEnvironment = false; // if false, a minimal env is used
};

}

#endif
