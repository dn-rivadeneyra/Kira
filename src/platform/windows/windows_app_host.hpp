#pragma once

#include <windows.h>
#include <memory>
#include <atomic>
#include "kira/app.hpp"
#include "src/platform/app_host.hpp"
#include "src/platform/windows/win32_window.hpp"

namespace kira::platform {

class WindowsAppHost : public AppHost {
public:
    explicit WindowsAppHost(const WindowConfig& config);
    ~WindowsAppHost() override;

    WindowsAppHost(const WindowsAppHost&) = delete;
    WindowsAppHost& operator=(const WindowsAppHost&) = delete;

    void start(
        RawMessageCallback on_message,
        ReadyCallback on_ready,
        CloseRequestedCallback on_close_requested,
        FatalErrorCallback on_fatal_error
    ) override;

    bool post_message(std::string message) override;

    void show() override;

    void request_close() override;

    int run_event_loop() override;

private:
    WindowConfig config_;
    RawMessageCallback on_message_;
    ReadyCallback on_ready_;
    CloseRequestedCallback on_close_requested_;
    FatalErrorCallback on_fatal_error_;

    std::unique_ptr<NativeWindow> window_;
    std::atomic<bool> is_started_{false};
    std::atomic<bool> is_ready_{false};
    std::atomic<bool> is_closed_{false};
};

} // namespace kira::platform
