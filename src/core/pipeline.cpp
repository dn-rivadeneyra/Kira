#include "src/core/pipeline.hpp"

namespace kira {

InvocationPipeline::InvocationPipeline(CommandRegistry& registry, WorkerExecutor& worker, ResponseCallback response_callback)
    : registry_(registry), worker_(worker), response_callback_(std::move(response_callback)) {}

void InvocationPipeline::shutdown() {
    shutting_down_ = true;
    ready_ = false;
}

void InvocationPipeline::process_raw_message(const std::string& raw_utf8_message) {
    if (!ready_ || shutting_down_) {
        return;
    }

    // 1. Parse raw UTF-8 string through ProtocolCodec
    auto parse_res = ProtocolCodec::parse(raw_utf8_message);

    // 2. If protocol error -> serialize and deliver protocol error immediately
    if (std::holds_alternative<ProtocolError>(parse_res)) {
        const auto& proto_err = std::get<ProtocolError>(parse_res);
        std::string err_json = ProtocolCodec::serialize_protocol_error(proto_err);
        if (ready_ && !shutting_down_ && response_callback_) {
            response_callback_(err_json);
        }
        return;
    }

    auto req = std::get<InvocationRequest>(parse_res);

    // 3. Enqueue invocation request to WorkerExecutor
    bool enqueued = worker_.enqueue([this, req]() {
        if (!ready_ || shutting_down_) {
            return;
        }

        // Execute command
        InvocationResult result = CommandExecutor::execute(req, registry_);

        // Serialize result
        std::string resp_json = ProtocolCodec::serialize_result(result);

        // Deliver result via response callback
        if (ready_ && !shutting_down_ && response_callback_) {
            response_callback_(resp_json);
        }
    });

    if (!enqueued) {
        // Enqueue failed because worker is shutting down -> silently discard
        return;
    }
}

} // namespace kira
