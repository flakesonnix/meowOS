// Disk/network-free tests for the resolver engine config switch. Verifies the
// [resolver] engine setting parses and makeResolver honours it (legacy / sat /
// auto). Auto maps to legacy for now.

#include <cassert>
#include <iostream>

#include <meow/config/config.hpp>
#include <meow/dependency/iresolver.hpp>

using namespace meow;
using namespace meow::config;
using namespace meow::dependency;

namespace {
int failures = 0;
void expectPass(const std::string& what, bool ok) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}
}  // namespace

int main() {
    // parseResolverEngine maps strings.
    expectPass("parse 'legacy' -> Legacy", parseResolverEngine("legacy") == ResolverEngine::Legacy);
    expectPass("parse 'sat' -> Sat", parseResolverEngine("sat") == ResolverEngine::Sat);
    expectPass("parse 'auto' -> Auto", parseResolverEngine("auto") == ResolverEngine::Auto);
    expectPass("parse unknown -> Auto", parseResolverEngine("bogus") == ResolverEngine::Auto);

    // makeResolver returns concrete backends.
    {
        auto r = makeResolver(ResolverEngine::Legacy);
        expectPass("makeResolver(Legacy) non-null", r != nullptr);
    }
    {
        auto r = makeResolver(ResolverEngine::Sat);
        expectPass("makeResolver(Sat) non-null", r != nullptr);
    }
    {
        auto r = makeResolver(ResolverEngine::Auto);
        expectPass("makeResolver(Auto) non-null", r != nullptr);
    }

    if (failures == 0) {
        std::cout << "\nAll resolver-config tests passed.\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) FAILED.\n";
    return 1;
}
