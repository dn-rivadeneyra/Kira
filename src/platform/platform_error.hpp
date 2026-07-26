#pragma once

#include <string>

namespace kira::platform {

enum class PlatformError {
    none,
    window_creation_failed,
    webview_unavailable,
    bootstrap_failed,
    navigation_failed,
    security_failure,
    message_delivery_failed,
    event_loop_failed,
    shutdown_failed,
    internal_error
};

struct PlatformResult {
    PlatformError code{PlatformError::none};
    std::string diagnostic;

    bool ok() const {
        return code == PlatformError::none;
    }
};

} // namespace kira::platform
