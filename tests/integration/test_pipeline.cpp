#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <condition_variable>
#include <chrono>
#include "src/core/pipeline.hpp"

using namespace kira;

class PipelineTestHarness {
public:
    PipelineTestHarness()
        : pipeline_(registry_, worker_, [this](const std::string& msg) {
            std::lock_guard<std::mutex> lock(mtx_);
            responses_.push_back(msg);
            if (promise_) {
                promise_->set_value(msg);
            }
        })
    {
        pipeline_.set_ready(true);
    }

    ~PipelineTestHarness() {
        pipeline_.shutdown();
        worker_.stop_and_join();
    }

    CommandRegistry& registry() { return registry_; }
    WorkerExecutor& worker() { return worker_; }
    InvocationPipeline& pipeline() { return pipeline_; }

    std::string process_and_wait(const std::string& raw_msg) {
        std::promise<std::string> prom;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            promise_ = &prom;
        }
        pipeline_.process_raw_message(raw_msg);
        std::future<std::string> fut = prom.get_future();
        auto status = fut.wait_for(std::chrono::seconds(5));
        assert(status == std::future_status::ready);
        std::string res = fut.get();
        {
            std::lock_guard<std::mutex> lock(mtx_);
            promise_ = nullptr;
        }
        return res;
    }

    std::vector<std::string> get_responses() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return responses_;
    }

private:
    CommandRegistry registry_;
    WorkerExecutor worker_;
    mutable std::mutex mtx_;
    std::promise<std::string>* promise_{nullptr};
    std::vector<std::string> responses_;
    InvocationPipeline pipeline_;
};

void test_pipeline_valid_command() {
    PipelineTestHarness harness;
    harness.registry().register_command("greet", [](const nlohmann::json& payload) {
        return nlohmann::json{{"message", "Hello, " + payload.value("name", "World") + "!"}};
    });

    std::string req = R"({ "version": 1, "type": "invoke", "id": "req-1", "command": "greet", "payload": { "name": "Alice" } })";
    std::string resp_str = harness.process_and_wait(req);

    auto json = nlohmann::json::parse(resp_str);
    assert(json["version"] == 1);
    assert(json["type"] == "result");
    assert(json["id"] == "req-1");
    assert(json["ok"] == true);
    assert(json["value"]["message"] == "Hello, Alice!");
    std::cout << "[PASS] test_pipeline_valid_command" << std::endl;
}

void test_pipeline_unknown_command() {
    PipelineTestHarness harness;
    std::string req = R"({ "version": 1, "type": "invoke", "id": "req-2", "command": "nonexistent" })";
    std::string resp_str = harness.process_and_wait(req);

    auto json = nlohmann::json::parse(resp_str);
    assert(json["version"] == 1);
    assert(json["type"] == "result"); // Valid invocation reaching executor uses "type": "result"
    assert(json["id"] == "req-2");
    assert(json["ok"] == false);
    assert(json["error"]["code"] == "command_not_found");
    std::cout << "[PASS] test_pipeline_unknown_command" << std::endl;
}

void test_pipeline_protocol_errors() {
    PipelineTestHarness harness;

    // 1. Invalid JSON
    std::string resp1_str = harness.process_and_wait("{ invalid json ");
    auto json1 = nlohmann::json::parse(resp1_str);
    assert(json1["version"] == 1);
    assert(json1["type"] == "protocol_error");
    assert(json1["id"].is_null());
    assert(json1["error"]["code"] == "invalid_json");

    // 2. Invalid Protocol Version with recoverable ID
    std::string req2 = R"({ "version": 99, "type": "invoke", "id": "recover-id-123", "command": "greet" })";
    std::string resp2_str = harness.process_and_wait(req2);
    auto json2 = nlohmann::json::parse(resp2_str);
    assert(json2["version"] == 1);
    assert(json2["type"] == "protocol_error");
    assert(json2["id"] == "recover-id-123");
    assert(json2["error"]["code"] == "unsupported_protocol_version");

    // 3. Invalid Payload
    std::string req3 = R"({ "version": 1, "type": "invoke", "id": "req-3", "command": "greet", "payload": [1, 2] })";
    std::string resp3_str = harness.process_and_wait(req3);
    auto json3 = nlohmann::json::parse(resp3_str);
    assert(json3["version"] == 1);
    assert(json3["type"] == "protocol_error");
    assert(json3["id"] == "req-3");
    assert(json3["error"]["code"] == "invalid_payload");

    std::cout << "[PASS] test_pipeline_protocol_errors" << std::endl;
}

