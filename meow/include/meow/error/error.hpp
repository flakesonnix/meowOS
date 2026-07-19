#ifndef MEOWOS_ERROR_H
#define MEOWOS_ERROR_H

#include <stdexcept>
#include <string>

namespace meow::error {
    enum class ErrorCode {
        PackageNotFound,
        VersionNotFound,
        DownloadFailed,
        ChecksumMismatch,
        ArchiveInvalid,
        FileNotFound,
        InvalidManifest,
        ArchiveOpenFailed,
        RepositoryNotFound,
        DatabaseOpenFailed,
        DatabaseQueryFailed,
        DatabaseMigrationFailed,
        DependencyNotFound,
        DependencyCycleDetected,
        TransactionFailed,
        RollbackFailed,
        InvalidSignature,
        TrustedKeyNotFound,
        Internal
    };

    struct MeowError : std::runtime_error {
        ErrorCode code;

        explicit MeowError(ErrorCode code, const std::string& message)
            : std::runtime_error(message), code(code) {}

        explicit MeowError(ErrorCode code, const char* message)
            : std::runtime_error(message), code(code) {}
    };

    std::string formatError(const MeowError& e);
}

#endif //MEOWOS_ERROR_H
