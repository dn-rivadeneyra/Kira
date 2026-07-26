#include "src/platform/windows/windows_app_host.hpp"

#include <iostream>
#include <utility>

namespace kira::platform {
namespace {

// These messages are consumed by WindowsAppHost::run_event_loop before they
// reach NativeWindow::WndProc. Native callbacks only enqueue terminal state and
// post one of these messages; they never synchronously invoke AppImpl callbacks.
constexpr UINT WM_KIRA_HOST_READY = WM_APP + 0x120;
constexpr UINT WM_KIRA_HOST_FATAL = WM_APP + 0x121;

} // namespace

WindowsAppHost::WindowsAppHost(const WindowConfig& config)
    : config_(config) {}

WindowsAppHost::~WindowsAppHost() {
    request_close();
}

bool WindowsAppHost::post_host_message(UINT message) {
    if (window_ && window_->get_hwnd()) {
        if (PostMessage(window_->get_hwnd(), message, 0, 0)) {
            return true;
        }

        std::cerr
            << "[Kira App Error] PostMessage(" << message
            << ") failed, err=" << GetLastError() << std::endl;
    }

    if (event_loop_thread_id_ != 0 &&
        PostThreadMessage(event_loop_thread_id_, message, 0, 0)) {
        return true;
    }

    std::cerr
        << "[Kira App Error] Unable to defer host terminal event "
        << message << ", err=" << GetLastError() << std::endl;
    return false;
}

bool WindowsAppHost::post_quit(int exit_code) {
    if (event_loop_thread_id_ == 0 ||
        GetCurrentThreadId() == event_loop_thread_id_) {
        // PostQuitMessage has no failure return value and targets the current
        // thread, which is the host event-loop thread in this branch.
        PostQuitMessage(exit_code);
        return true;
    }

    if (PostThreadMessage(
            event_loop_thread_id_,
            WM_QUIT,
            static_cast<WPARAM>(exit_code),
            0)) {
        return true;
    }

    const DWORD error = GetLastError();
    std::cerr
        << "[Kira App Error] PostThreadMessage(WM_QUIT) failed, err="
        << error << std::endl;
    return false;
}

void WindowsAppHost::defer_readiness(PlatformResult result) {
    if (readiness_reported_.exchange(true)) {
        return;
    }

    {
        std::lock_guard lock(terminal_result_mutex_);
        pending_readiness_.emplace(std::move(result));
    }

    if (!post_host_message(WM_KIRA_HOST_READY)) {
        // Do not invoke application callbacks from the native callback stack.
        // A quit message lets run_event_loop return; AppImpl then performs its
        // normal platform-independent shutdown path with a non-zero result.
        if (!post_quit(1)) {
            std::cerr
                << "[Kira App Error] Unable to terminate event loop after "
                   "readiness delivery failure."
                << std::endl;
        }
    }
}

void WindowsAppHost::defer_fatal(PlatformResult result) {
    if (fatal_reported_.exchange(true)) {
        return;
    }

    is_ready_.store(false);

    {
        std::lock_guard lock(terminal_result_mutex_);
        pending_fatal_.emplace(std::move(result));
    }

    if (!post_host_message(WM_KIRA_HOST_FATAL) && !post_quit(1)) {
        std::cerr
            << "[Kira App Error] Unable to terminate event loop after "
               "fatal-event delivery failure."
            << std::endl;
    }
}

void WindowsAppHost::deliver_readiness() {
    std::optional<PlatformResult> result;
    {
        std::lock_guard lock(terminal_result_mutex_);
        result = std::move(pending_readiness_);
        pending_readiness_.reset();
    }

    if (!result) {
        return;
    }

    is_ready_.store(result->ok());
    if (on_ready_) {
        on_ready_(std::move(*result));
    }
}

void WindowsAppHost::deliver_fatal() {
    std::optional<PlatformResult> result;
    {
        std::lock_guard lock(terminal_result_mutex_);
        result = std::move(pending_fatal_);
        pending_fatal_.reset();
    }

    if (!result) {
        return;
    }

    is_ready_.store(false);
    if (on_fatal_error_) {
        on_fatal_error_(std::move(*result));
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

    event_loop_thread_id_ = GetCurrentThreadId();

    // Ensure this thread has a message queue before any asynchronous WebView2
    // callback needs the PostThreadMessage fallback.
    MSG queue_probe{};
    PeekMessage(&queue_probe, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

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
            defer_readiness(success
                ? PlatformResult{}
                : PlatformResult{
                    PlatformError::bootstrap_failed,
                    "WebView2 initialization or navigation failed."
                });
        },
        [this]() {
            if (!post_host_message(WM_KIRA_APP_SHUTDOWN) &&
                !post_quit(0)) {
                std::cerr
                    << "[Kira App Error] Unable to deliver close request or "
                       "terminate the event loop."
                    << std::endl;
            }
        },
        [this](HRESULT hr, std::string diagnostic) {
            diagnostic += " HRESULT=" +
                std::to_string(static_cast<long long>(hr));
            defer_fatal(PlatformResult{
                PlatformError::security_failure,
                std::move(diagnostic)
            });
        }
    );

    if (!window_->initialize()) {
        defer_readiness(PlatformResult{
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

    is_ready_.store(false);
    if (window_) {
        window_->close();
        window_.reset();
    }

    if (!post_quit(0)) {
        std::cerr
            << "[Kira App Error] Native resources closed, but the host event "
               "loop could not be asked to quit."
            << std::endl;
    }
}

int WindowsAppHost::run_event_loop() {
    MSG msg{};
    while (true) {
        const BOOL result = GetMessage(&msg, nullptr, 0, 0);

        if (result > 0) {
            if (msg.message == WM_KIRA_HOST_READY) {
                deliver_readiness();
                continue;
            }

            if (msg.message == WM_KIRA_HOST_FATAL) {
                deliver_fatal();
                continue;
            }

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
        // This call originates in the host loop itself, not inside WndProc or a
        // WebView2 callback, so invoking the fatal callback here is not reentrant
        // with native object destruction.
        if (!fatal_reported_.exchange(true) && on_fatal_error_) {
            is_ready_.store(false);
            on_fatal_error_(PlatformResult{
                PlatformError::event_loop_failed,
                "GetMessage failed with Win32 error " + std::to_string(error)
            });
        }
        return -1;
    }
}

} // namespace kira::platform
