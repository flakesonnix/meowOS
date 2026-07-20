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
            case ErrorCode::DependencyNotFound:     codeStr = "DependencyNotFound";     break;
            case ErrorCode::DependencyCycleDetected: codeStr = "DependencyCycleDetected"; break;
            case ErrorCode::TransactionFailed:       codeStr = "TransactionFailed";       break;
            case ErrorCode::RollbackFailed:          codeStr = "RollbackFailed";          break;
            case ErrorCode::InvalidSignature:        codeStr = "InvalidSignature";        break;
            case ErrorCode::TrustedKeyNotFound:      codeStr = "TrustedKeyNotFound";      break;
            case ErrorCode::RepositoryExpired:       codeStr = "RepositoryExpired";       break;
            case ErrorCode::InvalidRepository:        codeStr = "InvalidRepository";        break;
            case ErrorCode::MissingPackageIndex:      codeStr = "MissingPackageIndex";      break;
            case ErrorCode::InvalidPackageIndex:      codeStr = "InvalidPackageIndex";      break;
            case ErrorCode::PackageIndexMismatch:     codeStr = "PackageIndexMismatch";     break;
            case ErrorCode::DownloadTimeout:          codeStr = "DownloadTimeout";          break;
            case ErrorCode::DownloadHttpError:         codeStr = "DownloadHttpError";        break;
            case ErrorCode::DownloadInterrupted:       codeStr = "DownloadInterrupted";      break;
            case ErrorCode::InvalidDownload:           codeStr = "InvalidDownload";          break;
            case ErrorCode::HookFailed:                codeStr = "HookFailed";               break;
            case ErrorCode::HookTimeout:               codeStr = "HookTimeout";              break;
            case ErrorCode::HookDenied:                codeStr = "HookDenied";               break;
            case ErrorCode::Internal:                codeStr = "Internal";                break;
        }
        if (e.code == ErrorCode::RepositoryExpired) {
            return std::string(e.what());
        }
        return "error: " + std::string(e.what()) + "\ncode: " + codeStr;
    }
}
