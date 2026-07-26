#include "src/platform/windows/win32_window.hpp"
#include "src/platform/windows/security.hpp"
#include <iostream>
#include <cassert>

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
      win_state_(std::make_shared<WindowAsyncState>()),
      ui_thread_id_(GetCurrentThreadId()) {
    win_state_->config = config_;
    win_state_->on_init = on_init_;
    win_state_->on_close_requested = on_close_requested_;
    win_state_->ui_thread_id = ui_thread_id_;
}

NativeWindow::~NativeWindow() {
    close();
}

bool NativeWindow::check_ui_thread() const {
    if (GetCurrentThreadId() != ui_thread_id_) {
        std::cerr << "[Kira Critical Error] NativeWindow operation attempted off UI thread!" << std::endl;
        assert(false && "NativeWindow operation attempted off UI thread");
        return false;
    }
    return true;
}

void NativeWindow::close() {
    if (!check_ui_thread()) return;

    if (win_state_) {
        if (win_state_->closed.exchange(true)) {
            return; // Idempotent return
        }
        win_state_->alive.store(false);
    }

    if (transport_) {
        transport_->detach();
        transport_.reset();
    }

    // WindowAsyncState is the sole owner of the asynchronously-created
    // WebView and controller. Do not maintain unsynchronized duplicate owners.
    if (win_state_ && win_state_->webview && win_state_->nav_completed_token.value != 0) {
        HRESULT hr = win_state_->webview->remove_NavigationCompleted(win_state_->nav_completed_token);
        if (FAILED(hr)) {
            std::cerr << "[Kira Window Error] remove_NavigationCompleted failed, hr=0x" << std::hex << hr << std::endl;
        }
        win_state_->nav_completed_token.value = 0;
    }

    if (win_state_ && win_state_->webview_controller) {
        HRESULT hr = win_state_->webview_controller->Close();
        if (FAILED(hr)) {
            std::cerr << "[Kira Window Error] WebView2 controller Close failed, hr=0x" << std::hex << hr << std::endl;
        }
        win_state_->webview_controller.Reset();
    }

    if (win_state_) {
        win_state_->webview.Reset();
    }

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (win_state_) {
        win_state_->hwnd = nullptr;
        win_state_->webview_controller = nullptr;
        win_state_->webview = nullptr;
        win_state_.reset();
    }

    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.clear();
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
    if (!check_ui_thread()) return;

    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        std::swap(pending, response_queue_);
    }

    if (transport_ && transport_->is_ready() && win_state_ && win_state_->alive.load()) {
        for (const auto& resp : pending) {
            transport_->send_message(resp);
        }
    }
}

