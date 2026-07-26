#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include "kira/app.hpp"

namespace kira {

struct TransportCallbackState {
    std::atomic_bool alive{true};
    DWORD ui_thread_id{0};
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    WindowConfig config;
    std::function<void(const std::string&)> on_message;
    std::wstring script_id;
};

class WebViewTransport {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using AttachCallback  = std::function<void(HRESULT)>;

    WebViewTransport(const WindowConfig& config, MessageCallback on_message);
    ~WebViewTransport();

    WebViewTransport(const WebViewTransport&) = delete;
    WebViewTransport& operator=(const WebViewTransport&) = delete;

    // Asynchronous transactional attach
    void attach(ICoreWebView2* webview, AttachCallback callback);
    void detach();

    // Posts raw UTF-8 string message back to JS context (must run on Win32 UI thread)
    bool send_message(const std::string& raw_utf8_message);

    bool is_ready() const {
        return webview_ != nullptr && ready_ && state_ && state_->alive.load();
    }

    bool is_ui_thread() const {
        return GetCurrentThreadId() == ui_thread_id_;
    }

private:
    WindowConfig config_;
    MessageCallback on_message_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken web_message_token_{};
    EventRegistrationToken nav_starting_token_{};

    std::shared_ptr<TransportCallbackState> state_;
    DWORD ui_thread_id_{0};
    bool ready_{false};
};

} // namespace kira
