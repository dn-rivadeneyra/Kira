#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <functional>
#include <memory>
#include "kira/app.hpp"

namespace kira {

class WebViewTransport {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    WebViewTransport(const WindowConfig& config, MessageCallback on_message);
    ~WebViewTransport();

    WebViewTransport(const WebViewTransport&) = delete;
    WebViewTransport& operator=(const WebViewTransport&) = delete;

    bool attach(ICoreWebView2* webview);
    void detach();

    // Posts raw UTF-8 string message back to JS context (must run on Win32 UI thread)
    bool send_message(const std::string& raw_utf8_message);

    bool is_ready() const { return webview_ != nullptr && ready_ && *alive_token_; }

private:
    void inject_native_bootstrap();
    void setup_navigation_security();

    WindowConfig config_;
    MessageCallback on_message_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken web_message_token_{};
    EventRegistrationToken nav_starting_token_{};

    // Shared lifetime token for safe asynchronous callbacks
    std::shared_ptr<bool> alive_token_;
    bool ready_{false};
};

} // namespace kira
