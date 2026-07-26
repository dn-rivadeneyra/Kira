#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
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

struct WindowCallbackState {
    std::atomic_bool alive{true};
    DWORD ui_thread_id{0};
    HWND hwnd{nullptr};
    std::function<void(bool success)> on_init;
    std::function<void()> on_close_requested;
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

    InitializationGate& init_gate() { return init_gate_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void resize_webview();
    void drain_response_queue();
    void complete_initialization(bool success);

    WindowConfig config_;
    RawMessageCallback on_message_;
    InitCallback on_init_;
    CloseRequestedCallback on_close_requested_;
    HWND hwnd_{nullptr};

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    std::unique_ptr<WebViewTransport> transport_;

    EventRegistrationToken nav_completed_token_{};
    std::shared_ptr<WindowCallbackState> state_;
    DWORD ui_thread_id_{0};

    InitializationGate init_gate_;
    std::atomic<bool> closed_{false};

    // Instance-owned response queue and mutex
    std::mutex response_mutex_;
    std::vector<std::string> response_queue_;
};

} // namespace kira
