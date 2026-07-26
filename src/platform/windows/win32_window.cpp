#include "win32_window.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <shlwapi.h>

namespace kira {

// Helper string conversion functions
static std::wstring string_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

static std::string wstring_to_string(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

// NativeWindow implementation wrapper
NativeWindow::NativeWindow(const WindowConfig& config)
    : impl_(std::make_unique<NativeWindowImpl>(config)) {}

NativeWindow::~NativeWindow() = default;

bool NativeWindow::initialize() { return impl_->initialize(); }
void NativeWindow::show() { impl_->show(); }
void NativeWindow::post_web_message(const std::string& message) { impl_->post_web_message(message); }
void NativeWindow::set_on_web_message(MessageCallback callback) { impl_->set_on_web_message(std::move(callback)); }
void* NativeWindow::get_native_handle() const { return static_cast<void*>(impl_->get_hwnd()); }

// NativeWindowImpl Win32 + WebView2 implementation
NativeWindowImpl::NativeWindowImpl(const WindowConfig& config)
    : config_(config) {}

NativeWindowImpl::~NativeWindowImpl() {
    if (webview_ && web_message_token_.value != 0) {
        webview_->remove_WebMessageReceived(web_message_token_);
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK NativeWindowImpl::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NativeWindowImpl* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<NativeWindowImpl*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NativeWindowImpl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) {
        switch (uMsg) {
        case WM_SIZE:
            self->resize_webview();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool NativeWindowImpl::initialize() {
    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"KiraWindowClass";

    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config_.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rc = { 0, 0, config_.width, config_.height };
    AdjustWindowRect(&rc, style, FALSE);

    std::wstring wtitle = string_to_wstring(config_.title);

    hwnd_ = CreateWindowExW(
        0,
        L"KiraWindowClass",
        wtitle.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        NULL,
        NULL,
        hInstance,
        this
    );

    if (!hwnd_) {
        std::cerr << "[Kira] Failed to create Win32 window." << std::endl;
        return false;
    }

    // Initialize WebView2 Environment
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    std::cerr << "[Kira] CreateCoreWebView2EnvironmentWithOptions failed." << std::endl;
                    return result;
                }

                env->CreateCoreWebView2Controller(
                    hwnd_,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(res) || !controller) {
                                std::cerr << "[Kira] CreateCoreWebView2Controller failed." << std::endl;
                                return res;
                            }

                            webview_controller_ = controller;
                            webview_controller_->get_CoreWebView2(&webview_);

                            resize_webview();
                            inject_kira_ipc_script();

                            // Wire message handler
                            webview_->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        PWSTR message_raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&message_raw)) && message_raw) {
                                            std::string msg = wstring_to_string(message_raw);
                                            CoTaskMemFree(message_raw);

                                            if (on_web_message_) {
                                                on_web_message_(msg);
                                            }
                                        }
                                        return S_OK;
                                    }).Get(),
                                &web_message_token_
                            );

                            navigate_to_target();
                            return S_OK;
                        }).Get()
                );
                return S_OK;
            }).Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[Kira] WebView2 environment creation request failed, hr=0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

void NativeWindowImpl::resize_webview() {
    if (webview_controller_ && hwnd_) {
        RECT bounds;
        GetClientRect(hwnd_, &bounds);
        webview_controller_->put_Bounds(bounds);
    }
}

void NativeWindowImpl::inject_kira_ipc_script() {
    if (!webview_) return;

    std::wstring ipc_js = L"(function() {\n"
                          L"    if (window.invoke) return;\n"
                          L"    const pendingPromises = new Map();\n"
                          L"    let reqCounter = 0;\n"
                          L"    window.invoke = function(command, args = {}) {\n"
                          L"        return new Promise((resolve, reject) => {\n"
                          L"            const id = 'kira_req_' + (++reqCounter) + '_' + Date.now() + '_' + Math.random().toString(36).substr(2, 5);\n"
                          L"            pendingPromises.set(id, { resolve, reject });\n"
                          L"            const payload = JSON.stringify({ id, command, args });\n"
                          L"            window.chrome.webview.postMessage(payload);\n"
                          L"        });\n"
                          L"    };\n"
                          L"    window.chrome.webview.addEventListener('message', function(event) {\n"
                          L"        try {\n"
                          L"            const data = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;\n"
                          L"            if (data && data.id && pendingPromises.has(data.id)) {\n"
                          L"                const { resolve, reject } = pendingPromises.get(data.id);\n"
                          L"                pendingPromises.delete(data.id);\n"
                          L"                if (data.status === 'ok') {\n"
                          L"                    resolve(data.result);\n"
                          L"                } else {\n"
                          L"                    reject(new Error(data.error || 'IPC Error'));\n"
                          L"                }\n"
                          L"            }\n"
                          L"        } catch (e) {\n"
                          L"            console.error('[Kira IPC] Error parsing IPC response:', e);\n"
                          L"        }\n"
                          L"    });\n"
                          L"})();";

    webview_->AddScriptToExecuteOnDocumentCreated(ipc_js.c_str(), nullptr);
}

void NativeWindowImpl::navigate_to_target() {
    if (!webview_) return;

    if (!config_.dev_url.empty()) {
        std::wstring wurl = string_to_wstring(config_.dev_url);
        webview_->Navigate(wurl.c_str());
        return;
    }

    std::filesystem::path html_path;
    if (!config_.asset_path.empty()) {
        html_path = std::filesystem::absolute(config_.asset_path);
    } else {
        // Default to assets/index.html next to executable
        wchar_t exe_path_buf[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path_buf, MAX_PATH);
        std::filesystem::path exe_dir = std::filesystem::path(exe_path_buf).parent_path();
        html_path = exe_dir / "assets" / "index.html";
    }

    if (std::filesystem::exists(html_path)) {
        std::wstring file_url = L"file:///" + html_path.wstring();
        webview_->Navigate(file_url.c_str());
    } else {
        std::string fallback_html = "<html><body style='font-family:sans-serif;padding:2rem;background:#1e1e2e;color:#cdd6f4;'>"
                                      "<h2>Kira Application Framework</h2>"
                                      "<p>Asset not found at: " + html_path.string() + "</p>"
                                      "</body></html>";
        webview_->NavigateToString(string_to_wstring(fallback_html).c_str());
    }
}

void NativeWindowImpl::post_web_message(const std::string& message) {
    if (webview_) {
        std::wstring wmsg = string_to_wstring(message);
        webview_->PostWebMessageAsString(wmsg.c_str());
    }
}

void NativeWindowImpl::set_on_web_message(NativeWindow::MessageCallback callback) {
    on_web_message_ = std::move(callback);
}

void NativeWindowImpl::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

} // namespace kira
