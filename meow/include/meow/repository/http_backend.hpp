#pragma once

#include <string>

#include "meow/repository/backend.hpp"

namespace meow::repository {

// Repository accessed over HTTP(S). Metadata, package manifests, and
// artifacts are fetched through the shared download layer (download::) so
// that retries, timeouts, ETag caching, and checksum verification are
// identical to every other transport. The trust chain (signature verify +
// expiry check) is the same as the filesystem backend.
class HttpRepositoryBackend : public IRepositoryBackend {
public:
    explicit HttpRepositoryBackend(std::string baseUrl);

    Repository loadRepository() override;
    RepositoryPackage loadPackage(const types::PackageName& name) override;
    package::PackageFile fetchArtifact(
        const types::PackageArtifact& artifact) override;

private:
    std::string baseUrl_;

    std::string absUrl(const std::string& relPath) const;
};

}  // namespace meow::repository
