#include "src/core/executor.hpp"
#include <iostream>

namespace kira {

static void log_internal_debug(const std::string& message) {
    #ifndef NDEBUG
    std::cerr << "[Kira Debug] " << message << std::endl;
    #else
    (void)message;
    #endif
}

InvocationResult CommandExecutor::execute(const InvocationRequest& request, const CommandRegistry& registry) {
    auto handler_opt = registry.get_command(request.command);
    if (!handler_opt.has_value()) {
        return InvocationResult{
            .id = request.id,
            .outcome = CommandError{
                .code = ErrorCode::command_not_found,
                .message = "No command named '" + request.command + "'"
            }
        };
    }

    try {
        nlohmann::json val = (*handler_opt)(request.payload);
        return InvocationResult{
            .id = request.id,
            .outcome = val
        };
    } catch (const std::exception& e) {
        log_internal_debug("Exception executing command '" + request.command + "': " + e.what());
        return InvocationResult{
            .id = request.id,
            .outcome = CommandError{
                .code = ErrorCode::command_exception,
                .message = "The native command failed"
            }
        };
    } catch (...) {
        log_internal_debug("Unknown exception executing command '" + request.command + "'");
        return InvocationResult{
            .id = request.id,
            .outcome = CommandError{
                .code = ErrorCode::command_exception,
                .message = "The native command failed"
            }
        };
    }
}

} // namespace kira