void test_pipeline_fifo_and_off_caller_thread() {
    PipelineTestHarness harness;
    std::thread::id caller_id = std::this_thread::get_id();
    std::thread::id worker_id;

    std::vector<int> execution_order;
    std::mutex mtx;

    for (int i = 0; i < 5; ++i) {
        std::string cmd_name = "cmd_" + std::to_string(i);
        harness.registry().register_command(cmd_name, [i, &caller_id, &worker_id, &execution_order, &mtx](const nlohmann::json&) {
            worker_id = std::this_thread::get_id();
            std::lock_guard<std::mutex> lock(mtx);
            execution_order.push_back(i);
            return nlohmann::json{{"idx", i}};
        });
    }

    for (int i = 0; i < 5; ++i) {
        std::string req = "{\"version\":1,\"type\":\"invoke\",\"id\":\"id-" + std::to_string(i) + "\",\"command\":\"cmd_" + std::to_string(i) + "\"}";
        harness.pipeline().process_raw_message(req);
    }

    // Wait for all 5 responses
    while (harness.get_responses().size() < 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(worker_id != caller_id);
    assert(execution_order.size() == 5);
    for (int i = 0; i < 5; ++i) {
        assert(execution_order[i] == i);
    }

    std::cout << "[PASS] test_pipeline_fifo_and_off_caller_thread" << std::endl;
}

void test_pipeline_shutdown_behavior() {
    CommandRegistry registry;
    WorkerExecutor worker;
    std::mutex mtx;
    std::condition_variable cv;
    bool task1_started = false;
    bool task1_can_finish = false;
    bool task1_completed = false;
    bool task2_ran = false;

    registry.register_command("block_cmd", [&](const nlohmann::json&) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            task1_started = true;
        }
        cv.notify_all();

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return task1_can_finish; });
        task1_completed = true;
        return nlohmann::json{{"ok", true}};
    });

    registry.register_command("queued_cmd", [&](const nlohmann::json&) {
        std::lock_guard<std::mutex> lock(mtx);
        task2_ran = true;
        return nlohmann::json{{"ok", true}};
    });

    std::vector<std::string> pipeline_responses;
    InvocationPipeline pipeline(registry, worker, [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        pipeline_responses.push_back(msg);
    });
    pipeline.set_ready(true);

    // Send task 1 and task 2
    pipeline.process_raw_message(R"({ "version": 1, "type": "invoke", "id": "t1", "command": "block_cmd" })");
    pipeline.process_raw_message(R"({ "version": 1, "type": "invoke", "id": "t2", "command": "queued_cmd" })");

    // Wait until task 1 starts
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return task1_started; });
    }

    // Trigger pipeline and worker shutdown
    pipeline.shutdown();
    std::thread shutdown_thread([&]() {
        worker.stop_and_join();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Allow task 1 to finish
    {
        std::lock_guard<std::mutex> lock(mtx);
        task1_can_finish = true;
    }
    cv.notify_all();

    shutdown_thread.join();

    assert(task1_completed);
    assert(!task2_ran);

    // Verify no callbacks fired after shutdown
    assert(pipeline_responses.empty());

    std::cout << "[PASS] test_pipeline_shutdown_behavior" << std::endl;
}

int main() {
    test_pipeline_valid_command();
    test_pipeline_unknown_command();
    test_pipeline_protocol_errors();
    test_pipeline_fifo_and_off_caller_thread();
    test_pipeline_shutdown_behavior();
    std::cout << "ALL INVOCATION PIPELINE TESTS PASSED." << std::endl;
    return 0;
}
