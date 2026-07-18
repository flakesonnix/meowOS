#include <meow/repository/version.hpp>
#include <charconv>
#include <cstring>
#include <vector>

namespace meow::repository {
    types::PackageVersion parseVersion(std::string_view version) {
        return types::PackageVersion{std::string(version)};
    }

    int compareVersions(const types::PackageVersion& lhs, const types::PackageVersion& rhs) {
        auto split = [](std::string_view sv) -> std::vector<int> {
            std::vector<int> parts;
            const char* start = sv.data();
            const char* end = sv.data() + sv.size();

            while (start < end) {
                int val = 0;
                auto [ptr, ec] = std::from_chars(start, end, val);
                if (ec == std::errc()) {
                    parts.push_back(val);
                    start = ptr;
                } else {
                    ++start;
                }
                if (start < end && *start == '.') {
                    ++start;
                }
            }
            return parts;
        };

        auto lhsParts = split(lhs.value);
        auto rhsParts = split(rhs.value);

        for (size_t i = 0; i < lhsParts.size() && i < rhsParts.size(); ++i) {
            if (lhsParts[i] < rhsParts[i]) return -1;
            if (lhsParts[i] > rhsParts[i]) return 1;
        }

        if (lhsParts.size() < rhsParts.size()) return -1;
        if (lhsParts.size() > rhsParts.size()) return 1;
        return 0;
    }

    const types::PackageVersion* latestVersion(const RepositoryPackage& pkg) {
        if (pkg.versions.empty()) return nullptr;

        const RepositoryVersion* best = &pkg.versions[0];
        for (const auto& v : pkg.versions) {
            if (compareVersions(v.version, best->version) > 0) {
                best = &v;
            }
        }
        return &best->version;
    }
}
