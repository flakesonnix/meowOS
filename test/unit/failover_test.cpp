#include <meow/repository/failover.hpp>
#include <meow/repository/status.hpp>
#include <meow/error/error.hpp>

#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace repo = meow::repository;
namespace err = meow::error;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%d)\n", #cond, __LINE__); ++failures; } \
} while (0)

// ── isFailoverAllowed ───────────────────────────────────────────────────────
// Transport errors should trigger failover.
static void test_failover_allowed_transport() {
    auto mk = [](err::ErrorCode c) { return err::MeowError(c, "x"); };
    CHECK( repo::isFailoverAllowed(mk(err::ErrorCode::DownloadTimeout)) );
    CHECK( repo::isFailoverAllowed(mk(err::ErrorCode::DownloadInterrupted)) );
    CHECK( repo::isFailoverAllowed(mk(err::ErrorCode::DownloadFailed)) );
    CHECK( repo::isFailoverAllowed(mk(err::ErrorCode::DownloadHttp5xx)) );
}

// HTTP 4xx must NOT trigger failover (the resource is validly absent).
static void test_failover_blocked_http4xx() {
    auto e = err::MeowError(err::ErrorCode::DownloadHttpError, "404 Not Found");
    CHECK( !repo::isFailoverAllowed(e) );
}

// Trust / metadata failures must NOT trigger failover.
static void test_failover_blocked_trust() {
    auto mk = [](err::ErrorCode c) { return err::MeowError(c, "x"); };
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::InvalidSignature)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::TrustedKeyNotFound)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::RepositoryExpired)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::InvalidRepository)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::InvalidManifest)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::MissingPackageIndex)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::InvalidPackageIndex)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::PackageIndexMismatch)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::PackageNotFound)) );
    CHECK( !repo::isFailoverAllowed(mk(err::ErrorCode::ChecksumMismatch)) );
}

// ── classifyRepositoryError ─────────────────────────────────────────────────
// Transport errors → NetworkError.
static void test_classify_transport_network() {
    auto mk = [](err::ErrorCode c) { return err::MeowError(c, "x"); };
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::DownloadTimeout))
           == repo::RepositoryStatus::NetworkError );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::DownloadInterrupted))
           == repo::RepositoryStatus::NetworkError );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::DownloadFailed))
           == repo::RepositoryStatus::NetworkError );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::DownloadHttp5xx))
           == repo::RepositoryStatus::NetworkError );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::RepositoryNotFound))
           == repo::RepositoryStatus::NetworkError );
}

// HTTP 4xx → Unavailable (not a network error).
static void test_classify_http4xx_unavailable() {
    auto e = err::MeowError(err::ErrorCode::DownloadHttpError, "403 Forbidden");
    CHECK( repo::classifyRepositoryError(e) == repo::RepositoryStatus::Unavailable );
}

// Trust failures → InvalidSignature.
static void test_classify_trust_invalid_signature() {
    auto mk = [](err::ErrorCode c) { return err::MeowError(c, "x"); };
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::InvalidSignature))
           == repo::RepositoryStatus::InvalidSignature );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::TrustedKeyNotFound))
           == repo::RepositoryStatus::InvalidSignature );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::MissingPackageIndex))
           == repo::RepositoryStatus::InvalidSignature );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::InvalidPackageIndex))
           == repo::RepositoryStatus::InvalidSignature );
}

// Expired → Expired.
static void test_classify_expired() {
    auto e = err::MeowError(err::ErrorCode::RepositoryExpired, "x");
    CHECK( repo::classifyRepositoryError(e) == repo::RepositoryStatus::Expired );
}

// Metadata failures → InvalidMetadata.
static void test_classify_invalid_metadata() {
    auto mk = [](err::ErrorCode c) { return err::MeowError(c, "x"); };
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::InvalidRepository))
           == repo::RepositoryStatus::InvalidMetadata );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::InvalidManifest))
           == repo::RepositoryStatus::InvalidMetadata );
    CHECK( repo::classifyRepositoryError(mk(err::ErrorCode::PackageIndexMismatch))
           == repo::RepositoryStatus::InvalidMetadata );
}

// Unrecognised error code → Unavailable (default fallback).
static void test_classify_unknown_unavailable() {
    auto e = err::MeowError(err::ErrorCode::Internal, "unexpected");
    CHECK( repo::classifyRepositoryError(e) == repo::RepositoryStatus::Unavailable );
}

int main() {
    test_failover_allowed_transport();
    test_failover_blocked_http4xx();
    test_failover_blocked_trust();
    test_classify_transport_network();
    test_classify_http4xx_unavailable();
    test_classify_trust_invalid_signature();
    test_classify_expired();
    test_classify_invalid_metadata();
    test_classify_unknown_unavailable();

    if (failures == 0) {
        std::printf("all failover tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