bool NativeWindow::initialize() {
    if (!check_ui_thread()) {
        if (win_state_) win_state_->complete_initialization(false);
        return false;
    }

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
        if (win_state_) win_state_->complete_initialization(false);
        return false;
    }

    win_state_->hwnd = hwnd_;
    std::weak_ptr<WindowAsyncState> weak_win_state = win_state_;
    transport_ = std::make_shared<WebViewTransport>(
        config_,
        on_message_,
        [weak_win_state](HRESULT security_hr) {
            auto state = weak_win_state.lock();
            if (!state || !state->alive.load()) {
                return;
            }

            // A security-critical transport failure is terminal. Report it once, then route it according to current lifecycle phase.
            if (state->security_failure_reported.exchange(true)) {
                return;
            }

            std::cerr << "[Kira Security Critical] Transport security failure, hr=0x" << std::hex << security_hr << std::endl;

            if (!state->init_gate.is_completed()) {
                // Startup has not completed: fail initialization.
                state->complete_initialization(false);
                return;
            }

            // The app is already running. Request application-owned deferred shutdown instead of trying to reuse completed init gate.
            if (state->on_close_requested) {
                state->on_close_requested();
            }
        }
    );

    // Initialize WebView2 Environment asynchronously
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak_win = std::weak_ptr<WindowAsyncState>(win_state_), weak_transport = std::weak_ptr<WebViewTransport>(transport_)](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                auto win = weak_win.lock();
                if (!win || !win->alive.load()) return S_OK;

                if (FAILED(result) || !env) {
                    std::cerr << "[Kira Error] CreateCoreWebView2EnvironmentWithOptions failed, hr=0x" << std::hex << result << std::endl;
                    win->complete_initialization(false);
                    return result;
                }

                // Create Controller
                HRESULT hr_ctrl = env->CreateCoreWebView2Controller(
                    win->hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak_win, weak_transport](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            auto win2 = weak_win.lock();
                            if (!win2 || !win2->alive.load()) return S_OK;

                            if (FAILED(res) || !controller) {
                                std::cerr << "[Kira Error] CreateCoreWebView2Controller failed, hr=0x" << std::hex << res << std::endl;
                                win2->complete_initialization(false);
                                return res;
                            }

                            win2->webview_controller = controller;
                            HRESULT hr_get = win2->webview_controller->get_CoreWebView2(&win2->webview);
                            if (FAILED(hr_get) || !win2->webview) {
                                std::cerr << "[Kira Error] get_CoreWebView2 failed, hr=0x" << std::hex << hr_get << std::endl;
                                win2->complete_initialization(false);
                                return hr_get;
                            }

                            // Resize WebView
                            if (win2->webview_controller && win2->hwnd) {
                                RECT bounds;
                                if (GetClientRect(win2->hwnd, &bounds)) {
                                    win2->webview_controller->put_Bounds(bounds);
                                }
                            }

                            // Production asset virtual host mapping if not in dev mode
                            if (win2->config.dev_url.empty()) {
                                auto asset_opt = SecurityPolicy::validate_asset_directory(win2->config.asset_dir);
                                if (!asset_opt.has_value()) {
                                    std::cerr << "[Kira Error] Invalid or missing production asset directory: " << win2->config.asset_dir << std::endl;
                                    win2->complete_initialization(false);
                                    return E_FAIL;
                                }

                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                                HRESULT q_hr = win2->webview.As(&webview3);
                                if (FAILED(q_hr) || !webview3) {
                                    std::cerr << "[Kira Error] QueryInterface for ICoreWebView2_3 failed, hr=0x" << std::hex << q_hr << std::endl;
                                    win2->complete_initialization(false);
                                    return q_hr;
                                }

                                HRESULT map_hr = webview3->SetVirtualHostNameToFolderMapping(
                                    L"kira.local",
                                    asset_opt->c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );

                                if (FAILED(map_hr)) {
                                    std::cerr << "[Kira Error] SetVirtualHostNameToFolderMapping failed, hr=0x" << std::hex << map_hr << std::endl;
                                    win2->complete_initialization(false);
                                    return map_hr;
                                }
                            }

                            // Asynchronous attach: transport attachment continues only after bootstrap registration completes
                            auto transport = weak_transport.lock();
                            if (!transport) {
                                win2->complete_initialization(false);
                                return E_ABORT;
                            }

                            transport->attach(win2->webview.Get(), [weak_win](HRESULT attach_hr) {
                                auto win3 = weak_win.lock();
                                if (!win3 || !win3->alive.load()) return;

                                if (FAILED(attach_hr)) {
                                    std::cerr << "[Kira Error] Asynchronous transport attach failed, hr=0x" << std::hex << attach_hr << std::endl;
                                    win3->complete_initialization(false);
                                    return;
                                }

                                // Register initial NavigationCompleted handler
                                HRESULT hr_nav = win3->webview->add_NavigationCompleted(
                                    Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [weak_win](ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                            auto win4 = weak_win.lock();
                                            if (!win4 || !win4->alive.load() || !args) return S_OK;

                                            BOOL is_success = FALSE;
                                            if (FAILED(args->get_IsSuccess(&is_success)) || !is_success) {
                                                std::cerr << "[Kira Error] WebView2 initial navigation failed or was cancelled." << std::endl;
                                                win4->complete_initialization(false);
                                                return S_OK;
                                            }

                                            PWSTR source_raw = nullptr;
                                            std::string loaded_uri;
                                            if (SUCCEEDED(win4->webview->get_Source(&source_raw)) && source_raw) {
                                                loaded_uri = wstring_to_string(source_raw);
                                                CoTaskMemFree(source_raw);
                                            }

                                            if (loaded_uri.empty() || !SecurityPolicy::is_approved_origin(loaded_uri, win4->config)) {
                                                std::cerr << "[Kira Security] Loaded document source is invalid or unapproved: " << loaded_uri << std::endl;
                                                win4->complete_initialization(false);
                                                return S_OK;
                                            }

                                            // Signal initialization success only after approved initial document has loaded
                                            win4->complete_initialization(true);
                                            return S_OK;
                                        }).Get(),
                                    &win3->nav_completed_token
                                );

                                if (FAILED(hr_nav)) {
                                    std::cerr << "[Kira Error] add_NavigationCompleted failed, hr=0x" << std::hex << hr_nav << std::endl;
                                    win3->complete_initialization(false);
                                    return;
                                }

                                // Navigate only after bootstrap & transport setup complete asynchronously
                                HRESULT hr_start_nav = S_OK;
                                if (!win3->config.dev_url.empty()) {
                                    std::wstring wurl = string_to_wstring(win3->config.dev_url);
                                    hr_start_nav = win3->webview->Navigate(wurl.c_str());
                                } else {
                                    hr_start_nav = win3->webview->Navigate(L"https://kira.local/index.html");
                                }

                                if (FAILED(hr_start_nav)) {
                                    std::cerr << "[Kira Error] Navigate failed synchronously, hr=0x" << std::hex << hr_start_nav << std::endl;
                                    win3->complete_initialization(false);
                                }
                            });

                            return S_OK;
                        }).Get()
                );
                if (FAILED(hr_ctrl)) {
                    std::cerr << "[Kira Error] CreateCoreWebView2Controller call failed, hr=0x" << std::hex << hr_ctrl << std::endl;
                    win->complete_initialization(false);
                    return hr_ctrl;
                }
                return S_OK;
            }).Get()
    );

    if (FAILED(hr)) {
        std::cerr << "[Kira Error] CreateCoreWebView2EnvironmentWithOptions request failed, hr=0x" << std::hex << hr << std::endl;
        if (win_state_) win_state_->complete_initialization(false);
        return false;
    }

    return true;
}

