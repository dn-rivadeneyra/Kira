#include "src/platform/windows/windows_app_host.hpp"
#include <iostream>

namespace kira::platform {

WindowsAppHost::WindowsAppHost(const WindowConfig& config)
    : config_(config) {}

WindowsAppHost::~WindowsAppHost() {
    request_close();
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
            if (success) {
                is_ready_ = true;
                if (on_ready_) {
                    on_ready_(PlatformResult{});
                }
            } else {
                if (on_ready_) {
                    on_ready_(PlatformResult{
                        PlatformError::bootstrap_failed,
                        "WebView2 initialization or navigation failed."
                    });
                }
            }
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
        }
    );

    if (!window_->initialize()) {
        if (on_ready_) {
            on_ready_(PlatformResult{
                PlatformError::window_creation_failed,
                "Native window initialization failed."
            });
        }
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
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KIRA_APP_SHUTDOWN) {
            if (on_close_requested_) {
                on_close_requested_();
            }
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace kira::platform
