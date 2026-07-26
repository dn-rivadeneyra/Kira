#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

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
    void defer_readiness(PlatformResult result);
    void defer_fatal(PlatformResult result);
    void deliver_readiness();
    void deliver_fatal();
    bool post_host_message(UINT message);
    bool post_quit(int exit_code);

    WindowConfig config_;
    RawMessageCallback on_message_;
    ReadyCallback on_ready_;
    CloseRequestedCallback on_close_requested_;
    FatalErrorCallback on_fatal_error_;

    std::unique_ptr<NativeWindow> window_;
    std::atomic<bool> is_started_{false};
    std::atomic<bool> is_ready_{false};
    std::atomic<bool> is_closed_{false};
    std::atomic<bool> readiness_reported_{false};
    std::atomic<bool> fatal_reported_{false};

    DWORD event_loop_thread_id_{0};
    std::mutex terminal_result_mutex_;
    std::optional<PlatformResult> pending_readiness_;
    std::optional<PlatformResult> pending_fatal_;
};

} // namespace kira::platform
