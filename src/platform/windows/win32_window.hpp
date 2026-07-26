#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <memory>
#include <functional>
#include "kira/app.hpp"
#include "src/platform/windows/webview_transport.hpp"

namespace kira {

constexpr UINT WM_KIRA_IPC_RESPONSE  = WM_USER + 100;
constexpr UINT WM_KIRA_INIT_COMPLETE = WM_USER + 101;

class NativeWindow {
public:
    using RawMessageCallback = std::function<void(const std::string&)>;
    using InitCallback       = std::function<void(bool success)>;

    NativeWindow(const WindowConfig& config, RawMessageCallback on_message, InitCallback on_init);
    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool initialize();
    void show();
    void hide();
    void close();

    // Safely posts IPC response to Win32 UI thread message queue
    void post_ui_response(const std::string& response_json);

    WebViewTransport* transport() { return transport_.get(); }
    HWND get_hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void resize_webview();
    void navigate();

    WindowConfig config_;
    RawMessageCallback on_message_;
    InitCallback on_init_;
    HWND hwnd_{nullptr};

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    std::unique_ptr<WebViewTransport> transport_;
};

} // namespace kira
