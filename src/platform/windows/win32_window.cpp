#include "src/platform/windows/win32_window.hpp"
#include "src/platform/windows/security.hpp"
#include <iostream>
#include <vector>
#include <mutex>

namespace kira {

static std::wstring string_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

// Queue for pending IPC responses posted to Win32 UI thread
static std::mutex g_response_queue_mutex;
static std::vector<std::string> g_pending_responses;

NativeWindow::NativeWindow(const WindowConfig& config, RawMessageCallback on_message, InitCallback on_init)
    : config_(config), on_message_(std::move(on_message)), on_init_(std::move(on_init)) {}

NativeWindow::~NativeWindow() {
    close();
}

void NativeWindow::close() {
    if (transport_) {
        transport_->detach();
        transport_.reset();
    }
    if (webview_controller_) {
        webview_controller_->Close();
        webview_controller_ = nullptr;
    }
    if (webview_) {
        webview_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK NativeWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NativeWindow* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<NativeWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NativeWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) {
        switch (uMsg) {
        case WM_SIZE:
            self->resize_webview();
            return 0;
        case WM_KIRA_IPC_RESPONSE: {
            std::vector<std::string> responses;
            {
                std::lock_guard<std::mutex> lock(g_response_queue_mutex);
                std::swap(responses, g_pending_responses);
            }
            if (self->transport_ && self->transport_->is_ready()) {
                for (const auto& resp : responses) {
                    self->transport_->send_message(resp);
                }
            }
            return 0;
        }
        case WM_KIRA_INIT_COMPLETE: {
            bool success = (wParam == 1);
            if (self->on_init_) {
                self->on_init_(success);
            }
            return 0;
        }
        case WM_DESTROY:
            // Do not call PostQuitMessage here unless instructed by App
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool NativeWindow::initialize() {
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
        std::cerr << "[Kira] Failed to create Win32 window handle." << std::endl;
        return false;
    }

    transport_ = std::make_unique<WebViewTransport>(config_, on_message_);

    // Initialize WebView2 Environment asynchronously
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    std::cerr << "[Kira] CreateCoreWebView2EnvironmentWithOptions failed." << std::endl;
                    if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                    return result;
                }

                env->CreateCoreWebView2Controller(
                    hwnd_,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, env](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(res) || !controller) {
                                std::cerr << "[Kira] CreateCoreWebView2Controller failed." << std::endl;
                                if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                                return res;
                            }

                            webview_controller_ = controller;
                            webview_controller_->get_CoreWebView2(&webview_);

                            resize_webview();

                            // Production asset virtual host mapping if not in dev mode
                            if (config_.dev_url.empty()) {
                                auto asset_opt = SecurityPolicy::validate_asset_directory(config_.asset_dir);
                                if (!asset_opt.has_value()) {
                                    std::cerr << "[Kira Error] Invalid or missing production asset directory: " << config_.asset_dir << std::endl;
                                    if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                                    return E_FAIL;
                                }

                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                                HRESULT q_hr = webview_.As(&webview3);
                                if (FAILED(q_hr) || !webview3) {
                                    std::cerr << "[Kira Error] QueryInterface for ICoreWebView2_3 failed." << std::endl;
                                    if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                                    return q_hr;
                                }

                                HRESULT map_hr = webview3->SetVirtualHostNameToFolderMapping(
                                    L"kira.local",
                                    asset_opt->c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );

                                if (FAILED(map_hr)) {
                                    std::cerr << "[Kira Error] SetVirtualHostNameToFolderMapping failed, hr=0x" << std::hex << map_hr << std::endl;
                                    if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                                    return map_hr;
                                }
                            }

                            if (!transport_->attach(webview_.Get())) {
                                std::cerr << "[Kira Error] Failed to attach transport to WebView2." << std::endl;
                                if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 0, 0);
                                return E_FAIL;
                            }

                            navigate();

                            // Signal initialization success to Win32 message loop
                            if (hwnd_) PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, 1, 0);
                            return S_OK;
                        }).Get()
                );
                return S_OK;
            }).Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[Kira] CreateCoreWebView2EnvironmentWithOptions request failed." << std::endl;
        return false;
    }

    return true;
}

void NativeWindow::navigate() {
    if (!webview_) return;

    if (!config_.dev_url.empty()) {
        std::wstring wurl = string_to_wstring(config_.dev_url);
        webview_->Navigate(wurl.c_str());
    } else {
        // Production virtual host mapping: https://kira.local/index.html
        webview_->Navigate(L"https://kira.local/index.html");
    }
}

void NativeWindow::resize_webview() {
    if (webview_controller_ && hwnd_) {
        RECT bounds;
        GetClientRect(hwnd_, &bounds);
        webview_controller_->put_Bounds(bounds);
    }
}

void NativeWindow::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

void NativeWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void NativeWindow::post_ui_response(const std::string& response_json) {
    if (!hwnd_) return;
    {
        std::lock_guard<std::mutex> lock(g_response_queue_mutex);
        g_pending_responses.push_back(response_json);
    }
    PostMessage(hwnd_, WM_KIRA_IPC_RESPONSE, 0, 0);
}

} // namespace kira
