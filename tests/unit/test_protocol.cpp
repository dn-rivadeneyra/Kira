#include <cassert>
#include <iostream>
#include <string>
#include "src/core/protocol.hpp"

using namespace kira;

void test_valid_invocation() {
    std::string raw = R"({
        "version": 1,
        "type": "invoke",
        "id": "req-123",
        "command": "greet",
        "payload": { "name": "Alice" }
    })";

    auto parsed = ProtocolCodec::parse(raw);
    assert(std::holds_alternative<InvocationRequest>(parsed));

    const auto& req = std::get<InvocationRequest>(parsed);
    assert(req.id == "req-123");
    assert(req.command == "greet");
    assert(req.payload.at("name") == "Alice");
    std::cout << "[PASS] test_valid_invocation" << std::endl;
}

void test_omitted_payload() {
    std::string raw = R"({
        "version": 1,
        "type": "invoke",
        "id": "req-456",
        "command": "ping"
    })";

    auto parsed = ProtocolCodec::parse(raw);
    assert(std::holds_alternative<InvocationRequest>(parsed));

    const auto& req = std::get<InvocationRequest>(parsed);
    assert(req.id == "req-456");
    assert(req.command == "ping");
    assert(req.payload.is_object() && req.payload.empty());
    std::cout << "[PASS] test_omitted_payload" << std::endl;
}

void test_invalid_json() {
    std::string raw = "{ invalid json ";
    auto parsed = ProtocolCodec::parse(raw);
    assert(std::holds_alternative<ProtocolError>(parsed));

    const auto& err = std::get<ProtocolError>(parsed);
    assert(err.code == ErrorCode::invalid_json);
    assert(!err.id.has_value());
    std::cout << "[PASS] test_invalid_json" << std::endl;
}

void test_unsupported_version() {
    std::string raw = R"({
        "version": 2,
        "type": "invoke",
        "id": "req-1",
        "command": "greet"
    })";

    auto parsed = ProtocolCodec::parse(raw);
    assert(std::holds_alternative<ProtocolError>(parsed));

    const auto& err = std::get<ProtocolError>(parsed);
    assert(err.code == ErrorCode::unsupported_protocol_version);
    assert(err.id.has_value() && *err.id == "req-1");
    std::cout << "[PASS] test_unsupported_version" << std::endl;
}

void test_invalid_message_type() {
    std::string raw = R"({
        "version": 1,
        "type": "event",
        "id": "req-1",
        "command": "greet"
    })";

    auto parsed = ProtocolCodec::parse(raw);
    assert(std::holds_alternative<ProtocolError>(parsed));

    const auto& err = std::get<ProtocolError>(parsed);
    assert(err.code == ErrorCode::invalid_message_type);
    assert(err.id.has_value() && *err.id == "req-1");
    std::cout << "[PASS] test_invalid_message_type" << std::endl;
}

void test_missing_request_ids() {
    // 1. Missing ID
    std::string raw1 = R"({ "version": 1, "type": "invoke", "command": "greet" })";
    auto parsed1 = ProtocolCodec::parse(raw1);
    assert(std::holds_alternative<ProtocolError>(parsed1));
    assert(std::get<ProtocolError>(parsed1).code == ErrorCode::missing_request_id);

    // 2. Empty ID
    std::string raw2 = R"({ "version": 1, "type": "invoke", "id": "", "command": "greet" })";
    auto parsed2 = ProtocolCodec::parse(raw2);
    assert(std::holds_alternative<ProtocolError>(parsed2));
    assert(std::get<ProtocolError>(parsed2).code == ErrorCode::missing_request_id);

    // 3. Non-string ID
    std::string raw3 = R"({ "version": 1, "type": "invoke", "id": 12345, "command": "greet" })";
    auto parsed3 = ProtocolCodec::parse(raw3);
    assert(std::holds_alternative<ProtocolError>(parsed3));
    assert(std::get<ProtocolError>(parsed3).code == ErrorCode::missing_request_id);

    // 4. Over 128 bytes ID
    std::string long_id(129, 'x');
    std::string raw4 = "{\"version\":1,\"type\":\"invoke\",\"id\":\"" + long_id + "\",\"command\":\"greet\"}";
    auto parsed4 = ProtocolCodec::parse(raw4);
    assert(std::holds_alternative<ProtocolError>(parsed4));
    assert(std::get<ProtocolError>(parsed4).code == ErrorCode::missing_request_id);

    std::cout << "[PASS] test_missing_request_ids" << std::endl;
}

void test_invalid_payloads() {
    // Array payload
    std::string raw1 = R"({ "version": 1, "type": "invoke", "id": "req-1", "command": "greet", "payload": [1, 2, 3] })";
    auto parsed1 = ProtocolCodec::parse(raw1);
    assert(std::holds_alternative<ProtocolError>(parsed1));
    assert(std::get<ProtocolError>(parsed1).code == ErrorCode::invalid_payload);

    // String payload
    std::string raw2 = R"({ "version": 1, "type": "invoke", "id": "req-1", "command": "greet", "payload": "hello" })";
    auto parsed2 = ProtocolCodec::parse(raw2);
    assert(std::holds_alternative<ProtocolError>(parsed2));
    assert(std::get<ProtocolError>(parsed2).code == ErrorCode::invalid_payload);

    // Null payload
    std::string raw3 = R"({ "version": 1, "type": "invoke", "id": "req-1", "command": "greet", "payload": null })";
    auto parsed3 = ProtocolCodec::parse(raw3);
    assert(std::holds_alternative<ProtocolError>(parsed3));
    assert(std::get<ProtocolError>(parsed3).code == ErrorCode::invalid_payload);

    std::cout << "[PASS] test_invalid_payloads" << std::endl;
}

void test_serialization() {
    // Success result
    InvocationResult res1{
        .id = "req-99",
        .outcome = nlohmann::json{{"msg", "ok"}}
    };
    std::string json1 = ProtocolCodec::serialize_result(res1);
    auto p1 = nlohmann::json::parse(json1);
    assert(p1["version"] == 1);
    assert(p1["type"] == "result");
    assert(p1["id"] == "req-99");
    assert(p1["ok"] == true);
    assert(p1["value"]["msg"] == "ok");

    // Command error result
    InvocationResult res2{
        .id = "req-100",
        .outcome = CommandError{ ErrorCode::command_not_found, "No command named 'foo'" }
    };
    std::string json2 = ProtocolCodec::serialize_result(res2);
    auto p2 = nlohmann::json::parse(json2);
    assert(p2["version"] == 1);
    assert(p2["type"] == "result");
    assert(p2["id"] == "req-100");
    assert(p2["ok"] == false);
    assert(p2["error"]["code"] == "command_not_found");

    // Protocol error (no ID)
    ProtocolError pe1{ ErrorCode::invalid_json, "Bad JSON", std::nullopt };
    std::string json3 = ProtocolCodec::serialize_protocol_error(pe1);
    auto p3 = nlohmann::json::parse(json3);
    assert(p3["version"] == 1);
    assert(p3["type"] == "protocol_error");
    assert(p3["id"].is_null());
    assert(p3["error"]["code"] == "invalid_json");

    std::cout << "[PASS] test_serialization" << std::endl;
}

int main() {
    test_valid_invocation();
    test_omitted_payload();
    test_invalid_json();
    test_unsupported_version();
    test_invalid_message_type();
    test_missing_request_ids();
    test_invalid_payloads();
    test_serialization();
    std::cout << "ALL PROTOCOL TESTS PASSED." << std::endl;
    return 0;
}
