#ifndef MEOWOS_DOWNLOAD_QUEUE_H
#define MEOWOS_DOWNLOAD_QUEUE_H

#include <meow/download/downloader.hpp>
#include <meow/types/types.hpp>
#include <vector>

namespace meow::download {

struct DownloadTask {
    meow::types::PackageArtifact artifact;
};

struct DownloadQueue {
    // 0 = default (min(hardware_concurrency, 8))
    size_t workers = 0;
};

// Downloads all tasks concurrently using a bounded worker pool.
// Returns one DownloadResult per task, in input order.
// On any task failure: remaining tasks are not started, in-flight tasks
// are joined, partial (.part) files are removed, and the first error is
// rethrown. Downloads are independent of verification/installation.
std::vector<DownloadResult> downloadAll(
    const DownloadQueue& queue,
    const std::vector<DownloadTask>& tasks
);

}

#endif
