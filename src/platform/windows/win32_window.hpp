#pragma once

#include "kira/window.hpp"
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>

namespace kira {

class NativeWindowImpl {
public:
    explicit NativeWindowImpl(const WindowConfig& config);
    ~NativeWindowImpl();

    bool initialize();
    void show();
    void post_web_message(const std::string& message);
    void set_on_web_message(NativeWindow::MessageCallback callback);
    HWND get_hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void resize_webview();
    void navigate_to_target();
    void inject_kira_ipc_script();

    WindowConfig config_;
    HWND hwnd_ = nullptr;
    NativeWindow::MessageCallback on_web_message_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken web_message_token_{};
};

} // namespace kira
