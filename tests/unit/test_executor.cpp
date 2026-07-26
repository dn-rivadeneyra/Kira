#include <cassert>
#include <iostream>
#include <vector>
#include <mutex>
#include <future>
#include <condition_variable>
#include "src/core/registry.hpp"
#include "src/core/executor.hpp"
#include "src/core/worker.hpp"

using namespace kira;

void test_command_execution() {
    CommandRegistry registry;
    registry.register_command("greet", [](const nlohmann::json& payload) {
        std::string name = payload.value("name", "Guest");
        return nlohmann::json{{"message", "Hello, " + name + "!"}};
    });

    registry.register_command("throw_err", [](const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("Internal database explosion");
    });

    // 1. Success execution
    InvocationRequest req1{ .id = "req-1", .command = "greet", .payload = nlohmann::json{{"name", "Bob"}} };
    auto res1 = CommandExecutor::execute(req1, registry);
    assert(res1.id == "req-1");
    assert(std::holds_alternative<nlohmann::json>(res1.outcome));
    assert(std::get<nlohmann::json>(res1.outcome)["message"] == "Hello, Bob!");

    // 2. Exception execution (masked to command_exception)
    InvocationRequest req2{ .id = "req-2", .command = "throw_err", .payload = nlohmann::json::object() };
    auto res2 = CommandExecutor::execute(req2, registry);
    assert(res2.id == "req-2");
    assert(std::holds_alternative<CommandError>(res2.outcome));
    const auto& err2 = std::get<CommandError>(res2.outcome);
    assert(err2.code == ErrorCode::command_exception);
    assert(err2.message == "The native command failed"); // Raw exception text masked

    // 3. Command not found
    InvocationRequest req3{ .id = "req-3", .command = "nonexistent", .payload = nlohmann::json::object() };
    auto res3 = CommandExecutor::execute(req3, registry);
    assert(res3.id == "req-3");
    assert(std::holds_alternative<CommandError>(res3.outcome));
    assert(std::get<CommandError>(res3.outcome).code == ErrorCode::command_not_found);

    std::cout << "[PASS] test_command_execution" << std::endl;
}

void test_worker_fifo_execution() {
    WorkerExecutor worker;
    std::mutex mtx;
    std::vector<int> execution_order;
    std::promise<void> done_promise;

    const int total_tasks = 10;
    for (int i = 0; i < total_tasks; ++i) {
        worker.enqueue([i, &execution_order, &mtx, &done_promise, total_tasks]() {
            std::lock_guard<std::mutex> lock(mtx);
            execution_order.push_back(i);
            if (execution_order.size() == total_tasks) {
                done_promise.set_value();
            }
        });
    }

    done_promise.get_future().wait();
    worker.stop_and_join();

    assert(execution_order.size() == total_tasks);
    for (int i = 0; i < total_tasks; ++i) {
        assert(execution_order[i] == i);
    }

    std::cout << "[PASS] test_worker_fifo_execution" << std::endl;
}

void test_worker_shutdown_queued_discard() {
    WorkerExecutor worker;
    std::mutex mtx;
    std::condition_variable cv;
    bool task1_started = false;
    bool task1_can_finish = false;
    bool task1_completed = false;
    bool task2_ran = false;
    bool task3_ran = false;

    // Task 1: blocks until task1_can_finish is set
    worker.enqueue([&]() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            task1_started = true;
        }
        cv.notify_all();

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return task1_can_finish; });
        task1_completed = true;
    });

    // Enqueue queued tasks that should be discarded upon shutdown
    worker.enqueue([&]() {
        std::lock_guard<std::mutex> lock(mtx);
        task2_ran = true;
    });

    worker.enqueue([&]() {
        std::lock_guard<std::mutex> lock(mtx);
        task3_ran = true;
    });

    // Wait until Task 1 has started executing
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return task1_started; });
    }

    // Start shutdown thread. It will acquire worker mutex, clear queue, and block on worker thread join.
    std::thread shutdown_thread([&]() {
        worker.stop_and_join();
    });

    // Brief sleep to ensure shutdown_thread has cleared worker queue and is waiting on join
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now allow task 1 to finish
    {
        std::lock_guard<std::mutex> lock(mtx);
        task1_can_finish = true;
    }
    cv.notify_all();

    shutdown_thread.join();

    // Verify task 1 completed, while queued tasks (task 2 and 3) were discarded
    assert(task1_completed);
    assert(!task2_ran);
    assert(!task3_ran);

    std::cout << "[PASS] test_worker_shutdown_queued_discard" << std::endl;
}

int main() {
    test_command_execution();
    test_worker_fifo_execution();
    test_worker_shutdown_queued_discard();
    std::cout << "ALL EXECUTOR AND WORKER TESTS PASSED." << std::endl;
    return 0;
}
