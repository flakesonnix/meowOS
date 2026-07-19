#pragma once

#include <map>
#include <memory>
#include <string>

#include "meow/error/error.hpp"
#include "meow/package/package.hpp"
#include "meow/repository/backend.hpp"
#include "meow/repository/repository.hpp"

namespace meow::repository {

// In-memory repository backend used by unit tests. It carries a fully built
// in-memory Repository plus a set of preloaded package artifacts, so the
// resolver, installer, and doctor can be exercised with no disk or network
// access. Artifact descriptors may point at files that are not present in the
// preloaded set to simulate a missing artifact.
class MemoryRepositoryBackend : public IRepositoryBackend {
public:
    // `artifacts` maps an artifact filename to its parsed PackageFile. An
    // artifact referenced by the repository but absent here causes
    // fetchArtifact to throw, mirroring a download/FileNotFound failure.
    explicit MemoryRepositoryBackend(
        Repository repository,
        std::map<std::string, package::PackageFile> artifacts = {})
        : repository_(std::move(repository)),
          artifacts_(std::move(artifacts)) {}

    Repository loadRepository() override { return repository_; }

    RepositoryPackage loadPackage(const types::PackageName& name) override {
        for (const auto& p : repository_.packages)
            if (p.name.value == name.value) return p;
        throw error::MeowError(error::ErrorCode::PackageNotFound,
                               "package not found: " + name.value);
    }

    package::PackageFile fetchArtifact(
        const types::PackageArtifact& artifact) override {
        auto it = artifacts_.find(artifact.filename);
        if (it == artifacts_.end())
            throw error::MeowError(error::ErrorCode::FileNotFound,
                                   "artifact not available in memory: " +
                                       artifact.filename);
        return it->second;
    }

private:
    Repository repository_;
    std::map<std::string, package::PackageFile> artifacts_;
};

}  // namespace meow::repository
