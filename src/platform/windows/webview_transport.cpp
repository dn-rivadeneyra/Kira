#include "src/platform/windows/webview_transport.hpp"
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

WebViewTransport::WebViewTransport(const WindowConfig& config, MessageCallback on_message)
    : config_(config),
      on_message_(std::move(on_message)),
      op_state_(std::make_shared<AttachOperationState>()),
      ui_thread_id_(GetCurrentThreadId()) {
    op_state_->config = config_;
    op_state_->on_message = on_message_;
    op_state_->ui_thread_id = ui_thread_id_;
}

WebViewTransport::~WebViewTransport() {
    detach();
}

bool WebViewTransport::check_ui_thread() const {
    if (GetCurrentThreadId() != ui_thread_id_) {
        std::cerr << "[Kira Critical Error] WebViewTransport operation attempted off UI thread!" << std::endl;
        assert(false && "WebViewTransport operation attempted off UI thread");
        return false;
    }
    return true;
}

void WebViewTransport::detach() {
    if (!check_ui_thread()) return;

    ready_ = false;
    if (op_state_) {
        op_state_->alive.store(false);
    }

    if (webview_) {
        if (web_message_token_.value != 0) {
            HRESULT hr = webview_->remove_WebMessageReceived(web_message_token_);
            if (FAILED(hr)) {
                std::cerr << "[Kira Transport Error] remove_WebMessageReceived failed, hr=0x" << std::hex << hr << std::endl;
            }
            web_message_token_.value = 0;
        }
        if (nav_starting_token_.value != 0) {
            HRESULT hr = webview_->remove_NavigationStarting(nav_starting_token_);
            if (FAILED(hr)) {
                std::cerr << "[Kira Transport Error] remove_NavigationStarting failed, hr=0x" << std::hex << hr << std::endl;
            }
            nav_starting_token_.value = 0;
        }
        if (op_state_ && !op_state_->script_id.empty()) {
            HRESULT hr = webview_->RemoveScriptToExecuteOnDocumentCreated(op_state_->script_id.c_str());
            if (FAILED(hr)) {
                std::cerr << "[Kira Transport Error] RemoveScriptToExecuteOnDocumentCreated failed, hr=0x" << std::hex << hr << std::endl;
            }
            op_state_->script_id.clear();
        }
        webview_ = nullptr;
    }

    if (op_state_) {
        op_state_->webview = nullptr;
        op_state_.reset();
    }
}

