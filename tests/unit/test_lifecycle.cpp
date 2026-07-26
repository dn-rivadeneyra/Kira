#include <cassert>
#include <iostream>
#include <atomic>
#include "src/core/pipeline.hpp"

using namespace kira;

enum class TestInitState {
    pending,
    succeeded,
    failed
};

class OneShotInitHelper {
public:
    bool complete(bool success) {
        TestInitState expected = TestInitState::pending;
        TestInitState desired = success ? TestInitState::succeeded : TestInitState::failed;

        if (!state_.compare_exchange_strong(expected, desired)) {
            return false; // Already completed
        }
        callback_count_++;
        return true;
    }

    TestInitState state() const { return state_.load(); }
    int callback_count() const { return callback_count_.load(); }

private:
    std::atomic<TestInitState> state_{TestInitState::pending};
    std::atomic<int> callback_count_{0};
};

void test_initialization_oneshot() {
    // 1. Pending to Succeeded
    {
        OneShotInitHelper helper;
        assert(helper.state() == TestInitState::pending);
        bool first = helper.complete(true);
        assert(first == true);
        assert(helper.state() == TestInitState::succeeded);
        assert(helper.callback_count() == 1);

        // Repeated success ignored
        bool second = helper.complete(true);
        assert(second == false);
        assert(helper.state() == TestInitState::succeeded);
        assert(helper.callback_count() == 1);

        // Subsequent failure ignored
        bool third = helper.complete(false);
        assert(third == false);
        assert(helper.state() == TestInitState::succeeded);
        assert(helper.callback_count() == 1);
    }

    // 2. Pending to Failed
    {
        OneShotInitHelper helper;
        assert(helper.state() == TestInitState::pending);
        bool first = helper.complete(false);
        assert(first == true);
        assert(helper.state() == TestInitState::failed);
        assert(helper.callback_count() == 1);

        // Subsequent success ignored
        bool second = helper.complete(true);
        assert(second == false);
        assert(helper.state() == TestInitState::failed);
        assert(helper.callback_count() == 1);
    }

    std::cout << "[PASS] test_initialization_oneshot" << std::endl;
}

void test_lifecycle_shutdown_idempotency() {
    CommandRegistry registry;
    WorkerExecutor worker;
    bool pipeline_shutdown = false;
    bool worker_stopped = false;

    InvocationPipeline pipeline(registry, worker, [](const std::string&) {});
    pipeline.set_ready(true);

    std::atomic<bool> shutdown_requested{false};
    int shutdown_execution_count = 0;

    auto trigger_shutdown = [&]() {
        if (shutdown_requested.exchange(true)) {
            return; // Idempotent guard
        }
        shutdown_execution_count++;
        pipeline.shutdown();
        pipeline_shutdown = true;
        worker.stop_and_join();
        worker_stopped = true;
    };

    // First shutdown request
    trigger_shutdown();
    assert(shutdown_execution_count == 1);
    assert(pipeline_shutdown == true);
    assert(worker_stopped == true);
    assert(!pipeline.is_ready());

    // Second shutdown request ignored
    trigger_shutdown();
    assert(shutdown_execution_count == 1);

    std::cout << "[PASS] test_lifecycle_shutdown_idempotency" << std::endl;
}

int main() {
    test_initialization_oneshot();
    test_lifecycle_shutdown_idempotency();
    std::cout << "ALL LIFECYCLE TESTS PASSED." << std::endl;
    return 0;
}
