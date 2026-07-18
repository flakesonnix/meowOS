#include <meow/error/error.hpp>

namespace meow::error {
    std::string formatError(const MeowError& e) {
        std::string codeStr;
        switch (e.code) {
            case ErrorCode::PackageNotFound:    codeStr = "PackageNotFound";    break;
            case ErrorCode::VersionNotFound:    codeStr = "VersionNotFound";    break;
            case ErrorCode::DownloadFailed:     codeStr = "DownloadFailed";     break;
            case ErrorCode::ChecksumMismatch:   codeStr = "ChecksumMismatch";   break;
            case ErrorCode::ArchiveInvalid:     codeStr = "ArchiveInvalid";     break;
            case ErrorCode::FileNotFound:       codeStr = "FileNotFound";       break;
            case ErrorCode::InvalidManifest:    codeStr = "InvalidManifest";    break;
            case ErrorCode::ArchiveOpenFailed:  codeStr = "ArchiveOpenFailed";  break;
            case ErrorCode::RepositoryNotFound:    codeStr = "RepositoryNotFound";    break;
            case ErrorCode::DatabaseOpenFailed:    codeStr = "DatabaseOpenFailed";    break;
            case ErrorCode::DatabaseQueryFailed:   codeStr = "DatabaseQueryFailed";   break;
            case ErrorCode::DatabaseMigrationFailed: codeStr = "DatabaseMigrationFailed"; break;
            case ErrorCode::Internal:              codeStr = "Internal";              break;
        }
        return "error: " + std::string(e.what()) + "\ncode: " + codeStr;
    }
}