void WebViewTransport::attach(ICoreWebView2* webview, AttachCallback callback) {
    if (!check_ui_thread()) {
        if (callback) callback(E_FAIL);
        return;
    }

    if (!webview || !callback) {
        if (callback) callback(E_POINTER);
        return;
    }

    webview_ = webview;
    if (!op_state_) {
        op_state_ = std::make_shared<AttachOperationState>();
        op_state_->config = config_;
        op_state_->on_message = on_message_;
        op_state_->ui_thread_id = ui_thread_id_;
    }
    op_state_->webview = webview_;
    op_state_->callback = callback;
    op_state_->attach_completed.store(false);

    // Native bootstrap script under window.__KIRA_INTERNAL__
    std::wstring bootstrap_js = L"(function() {\n"
                                L"    if (window.__KIRA_INTERNAL__) return;\n"
                                L"    const callbacks = new Set();\n"
                                L"    window.__KIRA_INTERNAL__ = Object.freeze({\n"
                                L"        send: function(msg) {\n"
                                L"            if (typeof msg === 'string') {\n"
                                L"                window.chrome.webview.postMessage(msg);\n"
                                L"            }\n"
                                L"        },\n"
                                L"        onMessage: function(cb) {\n"
                                L"            if (typeof cb === 'function') callbacks.add(cb);\n"
                                L"        }\n"
                                L"    });\n"
                                L"    window.chrome.webview.addEventListener('message', function(event) {\n"
                                L"        const data = event.data;\n"
                                L"        callbacks.forEach(cb => {\n"
                                L"            try { cb(data); } catch (e) { console.error('[Kira Bootstrap]', e); }\n"
                                L"        });\n"
                                L"    });\n"
                                L"})();";

    // Step 1: Asynchronously install native bootstrap script (NO 'this' captured in COM callback lambda!)
    HRESULT hr_boot = webview_->AddScriptToExecuteOnDocumentCreated(
        bootstrap_js.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [weak_op = std::weak_ptr<AttachOperationState>(op_state_)](HRESULT errorCode, LPCWSTR id) -> HRESULT {
                auto op = weak_op.lock();
                if (!op || !op->alive.load()) {
                    return S_OK; // Harmless no-op if transport detached
                }

                if (FAILED(errorCode) || !id || wcslen(id) == 0) {
                    std::cerr << "[Kira Transport Error] AddScriptToExecuteOnDocumentCreated failed asynchronously, hr=0x" << std::hex << errorCode << std::endl;
                    if (op->webview && !op->script_id.empty()) {
                        op->webview->RemoveScriptToExecuteOnDocumentCreated(op->script_id.c_str());
                        op->script_id.clear();
                    }
                    op->complete_attach(FAILED(errorCode) ? errorCode : E_FAIL);
                    return S_OK;
                }

                op->script_id = id;

                // Step 2: Register NavigationStarting security handler (NO 'this' captured in COM callback lambda!)
                HRESULT hr_nav = op->webview->add_NavigationStarting(
                    Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [weak_op](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                            auto st = weak_op.lock();
                            if (!st || !st->alive.load()) return S_OK;

                            // Fail closed: cancel navigation whenever args is null or target URI cannot be positively authorized
                            bool authorized = false;
                            if (args) {
                                PWSTR uri_raw = nullptr;
                                HRESULT hr_uri = args->get_Uri(&uri_raw);
                                if (SUCCEEDED(hr_uri) && uri_raw && wcslen(uri_raw) > 0) {
                                    std::string target_uri = wstring_to_string(uri_raw);
                                    CoTaskMemFree(uri_raw);
                                    authorized = SecurityPolicy::is_approved_origin(target_uri, st->config);
                                }
                            }

                            if (!authorized) {
                                std::cerr << "[Kira Security] Navigation security failed closed; canceling navigation." << std::endl;
                                if (args) {
                                    HRESULT hr_cancel = args->put_Cancel(TRUE);
                                    if (FAILED(hr_cancel)) {
                                        std::cerr << "[Kira Security CRITICAL] put_Cancel(TRUE) failed, hr=0x" << std::hex << hr_cancel << std::endl;
                                    }
                                }
                            }
                            return S_OK;
                        }).Get(),
                    &op->nav_starting_token
                );

                if (FAILED(hr_nav)) {
                    std::cerr << "[Kira Transport Error] add_NavigationStarting failed, hr=0x" << std::hex << hr_nav << std::endl;
                    if (op->webview && !op->script_id.empty()) {
                        op->webview->RemoveScriptToExecuteOnDocumentCreated(op->script_id.c_str());
                        op->script_id.clear();
                    }
                    op->complete_attach(hr_nav);
                    return S_OK;
                }

                // Step 3: Register top-level WebMessageReceived event handler (NO 'this' captured in COM callback lambda!)
                HRESULT hr_msg = op->webview->add_WebMessageReceived(
                    Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [weak_op](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                            auto st = weak_op.lock();
                            if (!st || !st->alive.load() || !args || !st->webview) return S_OK;

                            // 1. Verify active top-level document source
                            PWSTR top_raw = nullptr;
                            HRESULT hr_top = st->webview->get_Source(&top_raw);
                            if (FAILED(hr_top) || !top_raw || wcslen(top_raw) == 0) {
                                if (top_raw) CoTaskMemFree(top_raw);
                                std::cerr << "[Kira Security] get_Source failed or returned empty." << std::endl;
                                return S_OK;
                            }
                            std::string top_uri = wstring_to_string(top_raw);
                            CoTaskMemFree(top_raw);

                            if (!SecurityPolicy::is_approved_origin(top_uri, st->config)) {
                                std::cerr << "[Kira Security] Top-level document origin not approved." << std::endl;
                                return S_OK;
                            }

                            // 2. Verify message sender source
                            PWSTR source_raw = nullptr;
                            HRESULT hr_src = args->get_Source(&source_raw);
                            if (FAILED(hr_src) || !source_raw || wcslen(source_raw) == 0) {
                                if (source_raw) CoTaskMemFree(source_raw);
                                std::cerr << "[Kira Security] Message sender source failed or empty." << std::endl;
                                return S_OK;
                            }
                            std::string source_uri = wstring_to_string(source_raw);
                            CoTaskMemFree(source_raw);

                            if (!SecurityPolicy::is_approved_origin(source_uri, st->config)) {
                                std::cerr << "[Kira Security] Message sender origin not approved." << std::endl;
                                return S_OK;
                            }

                            // 3. Verify sender origin matches top-level document origin
                            auto top_opt = SecurityPolicy::parse_and_normalize_origin(top_uri);
                            auto src_opt = SecurityPolicy::parse_and_normalize_origin(source_uri);
                            if (!top_opt || !src_opt || !SecurityPolicy::matches_origin(*top_opt, *src_opt)) {
                                std::cerr << "[Kira Security] Message sender origin does not match top-level document." << std::endl;
                                return S_OK;
                            }

                            // 4. Extract raw message string
                            PWSTR message_raw = nullptr;
                            HRESULT hr_get_msg = args->TryGetWebMessageAsString(&message_raw);
                            if (FAILED(hr_get_msg) || !message_raw) {
                                if (message_raw) CoTaskMemFree(message_raw);
                                std::cerr << "[Kira Transport] TryGetWebMessageAsString failed." << std::endl;
                                return S_OK;
                            }
                            std::string raw_msg = wstring_to_string(message_raw);
                            CoTaskMemFree(message_raw);

                            if (raw_msg.empty()) {
                                return S_OK;
                            }

                            if (st->on_message) {
                                st->on_message(raw_msg);
                            }
                            return S_OK;
                        }).Get(),
                    &op->web_message_token
                );

                if (FAILED(hr_msg)) {
                    std::cerr << "[Kira Transport Error] add_WebMessageReceived failed, hr=0x" << std::hex << hr_msg << std::endl;
                    if (op->webview && op->nav_starting_token.value != 0) {
                        op->webview->remove_NavigationStarting(op->nav_starting_token);
                        op->nav_starting_token.value = 0;
                    }
                    if (op->webview && !op->script_id.empty()) {
                        op->webview->RemoveScriptToExecuteOnDocumentCreated(op->script_id.c_str());
                        op->script_id.clear();
                    }
                    op->complete_attach(hr_msg);
                    return S_OK;
                }

                op->complete_attach(S_OK);
                return S_OK;
            }).Get()
    );

    if (FAILED(hr_boot)) {
        std::cerr << "[Kira Transport Error] AddScriptToExecuteOnDocumentCreated call failed synchronously, hr=0x" << std::hex << hr_boot << std::endl;
        detach();
        op_state_->complete_attach(hr_boot);
    }
}

bool WebViewTransport::send_message(const std::string& raw_utf8_message) {
    if (!check_ui_thread()) return false;

    if (!ready_ || !op_state_ || !op_state_->alive.load() || !webview_) {
        return false;
    }
    std::wstring wmsg = string_to_wstring(raw_utf8_message);
    HRESULT hr = webview_->PostWebMessageAsString(wmsg.c_str());
    if (FAILED(hr)) {
        std::cerr << "[Kira Transport Error] PostWebMessageAsString failed, hr=0x" << std::hex << hr << std::endl;
        return false;
    }
    return true;
}

} // namespace kira