HRESULT NativeWindow::navigate() {
    if (!check_ui_thread()) return E_FAIL;
    if (!win_state_ || !win_state_->webview) return E_POINTER;

    HRESULT hr = S_OK;
    if (!config_.dev_url.empty()) {
        std::wstring wurl = string_to_wstring(config_.dev_url);
        hr = win_state_->webview->Navigate(wurl.c_str());
    } else {
        hr = win_state_->webview->Navigate(L"https://kira.local/index.html");
    }
    return hr;
}

void NativeWindow::resize_webview() {
    if (!check_ui_thread()) return;
    if (win_state_ && win_state_->webview_controller && hwnd_) {
        RECT bounds;
        if (!GetClientRect(hwnd_, &bounds)) {
            std::cerr << "[Kira Window Error] GetClientRect failed, err=" << GetLastError() << std::endl;
            return;
        }
        HRESULT hr = win_state_->webview_controller->put_Bounds(bounds);
        if (FAILED(hr)) {
            std::cerr << "[Kira Window Error] put_Bounds failed, hr=0x" << std::hex << hr << std::endl;
        }
    }
}

void NativeWindow::show() {
    if (!check_ui_thread()) return;
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

void NativeWindow::hide() {
    if (!check_ui_thread()) return;
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void NativeWindow::post_ui_response(const std::string& response_json) {
    if (!hwnd_ || !win_state_ || !win_state_->alive.load()) return;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.push_back(response_json);
    }
    BOOL posted = PostMessage(hwnd_, WM_KIRA_IPC_RESPONSE, 0, 0);
    if (!posted) {
        std::cerr << "[Kira Window Error] PostMessage(WM_KIRA_IPC_RESPONSE) failed, err=" << GetLastError() << std::endl;
        if (ui_thread_id_ != 0) {
            PostThreadMessage(ui_thread_id_, WM_KIRA_IPC_RESPONSE, 0, 0);
        }
    }
}

} // namespace kira
