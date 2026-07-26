#include "src/platform/windows/win32_window.hpp"
#include "src/platform/windows/security.hpp"
#include <iostream>

namespace kira {

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
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)str.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

NativeWindow::NativeWindow(
    const WindowConfig& config,
    RawMessageCallback on_message,
    InitCallback on_init,
    CloseRequestedCallback on_close_requested
)
    : config_(config),
      on_message_(std::move(on_message)),
      on_init_(std::move(on_init)),
      on_close_requested_(std::move(on_close_requested)),
      lifetime_(std::make_shared<CallbackLifetime>()) {}

NativeWindow::~NativeWindow() {
    close();
}

void NativeWindow::close() {
    if (closed_.exchange(true)) {
        return; // Idempotent return if already closed
    }

    if (lifetime_) {
        lifetime_->alive.store(false);
        lifetime_.reset();
    }

    if (transport_) {
        transport_->detach();
        transport_.reset();
    }

    if (webview_ && nav_completed_token_.value != 0) {
        webview_->remove_NavigationCompleted(nav_completed_token_);
        nav_completed_token_.value = 0;
    }

    if (webview_controller_) {
        HRESULT hr = webview_controller_->Close();
        if (FAILED(hr)) {
            std::cerr << "[Kira Window] webview_controller_->Close failed, hr=0x" << std::hex << hr << std::endl;
        }
        webview_controller_ = nullptr;
    }

    if (webview_) {
        webview_ = nullptr;
    }

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.clear();
    }
}

