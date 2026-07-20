#include <meow/repository/security_policy.hpp>

namespace meow::repository {

namespace {
SecurityPolicy g_policy{};
}

void setSecurityPolicy(const SecurityPolicy& policy) { g_policy = policy; }

const SecurityPolicy& securityPolicy() { return g_policy; }

}  // namespace meow::repository
