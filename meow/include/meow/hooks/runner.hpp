#ifndef MEOWOS_HOOKS_RUNNER_H
#define MEOWOS_HOOKS_RUNNER_H

#include <filesystem>
#include <string>

#include <meow/hooks/policy.hpp>

namespace meow::hooks {

enum class HookType {
    PreInstall,
    PostInstall,
    PreRemove,
    PostRemove
};

struct HookResult {
    int exitCode = 0;
    std::string output;
};

// Runs a package script under the supplied policy.
//
//  - cwd is an isolated staging dir (/tmp/meow/hooks/<package>/<type>)
//  - environment is a minimal, controlled set unless inheritEnvironment
//  - output is captured and logged; the process is killed on timeout
//    (SIGTERM, then SIGKILL)
//  - network policy is enforced when the OS provides it; otherwise a
//    warning is emitted (never silently allowed)
//
// Throws HookTimeout on timeout, HookFailed on non-zero exit, HookDenied if
// the script may not be executed. Success returns the captured result.
HookResult runHook(const std::filesystem::path& script,
                   const std::string& packageName,
                   const std::string& version,
                   HookType type,
                   const HookPolicy& policy);

}

#endif
