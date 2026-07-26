#pragma once

#include "src/core/types.hpp"
#include <string>
#include <variant>

namespace kira {

class ProtocolCodec {
public:
    static std::variant<InvocationRequest, ProtocolError> parse(const std::string& raw_message);
    static std::string serialize_result(const InvocationResult& result);
    static std::string serialize_protocol_error(const ProtocolError& error);
    static std::string error_code_to_string(ErrorCode code);
};

} // namespace kira
