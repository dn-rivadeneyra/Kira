#pragma once

#include <functional>
#include <string>
#include "src/platform/platform_error.hpp"

namespace kira::platform {

class AppHost {
public:
    using RawMessageCallback = std::function<void(std::string)>;
    using ReadyCallback = std::function<void(PlatformResult)>;
    using CloseRequestedCallback = std::function<void()>;
    using FatalErrorCallback = std::function<void(PlatformResult)>;

    virtual ~AppHost() = default;

    virtual void start(
        RawMessageCallback on_message,
        ReadyCallback on_ready,
        CloseRequestedCallback on_close_requested,
        FatalErrorCallback on_fatal_error
    ) = 0;

    virtual bool post_message(std::string message) = 0;

    virtual void show() = 0;

    virtual void request_close() = 0;

    virtual int run_event_loop() = 0;
};

} // namespace kira::platform