void NativeWindow::complete_initialization(bool success) {
    InitializationResult expected = InitializationResult::pending;
    InitializationResult desired = success ? InitializationResult::succeeded : InitializationResult::failed;

    if (!init_result_.compare_exchange_strong(expected, desired)) {
        return; // One-shot guard: first call wins, subsequent calls ignored
    }

    // Remove initialization-only NavigationCompleted handler
    if (webview_ && nav_completed_token_.value != 0) {
        webview_->remove_NavigationCompleted(nav_completed_token_);
        nav_completed_token_.value = 0;
    }

    if (hwnd_) {
        PostMessage(hwnd_, WM_KIRA_INIT_COMPLETE, success ? 1 : 0, 0);
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

        case WM_CLOSE:
            if (self->on_close_requested_) {
                self->on_close_requested_();
            }
            return 0;

        case WM_KIRA_IPC_RESPONSE:
            self->drain_response_queue();
            return 0;

        case WM_KIRA_INIT_COMPLETE: {
            bool success = (wParam == 1);
            if (self->on_init_) {
                self->on_init_(success);
            }
            return 0;
        }

        case WM_DESTROY:
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void NativeWindow::drain_response_queue() {
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        std::swap(pending, response_queue_);
    }

    if (transport_ && transport_->is_ready() && lifetime_ && lifetime_->alive.load()) {
        for (const auto& resp : pending) {
            transport_->send_message(resp);
        }
    }
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
        std::cerr << "[Kira Error] Failed to create Win32 window handle." << std::endl;
        complete_initialization(false);
        return false;
    }

    transport_ = std::make_unique<WebViewTransport>(config_, on_message_);

    // Initialize WebView2 Environment asynchronously
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, weak_life = std::weak_ptr<CallbackLifetime>(lifetime_)](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                auto life = weak_life.lock();
                if (!life || !life->alive.load()) return S_OK;

                if (FAILED(result) || !env) {
                    std::cerr << "[Kira Error] CreateCoreWebView2EnvironmentWithOptions failed, hr=0x" << std::hex << result << std::endl;
                    complete_initialization(false);
                    return result;
                }

                HRESULT hr_ctrl = env->CreateCoreWebView2Controller(
                    hwnd_,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, weak_life](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            auto life2 = weak_life.lock();
                            if (!life2 || !life2->alive.load()) return S_OK;

                            if (FAILED(res) || !controller) {
                                std::cerr << "[Kira Error] CreateCoreWebView2Controller failed, hr=0x" << std::hex << res << std::endl;
                                complete_initialization(false);
                                return res;
                            }

                            webview_controller_ = controller;
                            HRESULT hr_get = webview_controller_->get_CoreWebView2(&webview_);
                            if (FAILED(hr_get) || !webview_) {
                                std::cerr << "[Kira Error] get_CoreWebView2 failed, hr=0x" << std::hex << hr_get << std::endl;
                                complete_initialization(false);
                                return hr_get;
                            }

                            resize_webview();

                            // Production asset virtual host mapping if not in dev mode
                            if (config_.dev_url.empty()) {
                                auto asset_opt = SecurityPolicy::validate_asset_directory(config_.asset_dir);
                                if (!asset_opt.has_value()) {
                                    std::cerr << "[Kira Error] Invalid or missing production asset directory: " << config_.asset_dir << std::endl;
                                    complete_initialization(false);
                                    return E_FAIL;
                                }

                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                                HRESULT q_hr = webview_.As(&webview3);
                                if (FAILED(q_hr) || !webview3) {
                                    std::cerr << "[Kira Error] QueryInterface for ICoreWebView2_3 failed, hr=0x" << std::hex << q_hr << std::endl;
                                    complete_initialization(false);
                                    return q_hr;
                                }

                                HRESULT map_hr = webview3->SetVirtualHostNameToFolderMapping(
                                    L"kira.local",
                                    asset_opt->c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );

                                if (FAILED(map_hr)) {
                                    std::cerr << "[Kira Error] SetVirtualHostNameToFolderMapping failed, hr=0x" << std::hex << map_hr << std::endl;
                                    complete_initialization(false);
                                    return map_hr;
                                }
                            }

                            if (!transport_->attach(webview_.Get())) {
                                std::cerr << "[Kira Error] Failed to attach transport to WebView2." << std::endl;
                                complete_initialization(false);
                                return E_FAIL;
                            }

                            // Register NavigationCompleted handler for initial document validation
                            HRESULT hr_nav = webview_->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this, weak_life](ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        auto life3 = weak_life.lock();
                                        if (!life3 || !life3->alive.load() || !args) return S_OK;

                                        BOOL is_success = FALSE;
                                        if (FAILED(args->get_IsSuccess(&is_success)) || !is_success) {
                                            std::cerr << "[Kira Error] WebView2 initial navigation failed or was cancelled." << std::endl;
                                            complete_initialization(false);
                                            return S_OK;
                                        }

                                        PWSTR source_raw = nullptr;
                                        std::string loaded_uri;
                                        if (SUCCEEDED(webview_->get_Source(&source_raw)) && source_raw) {
                                            loaded_uri = wstring_to_string(source_raw);
                                            CoTaskMemFree(source_raw);
                                        }

                                        if (loaded_uri.empty() || !SecurityPolicy::is_approved_origin(loaded_uri, config_)) {
                                            std::cerr << "[Kira Security] Loaded document source is invalid or unapproved: " << loaded_uri << std::endl;
                                            complete_initialization(false);
                                            return S_OK;
                                        }

                                        // Signal initialization success only after initial top-level document has loaded & passed validation
                                        complete_initialization(true);
                                        return S_OK;
                                    }).Get(),
                                &nav_completed_token_
                            );

                            if (FAILED(hr_nav)) {
                                std::cerr << "[Kira Error] add_NavigationCompleted failed, hr=0x" << std::hex << hr_nav << std::endl;
                                complete_initialization(false);
                                return hr_nav;
                            }

                            HRESULT hr_start_nav = navigate();
                            if (FAILED(hr_start_nav)) {
                                std::cerr << "[Kira Error] Navigate failed synchronously, hr=0x" << std::hex << hr_start_nav << std::endl;
                                complete_initialization(false);
                                return hr_start_nav;
                            }

                            return S_OK;
                        }).Get()
                );
                if (FAILED(hr_ctrl)) {
                    std::cerr << "[Kira Error] CreateCoreWebView2Controller call failed, hr=0x" << std::hex << hr_ctrl << std::endl;
                    complete_initialization(false);
                    return hr_ctrl;
                }
                return S_OK;
            }).Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[Kira Error] CreateCoreWebView2EnvironmentWithOptions request failed, hr=0x" << std::hex << hr << std::endl;
        complete_initialization(false);
        return false;
    }

    return true;
}

HRESULT NativeWindow::navigate() {
    if (!webview_) return E_POINTER;

    HRESULT hr = S_OK;
    if (!config_.dev_url.empty()) {
        std::wstring wurl = string_to_wstring(config_.dev_url);
        hr = webview_->Navigate(wurl.c_str());
    } else {
        hr = webview_->Navigate(L"https://kira.local/index.html");
    }
    return hr;
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
    if (!hwnd_ || !lifetime_ || !lifetime_->alive.load()) return;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.push_back(response_json);
    }
    PostMessage(hwnd_, WM_KIRA_IPC_RESPONSE, 0, 0);
}

} // namespace kira
