#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include "kira/app.hpp"

namespace kira {

using AttachCallback = std::function<void(HRESULT)>;

struct AttachOperationState {
    std::atomic_bool alive{true};
    DWORD ui_thread_id{0};
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    WindowConfig config;
    std::function<void(const std::string&)> on_message;
    EventRegistrationToken nav_starting_token{};
    EventRegistrationToken web_message_token{};
    std::wstring script_id;
    AttachCallback callback;
    std::atomic<bool> attach_completed{false};

    void complete_attach(HRESULT hr) {
        if (!attach_completed.exchange(true)) {
            if (callback) {
                callback(hr);
            }
        }
    }
};

class WebViewTransport {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    WebViewTransport(const WindowConfig& config, MessageCallback on_message);
    ~WebViewTransport();

    WebViewTransport(const WebViewTransport&) = delete;
    WebViewTransport& operator=(const WebViewTransport&) = delete;

    // Asynchronous transactional attach
    void attach(ICoreWebView2* webview, AttachCallback callback);
    void detach();

    // Posts raw UTF-8 string message back to JS context
    bool send_message(const std::string& raw_utf8_message);

    bool is_ready() const {
        return webview_ != nullptr && ready_ && op_state_ && op_state_->alive.load();
    }

    bool is_ui_thread() const {
        return GetCurrentThreadId() == ui_thread_id_;
    }

private:
    bool check_ui_thread() const;

    WindowConfig config_;
    MessageCallback on_message_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken web_message_token_{};
    EventRegistrationToken nav_starting_token_{};

    std::shared_ptr<AttachOperationState> op_state_;
    DWORD ui_thread_id_{0};
    bool ready_{false};
};

} // namespace kira
