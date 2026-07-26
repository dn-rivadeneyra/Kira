#pragma once

#include <string>
#include <functional>
#include <atomic>
#include "src/core/types.hpp"
#include "src/core/registry.hpp"
#include "src/core/executor.hpp"
#include "src/core/worker.hpp"
#include "src/core/protocol.hpp"

namespace kira {

using ResponseCallback = std::function<void(const std::string&)>;

class InvocationPipeline {
public:
    InvocationPipeline(CommandRegistry& registry, WorkerExecutor& worker, ResponseCallback response_callback);
    ~InvocationPipeline() = default;

    InvocationPipeline(const InvocationPipeline&) = delete;
    InvocationPipeline& operator=(const InvocationPipeline&) = delete;

    // Process raw UTF-8 message string
    void process_raw_message(const std::string& raw_utf8_message);

    // Controls readiness/accepting status
    void set_ready(bool ready) { ready_ = ready; }
    bool is_ready() const { return ready_; }

    void shutdown();

private:
    CommandRegistry& registry_;
    WorkerExecutor& worker_;
    ResponseCallback response_callback_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> shutting_down_{false};
};

} // namespace kira
