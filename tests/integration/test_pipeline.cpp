#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include "src/core/protocol.hpp"
#include "src/core/registry.hpp"
#include "src/core/executor.hpp"
#include "src/core/worker.hpp"

using namespace kira;

// Platform-independent FakeTransport simulating raw UTF-8 string transport boundary
class FakeTransport {
public:
    void send_response(const std::string& raw_response) {
        std::lock_guard<std::mutex> lock(mtx_);
        received_responses_.push_back(raw_response);
        if (on_response_prom_) {
            on_response_prom_->set_value(raw_response);
        }
    }

    std::vector<std::string> get_responses() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return received_responses_;
    }

    void set_promise(std::promise<std::string>* prom) {
        std::lock_guard<std::mutex> lock(mtx_);
        on_response_prom_ = prom;
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::string> received_responses_;
    std::promise<std::string>* on_response_prom_{nullptr};
};

void test_full_pipeline_success() {
    CommandRegistry registry;
    registry.register_command("greet", [](const nlohmann::json& payload) {
        std::string name = payload.value("name", "World");
        return nlohmann::json{{"greeting", "Hello, " + name + "!"}};
    });

    WorkerExecutor worker;
    FakeTransport transport;
    std::promise<std::string> response_promise;
    transport.set_promise(&response_promise);

    // 1. Raw wire message received at transport boundary
    std::string raw_wire_req = R"({
        "version": 1,
        "type": "invoke",
        "id": "pipe-req-1",
        "command": "greet",
        "payload": { "name": "Kira Pipeline" }
    })";

    // 2. Parse protocol message
    auto parse_res = ProtocolCodec::parse(raw_wire_req);
    assert(std::holds_alternative<InvocationRequest>(parse_res));
    auto req = std::get<InvocationRequest>(parse_res);

    // 3. Enqueue to WorkerExecutor off calling thread
    worker.enqueue([req, &registry, &transport]() {
        // Execute on worker thread
        auto result = CommandExecutor::execute(req, registry);
        // Serialize result
        std::string raw_resp = ProtocolCodec::serialize_result(result);
        // Send back to fake transport
        transport.send_response(raw_resp);
    });

    // 4. Wait for response at transport boundary
    std::string response_json = response_promise.get_future().get();
    worker.stop_and_join();

    // 5. Verify response payload
    auto resp = nlohmann::json::parse(response_json);
    assert(resp["version"] == 1);
    assert(resp["type"] == "result");
    assert(resp["id"] == "pipe-req-1");
    assert(resp["ok"] == true);
    assert(resp["value"]["greeting"] == "Hello, Kira Pipeline!");

    std::cout << "[PASS] test_full_pipeline_success" << std::endl;
}

void test_full_pipeline_protocol_error() {
    WorkerExecutor worker;
    FakeTransport transport;

    std::string raw_invalid_msg = R"({ "version": 99, "type": "invoke", "id": "err-req" })";
    auto parse_res = ProtocolCodec::parse(raw_invalid_msg);
    assert(std::holds_alternative<ProtocolError>(parse_res));

    auto proto_err = std::get<ProtocolError>(parse_res);
    std::string serialized_err = ProtocolCodec::serialize_protocol_error(proto_err);
    transport.send_response(serialized_err);

    auto resp = nlohmann::json::parse(transport.get_responses()[0]);
    assert(resp["version"] == 1);
    assert(resp["ok"] == false);
    assert(resp["error"]["code"] == "unsupported_protocol_version");

    std::cout << "[PASS] test_full_pipeline_protocol_error" << std::endl;
}

int main() {
    test_full_pipeline_success();
    test_full_pipeline_protocol_error();
    std::cout << "ALL PIPELINE TESTS PASSED." << std::endl;
    return 0;
}
