#include <meow/download/queue.hpp>
#include <meow/error/error.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>

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

        const char* spinFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

        void printProgress(const std::string& filename, size_t current, size_t total, int frame = -1) {
            const int barWidth = 30;
            float progress = total > 0 ? static_cast<float>(current) / total : 0.0f;
            int pos = static_cast<int>(barWidth * progress);

            std::cout << "\r  \x1b[36m→\x1b[0m [\x1b[90m" << std::setw(3) << current << "\x1b[0m/\x1b[90m" << std::setw(3) << total << "\x1b[0m] "
                      << "\x1b[36m";
            for (int i = 0; i < barWidth; ++i) {
                if (i < pos) std::cout << "█";
                else std::cout << "░";
            }
            std::cout << "\x1b[0m " << "\x1b[33m" << std::fixed << std::setprecision(0) << (progress * 100.0f) << "%\x1b[0m "
                      << "\x1b[90m" << filename << "\x1b[0m" << std::flush;
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
        std::atomic<int> frameCounter{0};

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
                    // Print progress from worker thread
                    int frame = frameCounter.fetch_add(1) % 10;
                    printProgress(tasks[idx].artifact.filename, done.load() + 1, tasks.size(), frame);
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

        if (done.load() > 0) {
            std::cout << "\r" << std::string(120, ' ') << "\r" << std::flush;
        }

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
