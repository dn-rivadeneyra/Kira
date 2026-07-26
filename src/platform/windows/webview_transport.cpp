#include "src/platform/windows/webview_transport.hpp"
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

WebViewTransport::WebViewTransport(const WindowConfig& config, MessageCallback on_message)
    : config_(config), on_message_(std::move(on_message)), lifetime_(std::make_shared<CallbackLifetime>()) {}

WebViewTransport::~WebViewTransport() {
    detach();
}

void WebViewTransport::detach() {
    ready_ = false;
    if (lifetime_) {
        lifetime_->alive.store(false);
        lifetime_.reset();
    }
    if (webview_) {
        if (web_message_token_.value != 0) {
            HRESULT hr = webview_->remove_WebMessageReceived(web_message_token_);
            if (FAILED(hr)) {
                std::cerr << "[Kira Transport] Failed to remove WebMessageReceived handler, hr=0x" << std::hex << hr << std::endl;
            }
            web_message_token_.value = 0;
        }
        if (nav_starting_token_.value != 0) {
            HRESULT hr = webview_->remove_NavigationStarting(nav_starting_token_);
            if (FAILED(hr)) {
                std::cerr << "[Kira Transport] Failed to remove NavigationStarting handler, hr=0x" << std::hex << hr << std::endl;
            }
            nav_starting_token_.value = 0;
        }
        webview_ = nullptr;
    }
}

bool WebViewTransport::attach(ICoreWebView2* webview) {
    if (!webview) return false;
    webview_ = webview;
    if (!lifetime_) {
        lifetime_ = std::make_shared<CallbackLifetime>();
    }

    // Transactional Attach Sequence:
    // 1. Install native bootstrap script
    HRESULT hr_boot = install_native_bootstrap();
    if (FAILED(hr_boot)) {
        std::cerr << "[Kira Transport] Native bootstrap installation failed, hr=0x" << std::hex << hr_boot << std::endl;
        detach();
        return false;
    }

    // 2. Register navigation security handler
    HRESULT hr_nav = setup_navigation_security();
    if (FAILED(hr_nav)) {
        std::cerr << "[Kira Transport] Navigation security handler registration failed, hr=0x" << std::hex << hr_nav << std::endl;
        detach();
        return false;
    }

    // 3. Register top-level web message handler
    HRESULT hr_msg = setup_web_message_handler();
    if (FAILED(hr_msg)) {
        std::cerr << "[Kira Transport] Web message handler registration failed, hr=0x" << std::hex << hr_msg << std::endl;
        detach();
        return false;
    }

    ready_ = true;
    return true;
}

HRESULT WebViewTransport::install_native_bootstrap() {
    if (!webview_) return E_POINTER;

    // Native bootstrap under window.__KIRA_INTERNAL__
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

    HRESULT hr = webview_->AddScriptToExecuteOnDocumentCreated(bootstrap_js.c_str(), nullptr);
    return hr;
}

HRESULT WebViewTransport::setup_navigation_security() {
    if (!webview_) return E_POINTER;

    // Register NavigationStarting handler to cancel unapproved top-level origin navigation
    HRESULT hr = webview_->add_NavigationStarting(
        Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, weak_life = std::weak_ptr<CallbackLifetime>(lifetime_)](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                auto life = weak_life.lock();
                if (!life || !life->alive.load() || !args) return S_OK;

                PWSTR uri_raw = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri_raw)) && uri_raw) {
                    std::string target_uri = wstring_to_string(uri_raw);
                    CoTaskMemFree(uri_raw);

                    if (!SecurityPolicy::is_approved_origin(target_uri, config_)) {
                        std::cerr << "[Kira Security] Blocked navigation to unapproved origin: " << target_uri << std::endl;
                        args->put_Cancel(TRUE);
                    }
                }
                return S_OK;
            }).Get(),
        &nav_starting_token_
    );
    return hr;
}

HRESULT WebViewTransport::setup_web_message_handler() {
    if (!webview_) return E_POINTER;

    // Top-level WebMessageReceived event subscription:
    // 1. Kira subscribes to top-level WebMessageReceived on CoreWebView2.
    // 2. Frame WebMessageReceived events are not subscribed.
    // 3. Sender URL is validated against the approved origin policy and top-level document URL.
    HRESULT hr = webview_->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this, weak_life = std::weak_ptr<CallbackLifetime>(lifetime_)](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                auto life = weak_life.lock();
                if (!life || !life->alive.load() || !ready_ || !args) return S_OK;

                // Retrieve top-level document URL from WebView
                PWSTR top_raw = nullptr;
                std::string top_uri;
                if (SUCCEEDED(webview_->get_Source(&top_raw)) && top_raw) {
                    top_uri = wstring_to_string(top_raw);
                    CoTaskMemFree(top_raw);
                }

                if (top_uri.empty() || !SecurityPolicy::is_approved_origin(top_uri, config_)) {
                    std::cerr << "[Kira Security] Top-level document is not an approved origin: " << top_uri << std::endl;
                    return S_OK;
                }

                // Retrieve message sender URL
                PWSTR source_raw = nullptr;
                if (SUCCEEDED(args->get_Source(&source_raw)) && source_raw) {
                    std::string source_uri = wstring_to_string(source_raw);
                    CoTaskMemFree(source_raw);

                    if (!SecurityPolicy::is_approved_origin(source_uri, config_)) {
                        std::cerr << "[Kira Security] Rejected web message from unapproved origin: " << source_uri << std::endl;
                        return S_OK;
                    }

                    // Verify sender origin matches top-level document origin
                    auto top_opt = SecurityPolicy::parse_and_normalize_origin(top_uri);
                    auto src_opt = SecurityPolicy::parse_and_normalize_origin(source_uri);
                    if (!top_opt || !src_opt || !SecurityPolicy::matches_origin(*top_opt, *src_opt)) {
                        std::cerr << "[Kira Security] Rejected message from mismatched origin: " << source_uri << std::endl;
                        return S_OK;
                    }
                } else {
                    return S_OK;
                }

                // Retrieve raw message string
                PWSTR message_raw = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&message_raw)) && message_raw) {
                    std::string raw_msg = wstring_to_string(message_raw);
                    CoTaskMemFree(message_raw);

                    if (on_message_) {
                        on_message_(raw_msg);
                    }
                }
                return S_OK;
            }).Get(),
        &web_message_token_
    );
    return hr;
}

bool WebViewTransport::send_message(const std::string& raw_utf8_message) {
    if (!ready_ || !lifetime_ || !lifetime_->alive.load() || !webview_) {
        return false;
    }
    std::wstring wmsg = string_to_wstring(raw_utf8_message);
    HRESULT hr = webview_->PostWebMessageAsString(wmsg.c_str());
    if (FAILED(hr)) {
        std::cerr << "[Kira Transport] PostWebMessageAsString failed, hr=0x" << std::hex << hr << std::endl;
        return false;
    }
    return true;
}

} // namespace kira
