#include "src/platform/windows/windows_app_host.hpp"
#include <iostream>

namespace kira::platform {

WindowsAppHost::WindowsAppHost(const WindowConfig& config)
    : config_(config) {}

WindowsAppHost::~WindowsAppHost() {
    request_close();
}

void WindowsAppHost::complete_readiness(PlatformResult result) {
    if (readiness_reported_.exchange(true)) {
        return;
    }

    is_ready_.store(result.ok());
    if (on_ready_) {
        on_ready_(std::move(result));
    }
}

void WindowsAppHost::report_fatal(PlatformResult result) {
    if (fatal_reported_.exchange(true)) {
        return;
    }

    is_ready_.store(false);
    if (on_fatal_error_) {
        on_fatal_error_(std::move(result));
    }
}

void WindowsAppHost::start(
    RawMessageCallback on_message,
    ReadyCallback on_ready,
    CloseRequestedCallback on_close_requested,
    FatalErrorCallback on_fatal_error
) {
    if (is_started_.exchange(true)) {
        return;
    }

    on_message_ = std::move(on_message);
    on_ready_ = std::move(on_ready);
    on_close_requested_ = std::move(on_close_requested);
    on_fatal_error_ = std::move(on_fatal_error);

    window_ = std::make_unique<NativeWindow>(
        config_,
        [this](const std::string& raw_msg) {
            if (on_message_) {
                on_message_(raw_msg);
            }
        },
        [this](bool success) {
            complete_readiness(success
                ? PlatformResult{}
                : PlatformResult{
                    PlatformError::bootstrap_failed,
                    "WebView2 initialization or navigation failed."
                });
        },
        [this]() {
            if (window_ && window_->get_hwnd()) {
                BOOL posted = PostMessage(window_->get_hwnd(), WM_KIRA_APP_SHUTDOWN, 0, 0);
                if (!posted) {
                    std::cerr << "[Kira App Error] PostMessage(WM_KIRA_APP_SHUTDOWN) failed, err=" << GetLastError() << std::endl;
                    PostThreadMessage(GetCurrentThreadId(), WM_KIRA_APP_SHUTDOWN, 0, 0);
                }
            } else {
                PostThreadMessage(GetCurrentThreadId(), WM_KIRA_APP_SHUTDOWN, 0, 0);
            }
        },
        [this](HRESULT hr, std::string diagnostic) {
            diagnostic += " HRESULT=" + std::to_string(static_cast<long long>(hr));
            report_fatal(PlatformResult{
                PlatformError::security_failure,
                std::move(diagnostic)
            });
        }
    );

    if (!window_->initialize()) {
        complete_readiness(PlatformResult{
            PlatformError::window_creation_failed,
            "Native window initialization failed."
        });
    }
}

bool WindowsAppHost::post_message(std::string message) {
    if (is_closed_ || !is_ready_ || !window_) {
        return false;
    }
    window_->post_ui_response(message);
    return true;
}

void WindowsAppHost::show() {
    if (window_ && is_ready_ && !is_closed_) {
        window_->show();
    }
}

void WindowsAppHost::request_close() {
    if (is_closed_.exchange(true)) {
        return;
    }
    is_ready_ = false;
    if (window_) {
        window_->close();
        window_.reset();
    }
    PostQuitMessage(0);
}

int WindowsAppHost::run_event_loop() {
    MSG msg{};
    while (true) {
        const BOOL result = GetMessage(&msg, nullptr, 0, 0);

        if (result > 0) {
            if (msg.message == WM_KIRA_APP_SHUTDOWN) {
                if (on_close_requested_) {
                    on_close_requested_();
                }
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (result == 0) {
            return static_cast<int>(msg.wParam);
        }

        const DWORD error = GetLastError();
        report_fatal(PlatformResult{
            PlatformError::event_loop_failed,
            "GetMessage failed with Win32 error " + std::to_string(error)
        });
        return -1;
    }
}

} // namespace kira::platform
