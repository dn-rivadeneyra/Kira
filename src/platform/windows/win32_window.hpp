#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include "kira/app.hpp"
#include "src/core/gates.hpp"
#include "src/platform/windows/webview_transport.hpp"

namespace kira {

constexpr UINT WM_KIRA_IPC_RESPONSE   = WM_USER + 100;
constexpr UINT WM_KIRA_INIT_COMPLETE  = WM_USER + 101;
constexpr UINT WM_KIRA_APP_SHUTDOWN   = WM_USER + 102;

struct WindowAsyncState {
    std::atomic_bool alive{true};
    DWORD ui_thread_id{0};
    HWND hwnd{nullptr};
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    WindowConfig config;
    std::function<void(bool success)> on_init;
    std::function<void()> on_close_requested;
    InitializationGate init_gate;
    EventRegistrationToken nav_completed_token{};
    std::atomic<bool> closed{false};
    std::atomic<bool> security_failure_reported{false};

    void complete_initialization(bool success) {
        if (!init_gate.complete(success)) {
            return; // First completion call wins; subsequent calls ignored
        }

        // Unsubscribe initialization NavigationCompleted handler
        if (webview && nav_completed_token.value != 0) {
            HRESULT hr = webview->remove_NavigationCompleted(nav_completed_token);
            if (FAILED(hr)) {
                std::cerr << "[Kira Window Error] remove_NavigationCompleted failed, hr=0x" << std::hex << hr << std::endl;
            }
            nav_completed_token.value = 0;
        }

        if (hwnd) {
            BOOL posted = PostMessage(hwnd, WM_KIRA_INIT_COMPLETE, success ? 1 : 0, 0);
            if (!posted) {
                std::cerr << "[Kira Window Error] PostMessage(WM_KIRA_INIT_COMPLETE) failed, err=" << GetLastError() << std::endl;
                // Fallback thread message if window PostMessage fails (no synchronous re-entrancy)
                PostThreadMessage(ui_thread_id, WM_KIRA_INIT_COMPLETE, success ? 1 : 0, 0);
            }
        } else if (ui_thread_id != 0) {
            PostThreadMessage(ui_thread_id, WM_KIRA_INIT_COMPLETE, success ? 1 : 0, 0);
        }
    }
};

class NativeWindow {
public:
    using RawMessageCallback     = std::function<void(const std::string&)>;
    using InitCallback           = std::function<void(bool success)>;
    using CloseRequestedCallback = std::function<void()>;

    NativeWindow(
        const WindowConfig& config,
        RawMessageCallback on_message,
        InitCallback on_init,
        CloseRequestedCallback on_close_requested
    );

    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool initialize();
    void show();
    void hide();
    void close();

    HRESULT navigate();

    // Instance response queue posting (no global state)
    void post_ui_response(const std::string& response_json);

    WebViewTransport* transport() { return transport_.get(); }
    HWND get_hwnd() const { return hwnd_; }

    bool is_ui_thread() const {
        return GetCurrentThreadId() == ui_thread_id_;
    }

    InitializationGate& init_gate() {
        return win_state_->init_gate;
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void resize_webview();
    void drain_response_queue();
    bool check_ui_thread() const;

    WindowConfig config_;
    RawMessageCallback on_message_;
    InitCallback on_init_;
    CloseRequestedCallback on_close_requested_;
    HWND hwnd_{nullptr};

    std::shared_ptr<WebViewTransport> transport_;

    std::shared_ptr<WindowAsyncState> win_state_;
    DWORD ui_thread_id_{0};

    // Instance-owned response queue and mutex
    std::mutex response_mutex_;
    std::vector<std::string> response_queue_;
};

} // namespace kira
