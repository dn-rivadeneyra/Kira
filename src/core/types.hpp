#pragma once

#include <string>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace kira {

enum class ErrorCode {
    invalid_json,
    unsupported_protocol_version,
    invalid_message_type,
    missing_request_id,
    invalid_command_name,
    invalid_payload,
    command_not_found,
    command_exception,
    internal_error,
    request_timeout
};

struct CommandError {
    ErrorCode code;
    std::string message;
};

struct InvocationRequest {
    std::string id;
    std::string command;
    nlohmann::json payload;
};

using InvocationOutcome = std::variant<nlohmann::json, CommandError>;

struct InvocationResult {
    std::string id;
    InvocationOutcome outcome;
};

struct ProtocolError {
    ErrorCode code;
    std::string message;
    std::optional<std::string> id;
};

enum class ReadinessState {
    created,
    initializing,
    ready,
    failed,
    closing,
    closed
};

} // namespace kira
