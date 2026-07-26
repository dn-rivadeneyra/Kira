#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>

namespace kira {

class WorkerExecutor {
public:
    WorkerExecutor();
    ~WorkerExecutor();

    WorkerExecutor(const WorkerExecutor&) = delete;
    WorkerExecutor& operator=(const WorkerExecutor&) = delete;

    // Enqueues a task for serial FIFO execution off the UI thread.
    // Returns true if enqueued, false if worker is shutting down.
    bool enqueue(std::function<void()> task);

    // Stops accepting new tasks, finishes current task, discards queued tasks, and joins thread.
    void stop_and_join();

private:
    void worker_loop();

    std::thread worker_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    bool accepting_tasks_{true};
    bool stopping_{false};
};

} // namespace kira
