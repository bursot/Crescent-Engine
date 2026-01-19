#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Crescent {

class JobSystem {
    struct Fence;

public:
    using Job = std::function<void()>;

    struct JobHandle {
        std::shared_ptr<Fence> fence;
        bool valid() const { return static_cast<bool>(fence); }
    };

    JobSystem() = default;
    ~JobSystem() { stop(); }

    void start(size_t workerCount = 0) {
        if (m_running.load()) {
            return;
        }
        size_t detected = std::thread::hardware_concurrency();
        if (workerCount == 0) {
            workerCount = detected > 1 ? detected - 1 : 1;
        }
        m_running.store(true);
        m_workers.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i) {
            m_workers.emplace_back([this]() { workerLoop(); });
        }
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;
        }
        m_queueCv.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    JobHandle createHandle() const {
        return JobHandle{std::make_shared<Fence>()};
    }

    JobHandle submit(Job job) {
        JobHandle handle = createHandle();
        submit(std::move(job), handle);
        return handle;
    }

    void submit(Job job, const JobHandle& handle) {
        if (!handle.fence) {
            if (job) {
                job();
            }
            return;
        }
        handle.fence->remaining.fetch_add(1, std::memory_order_relaxed);
        if (!m_running.load()) {
            if (job) {
                job();
            }
            if (handle.fence->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lock(handle.fence->mutex);
                handle.fence->cv.notify_all();
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push(JobItem{std::move(job), handle.fence});
        }
        m_queueCv.notify_one();
    }

    void wait(const JobHandle& handle) const {
        if (!handle.fence) {
            return;
        }
        if (handle.fence->remaining.load(std::memory_order_acquire) == 0) {
            return;
        }
        std::unique_lock<std::mutex> lock(handle.fence->mutex);
        handle.fence->cv.wait(lock, [&]() {
            return handle.fence->remaining.load(std::memory_order_acquire) == 0;
        });
    }

    size_t workerCount() const { return m_workers.size(); }

private:
    struct Fence {
        std::atomic<int> remaining{0};
        mutable std::mutex mutex;
        mutable std::condition_variable cv;
    };

    struct JobItem {
        Job job;
        std::shared_ptr<Fence> fence;
    };

    void workerLoop() {
        while (true) {
            JobItem item;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [&]() {
                    return !m_running.load() || !m_queue.empty();
                });
                if (!m_running.load() && m_queue.empty()) {
                    break;
                }
                item = std::move(m_queue.front());
                m_queue.pop();
            }
            if (item.job) {
                item.job();
            }
            if (item.fence) {
                if (item.fence->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::lock_guard<std::mutex> lock(item.fence->mutex);
                    item.fence->cv.notify_all();
                }
            }
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<JobItem> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::atomic<bool> m_running{false};
};

} // namespace Crescent
