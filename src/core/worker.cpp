#include "src/core/worker.hpp"

namespace kira {

WorkerExecutor::WorkerExecutor() {
    worker_thread_ = std::thread(&WorkerExecutor::worker_loop, this);
}

WorkerExecutor::~WorkerExecutor() {
    stop_and_join();
}

bool WorkerExecutor::enqueue(std::function<void()> task) {
    if (!task) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_tasks_ || stopping_) {
            return false;
        }
        queue_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void WorkerExecutor::stop_and_join() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        accepting_tasks_ = false;
        stopping_ = true;

        // Discard unstarted queued tasks
        std::queue<std::function<void()>> empty;
        std::swap(queue_, empty);
    }
    cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void WorkerExecutor::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || stopping_;
            });

            if (stopping_ && queue_.empty()) {
                break;
            }

            if (!queue_.empty()) {
                task = std::move(queue_.front());
                queue_.pop();
            }
        }

        if (task) {
            task();
        }
    }
}

} // namespace kira
