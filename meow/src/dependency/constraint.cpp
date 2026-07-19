#include <meow/dependency/constraint.hpp>
#include <sstream>
#include <cctype>
#include <vector>

namespace meow::dependency {

static std::vector<std::string> splitVersionParts(const std::string& v) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : v) {
        if (c == '.') {
            parts.push_back(cur.empty() ? "0" : cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur.empty() ? "0" : cur);
    return parts;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto pa = splitVersionParts(a);
    auto pb = splitVersionParts(b);
    size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        auto sa = i < pa.size() ? pa[i] : "0";
        auto sb = i < pb.size() ? pb[i] : "0";
        int na = 0, nb = 0;
        try { na = std::stoi(sa); } catch (...) {}
        try { nb = std::stoi(sb); } catch (...) {}
        if (na < nb) return -1;
        if (na > nb) return 1;
    }
    return 0;
}

bool satisfiesConstraints(const types::PackageVersion& version,
                           const std::vector<types::VersionConstraint>& constraints) {
    if (constraints.empty()) return true;
    for (const auto& c : constraints) {
        int cmp = compareVersions(version.value, c.version.value);
        bool ok = false;
        if (c.op == "=")  ok = (cmp == 0);
        if (c.op == ">")  ok = (cmp > 0);
        if (c.op == "<")  ok = (cmp < 0);
        if (c.op == ">=") ok = (cmp >= 0);
        if (c.op == "<=") ok = (cmp <= 0);
        if (!ok) return false;
    }
    return true;
}

types::Dependency parseDependencyString(const std::string& input) {
    types::Dependency dep;
    std::string remaining = input;

    // strip whitespace
    auto nonSpace = remaining.find_first_not_of(" \t");
    if (nonSpace != std::string::npos) remaining = remaining.substr(nonSpace);

    // Find where the name ends and constraints begin
    // Name is alphanumeric plus hyphen/underscore
    size_t i = 0;
    while (i < remaining.size() && (std::isalnum(remaining[i]) || remaining[i] == '-' || remaining[i] == '_' || remaining[i] == '+')) {
        ++i;
    }
    dep.name.value = remaining.substr(0, i);
    remaining = remaining.substr(i);

    // Parse constraint groups separated by commas
    while (!remaining.empty()) {
        // skip commas and whitespace
        while (!remaining.empty() && (remaining[0] == ',' || remaining[0] == ' ' || remaining[0] == '\t')) {
            remaining = remaining.substr(1);
        }
        if (remaining.empty()) break;

        types::VersionConstraint vc;
        if (remaining.size() >= 2 && remaining[0] == '>' && remaining[1] == '=') {
            vc.op = ">=";
            remaining = remaining.substr(2);
        } else if (remaining.size() >= 2 && remaining[0] == '<' && remaining[1] == '=') {
            vc.op = "<=";
            remaining = remaining.substr(2);
        } else if (remaining.size() >= 1 && remaining[0] == '>') {
            vc.op = ">";
            remaining = remaining.substr(1);
        } else if (remaining.size() >= 1 && remaining[0] == '<') {
            vc.op = "<";
            remaining = remaining.substr(1);
        } else if (remaining.size() >= 1 && remaining[0] == '=') {
            vc.op = "=";
            remaining = remaining.substr(1);
        } else {
            // no operator means any version, skip rest
            break;
        }

        // skip whitespace after operator
        while (!remaining.empty() && (remaining[0] == ' ' || remaining[0] == '\t')) {
            remaining = remaining.substr(1);
        }

        // parse version number until comma or end
        size_t j = 0;
        while (j < remaining.size() && remaining[j] != ',' && remaining[j] != ' ' && remaining[j] != '\t') {
            ++j;
        }
        vc.version.value = remaining.substr(0, j);
        remaining = remaining.substr(j);

        dep.constraints.push_back(std::move(vc));
    }

    return dep;
}

}
