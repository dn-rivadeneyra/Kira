#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include "src/core/gates.hpp"
#include "src/core/pipeline.hpp"

using namespace kira;

void test_initialization_gate_production() {
    InitializationGate gate;
    assert(gate.state() == InitializationResult::pending);
    assert(!gate.is_completed());

    // 1. First completion attempt transitions state
    bool first = gate.complete(true);
    assert(first == true);
    assert(gate.state() == InitializationResult::succeeded);
    assert(gate.is_completed());

    // 2. Subsequent completion attempts are ignored (first call wins)
    bool second = gate.complete(true);
    assert(second == false);
    assert(gate.state() == InitializationResult::succeeded);

    bool third = gate.complete(false);
    assert(third == false);
    assert(gate.state() == InitializationResult::succeeded);
}

void test_initialization_gate_failure() {
    InitializationGate gate;
    assert(gate.state() == InitializationResult::pending);

    bool first = gate.complete(false);
    assert(first == true);
    assert(gate.state() == InitializationResult::failed);
    assert(gate.is_completed());

    bool second = gate.complete(true);
    assert(second == false);
    assert(gate.state() == InitializationResult::failed);
}

void test_shutdown_gate_production() {
    ShutdownGate gate;
    assert(!gate.requested());

    // First request returns true
    bool first = gate.request();
    assert(first == true);
    assert(gate.requested() == true);

    // Later requests return false
    bool second = gate.request();
    assert(second == false);
    assert(gate.requested() == true);
}

void test_lifecycle_shutdown_ordering() {
    CommandRegistry registry;
    WorkerExecutor worker;
    std::vector<std::string> order;

    InvocationPipeline pipeline(registry, worker, [](const std::string&) {});
    pipeline.set_ready(true);

    ShutdownGate shutdown_gate;

    auto execute_shutdown_sequence = [&]() {
        if (!shutdown_gate.request()) {
            return;
        }

        // 1. Pipeline shutdown
        pipeline.shutdown();
        order.push_back("pipeline_shutdown");

        // 2. Worker stop and join
        worker.stop_and_join();
        order.push_back("worker_join");

        // 3. Resource close callback
        order.push_back("window_resource_close");

        // 4. Quit request callback
        order.push_back("quit_request");
    };

    // First shutdown execution
    execute_shutdown_sequence();

    assert(order.size() == 4);
    assert(order[0] == "pipeline_shutdown");
    assert(order[1] == "worker_join");
    assert(order[2] == "window_resource_close");
    assert(order[3] == "quit_request");
    assert(!pipeline.is_ready());

    // Second shutdown execution (ignored)
    execute_shutdown_sequence();
    assert(order.size() == 4);

    std::cout << "[PASS] test_lifecycle_shutdown_ordering" << std::endl;
}

int main() {
    test_initialization_gate_production();
    test_initialization_gate_failure();
    test_shutdown_gate_production();
    test_lifecycle_shutdown_ordering();
    std::cout << "ALL LIFECYCLE TESTS PASSED." << std::endl;
    return 0;
}
