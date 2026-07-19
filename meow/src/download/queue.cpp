#include <meow/download/queue.hpp>
#include <meow/error/error.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace meow::download {
    namespace {
        std::filesystem::path cacheDir() {
            const char* home = std::getenv("HOME");
            if (!home) throw error::MeowError(error::ErrorCode::Internal, "HOME not set");
            auto dir = std::filesystem::path(home) / ".cache" / "meow";
            std::filesystem::create_directories(dir);
            return dir;
        }

        std::filesystem::path destFor(const meow::types::PackageArtifact& artifact) {
            return cacheDir() / artifact.filename;
        }
    }

    std::vector<DownloadResult> downloadAll(
        const DownloadQueue& queue,
        const std::vector<DownloadTask>& tasks
    ) {
        if (tasks.empty()) return {};

        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        size_t workers = queue.workers == 0
            ? std::min<size_t>(hw, 8)
            : queue.workers;
        workers = std::min<size_t>(workers, tasks.size());

        std::queue<size_t> pending;
        for (size_t i = 0; i < tasks.size(); ++i) pending.push(i);

        std::mutex m;
        std::condition_variable cv;
        std::vector<DownloadResult> results(tasks.size());
        std::vector<std::exception_ptr> errors(tasks.size());
        std::atomic<bool> failed{false};
        std::atomic<size_t> active{0};
        std::atomic<size_t> done{0};

        auto worker = [&]() {
            for (;;) {
                size_t idx;
                {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [&]() { return !pending.empty() || failed.load() || active.load() == 0; });
                    if (pending.empty()) {
                        if (failed.load() || active.load() == 0) return;
                        continue;
                    }
                    idx = pending.front();
                    pending.pop();
                    ++active;
                }

                try {
                    auto dest = destFor(tasks[idx].artifact);
                    results[idx] = downloadFile(tasks[idx].artifact.url, dest);
                } catch (...) {
                    errors[idx] = std::current_exception();
                    failed.store(true);
                }

                {
                    std::unique_lock<std::mutex> lk(m);
                    --active;
                    ++done;
                }
                cv.notify_all();

                if (failed.load()) return;
            }
        };

        std::vector<std::thread> pool;
        for (size_t i = 0; i < workers; ++i) pool.emplace_back(worker);

        // Wait until everything that will run has completed.
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&]() { return done.load() == tasks.size() || (failed.load() && active.load() == 0); });
        }
        for (auto& t : pool) t.join();

        if (failed.load()) {
            // Clean up any partial files left behind.
            for (const auto& t : tasks) {
                auto part = destFor(t.artifact);
                part += ".part";
                std::error_code ec;
                std::filesystem::remove(part, ec);
            }
            for (const auto& e : errors) {
                if (e) std::rethrow_exception(e);
            }
            throw error::MeowError(error::ErrorCode::DownloadFailed,
                "parallel download failed");
        }

        return results;
    }
}
