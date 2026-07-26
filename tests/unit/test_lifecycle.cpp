#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include "src/core/gates.hpp"
#include "src/core/shutdown_coordinator.hpp"
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

void test_production_shutdown_coordinator() {
    CommandRegistry registry;
    WorkerExecutor worker;
    std::vector<std::string> step_order;

    InvocationPipeline pipeline(registry, worker, [](const std::string&) {});
    pipeline.set_ready(true);

    ShutdownGate shutdown_gate;
    AppShutdownCoordinator coordinator;

    // Execute production AppShutdownCoordinator sequence
    bool executed1 = coordinator.execute_shutdown(
        shutdown_gate,
        pipeline,
        worker,
        [&]() {
            step_order.push_back("resource_close");
        },
        [&]() {
            step_order.push_back("quit_post");
        }
    );

    assert(executed1 == true);
    assert(!pipeline.is_ready());
    assert(step_order.size() == 2);
    assert(step_order[0] == "resource_close");
    assert(step_order[1] == "quit_post");

    // Second shutdown execution is ignored by ShutdownGate
    bool executed2 = coordinator.execute_shutdown(
        shutdown_gate,
        pipeline,
        worker,
        [&]() {
            step_order.push_back("resource_close_duplicate");
        },
        [&]() {
            step_order.push_back("quit_post_duplicate");
        }
    );

    assert(executed2 == false);
    assert(step_order.size() == 2);

    std::cout << "[PASS] test_production_shutdown_coordinator" << std::endl;
}

int main() {
    test_initialization_gate_production();
    test_initialization_gate_failure();
    test_shutdown_gate_production();
    test_production_shutdown_coordinator();
    std::cout << "ALL LIFECYCLE TESTS PASSED." << std::endl;
    return 0;
}
