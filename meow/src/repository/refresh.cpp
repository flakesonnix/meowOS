#include "meow/repository/refresh.hpp"

#include <meow/repository/backend.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace meow::repository {

namespace {

size_t resolveWorkers(size_t requested) {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    size_t w = requested == 0 ? std::min<size_t>(hw, 8) : requested;
    return w;
}

}  // namespace

std::vector<RefreshResult> refreshRepositories(
    const std::vector<config::RepositoryConfig>& repos,
    size_t workers) {
    if (repos.empty()) return {};

    size_t w = resolveWorkers(workers);
    w = std::min(w, repos.size());

    std::queue<size_t> pending;
    for (size_t i = 0; i < repos.size(); ++i) pending.push(i);

    std::mutex m;
    std::condition_variable cv;
    std::vector<RefreshResult> results(repos.size());
    std::atomic<size_t> active{0};
    std::atomic<size_t> done{0};

    // Per-source failover to the next mirror is handled inside
    // loadRepositoryWithFailover. The pool itself is NOT fail-fast: a broken
    // source is recorded and the other sources keep going.
    auto loader = [](const std::string& url) {
        return createBackend(url)->loadRepository();
    };

    auto worker = [&]() {
        for (;;) {
            size_t idx;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&]() { return !pending.empty() || active.load() == 0; });
                if (pending.empty() && active.load() == 0) return;
                if (pending.empty()) continue;
                idx = pending.front();
                pending.pop();
                ++active;
            }

            auto r = loadRepositoryWithFailover(repos[idx].urls(), loader);
            RefreshResult out;
            out.config = repos[idx];
            out.status = r.status;
            out.attempts = std::move(r.attempts);
            if (r.success) out.repository = std::move(r.repository);
            results[idx] = std::move(out);

            {
                std::unique_lock<std::mutex> lk(m);
                --active;
                ++done;
            }
            cv.notify_all();
        }
    };

    std::vector<std::thread> pool;
    for (size_t i = 0; i < w; ++i) pool.emplace_back(worker);

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&]() { return done.load() == repos.size(); });
    }
    for (auto& t : pool) t.join();

    return results;
}

}  // namespace meow::repository
