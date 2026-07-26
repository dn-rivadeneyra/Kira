#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "kira/app.hpp"

namespace kira {

using AttachCallback = std::function<void(HRESULT)>;
using TransportFailureCallback = std::function<void(HRESULT)>;

struct AttachOperationState {
    std::atomic_bool alive{true};
    std::atomic_bool ready{false};
    std::atomic_bool attach_completed{false};

    DWORD ui_thread_id{0};
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    WindowConfig config;

    std::function<void(const std::string&)> on_message;
    TransportFailureCallback on_security_failure;
    AttachCallback attach_callback;

    EventRegistrationToken nav_starting_token{};
    EventRegistrationToken web_message_token{};
    std::wstring script_id;

    void complete_attach(HRESULT hr) {
        if (attach_completed.exchange(true)) {
            return;
        }

        ready.store(SUCCEEDED(hr));

        auto callback = std::move(attach_callback);
        attach_callback = nullptr;
        if (callback) {
            callback(hr);
        }
    }
};

class WebViewTransport {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    WebViewTransport(
        const WindowConfig& config,
        MessageCallback on_message,
        TransportFailureCallback on_security_failure = {}
    );

    ~WebViewTransport();

    WebViewTransport(const WebViewTransport&) = delete;
    WebViewTransport& operator=(const WebViewTransport&) = delete;

    void attach(ICoreWebView2* webview, AttachCallback callback);
    void detach();

    bool send_message(const std::string& raw_utf8_message);

    bool is_ready() const {
        const auto state = state_;
        return state &&
               state->alive.load() &&
               state->ready.load() &&
               state->webview;
    }

    bool is_ui_thread() const {
        return GetCurrentThreadId() == ui_thread_id_;
    }

private:
    static void cleanup_state(const std::shared_ptr<AttachOperationState>& state);
    bool check_ui_thread() const;

    WindowConfig config_;
    MessageCallback on_message_;
    TransportFailureCallback on_security_failure_;

    std::shared_ptr<AttachOperationState> state_;
    DWORD ui_thread_id_{0};
};

} // namespace kira
