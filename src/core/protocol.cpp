#include "src/core/protocol.hpp"
#include "src/core/validator.hpp"

namespace kira {

std::string ProtocolCodec::error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::invalid_json: return "invalid_json";
        case ErrorCode::unsupported_protocol_version: return "unsupported_protocol_version";
        case ErrorCode::invalid_message_type: return "invalid_message_type";
        case ErrorCode::missing_request_id: return "missing_request_id";
        case ErrorCode::invalid_command_name: return "invalid_command_name";
        case ErrorCode::invalid_payload: return "invalid_payload";
        case ErrorCode::command_not_found: return "command_not_found";
        case ErrorCode::command_exception: return "command_exception";
        case ErrorCode::internal_error: return "internal_error";
    }
    return "internal_error";
}

static std::optional<std::string> extract_raw_id(const nlohmann::json& msg) {
    if (msg.contains("id") && msg["id"].is_string()) {
        std::string id = msg["id"].get<std::string>();
        if (!id.empty() && id.size() <= 128) {
            return id;
        }
    }
    return std::nullopt;
}

std::variant<InvocationRequest, ProtocolError> ProtocolCodec::parse(const std::string& raw_message) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(raw_message);
    } catch (const std::exception&) {
        return ProtocolError{
            ErrorCode::invalid_json,
            "The message is not valid JSON",
            std::nullopt
        };
    }

    if (!msg.is_object()) {
        return ProtocolError{
            ErrorCode::invalid_json,
            "The message must be a JSON object",
            std::nullopt
        };
    }

    // Extract ID first if valid
    std::optional<std::string> req_id = extract_raw_id(msg);

    // 1. Validate version
    if (!msg.contains("version") || !msg["version"].is_number_integer() || msg["version"].get<int>() != 1) {
        return ProtocolError{
            ErrorCode::unsupported_protocol_version,
            "Unsupported protocol version",
            req_id
        };
    }

    // 2. Validate type
    if (!msg.contains("type") || !msg["type"].is_string() || msg["type"].get<std::string>() != "invoke") {
        return ProtocolError{
            ErrorCode::invalid_message_type,
            "Invalid message type",
            req_id
        };
    }

    // 3. Validate request ID
    if (!req_id.has_value()) {
        return ProtocolError{
            ErrorCode::missing_request_id,
            "Missing or invalid request ID",
            std::nullopt
        };
    }

    // 4. Validate command name
    if (!msg.contains("command") || !msg["command"].is_string()) {
        return ProtocolError{
            ErrorCode::invalid_command_name,
            "Missing or invalid command name",
            req_id
        };
    }

    std::string command = msg["command"].get<std::string>();
    if (!is_valid_command_name(command)) {
        return ProtocolError{
            ErrorCode::invalid_command_name,
            "Invalid command name grammar",
            req_id
        };
    }

    // 5. Validate payload
    nlohmann::json payload = nlohmann::json::object();
    if (msg.contains("payload")) {
        if (!msg["payload"].is_object()) {
            return ProtocolError{
                ErrorCode::invalid_payload,
                "Payload must be a JSON object",
                req_id
            };
        }
        payload = msg["payload"];
    }

    return InvocationRequest{
        .id = *req_id,
        .command = std::move(command),
        .payload = std::move(payload)
    };
}

std::string ProtocolCodec::serialize_result(const InvocationResult& result) {
    nlohmann::json out;
    out["version"] = 1;
    out["type"] = "result";
    out["id"] = result.id;

    if (std::holds_alternative<nlohmann::json>(result.outcome)) {
        out["ok"] = true;
        out["value"] = std::get<nlohmann::json>(result.outcome);
    } else {
        const auto& err = std::get<CommandError>(result.outcome);
        out["ok"] = false;
        out["error"] = {
            {"code", error_code_to_string(err.code)},
            {"message", err.message}
        };
    }

    return out.dump();
}

std::string ProtocolCodec::serialize_protocol_error(const ProtocolError& error) {
    nlohmann::json out;
    out["version"] = 1;
    out["type"] = "protocol_error";

    if (error.id.has_value()) {
        out["id"] = *error.id;
    } else {
        out["id"] = nullptr;
    }

    out["error"] = {
        {"code", error_code_to_string(error.code)},
        {"message", error.message}
    };

    return out.dump();
}

} // namespace kira
