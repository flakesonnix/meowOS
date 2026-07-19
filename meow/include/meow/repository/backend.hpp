#pragma once

#include <memory>
#include <string>

#include "meow/package/package.hpp"
#include "meow/repository/repository.hpp"
#include "meow/types/types.hpp"

namespace meow::repository {

// Abstract repository access. A backend produces an in-memory `Repository`
// (metadata + artifact descriptors) and can fetch individual package
// artifacts. The resolver, installer, and doctor depend only on this
// interface, never on a concrete transport.
class IRepositoryBackend {
public:
    virtual ~IRepositoryBackend() = default;

    // Load and validate the full repository metadata. Returns a fully
    // populated in-memory Repository whose artifact descriptors carry the
    // URLs/checksums needed to fetch packages.
    virtual Repository loadRepository() = 0;

    // Load the metadata for a single package by name.
    virtual RepositoryPackage loadPackage(const types::PackageName& name) = 0;

    // Download (or locate a cached copy of) the given artifact and return the
    // parsed package file. The backend owns the transport and caching.
    virtual package::PackageFile fetchArtifact(
        const types::PackageArtifact& artifact) = 0;
};

// Filesystem-backed repository (a local directory tree served directly).
class FilesystemRepositoryBackend : public IRepositoryBackend {
public:
    explicit FilesystemRepositoryBackend(std::string url)
        : url_(std::move(url)) {}

    Repository loadRepository() override;
    RepositoryPackage loadPackage(const types::PackageName& name) override;
    package::PackageFile fetchArtifact(
        const types::PackageArtifact& artifact) override;

private:
    std::string url_;
};

// Construct the appropriate backend for a repository URL. file:// and plain
// filesystem paths yield a FilesystemRepositoryBackend. Remote (http/https)
// URLs are not implemented in this version; see the v0.5 roadmap.
std::unique_ptr<IRepositoryBackend> createBackend(const std::string& url);

}  // namespace meow::repository
