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
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

WebViewTransport::WebViewTransport(const WindowConfig& config, MessageCallback on_message)
    : config_(config), on_message_(std::move(on_message)) {}

WebViewTransport::~WebViewTransport() {
    detach();
}

void WebViewTransport::detach() {
    ready_ = false;
    if (webview_) {
        if (web_message_token_.value != 0) {
            webview_->remove_WebMessageReceived(web_message_token_);
            web_message_token_.value = 0;
        }
        if (nav_starting_token_.value != 0) {
            webview_->remove_NavigationStarting(nav_starting_token_);
            nav_starting_token_.value = 0;
        }
        webview_ = nullptr;
    }
}

bool WebViewTransport::attach(ICoreWebView2* webview) {
    if (!webview) return false;
    webview_ = webview;

    setup_navigation_security();
    inject_native_bootstrap();

    // Attach WebMessageReceived listener
    HRESULT hr = webview_->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (!ready_ || !args) return S_OK;

                // 1. Verify origin security
                PWSTR source_raw = nullptr;
                if (SUCCEEDED(args->get_Source(&source_raw)) && source_raw) {
                    std::string source_uri = wstring_to_string(source_raw);
                    CoTaskMemFree(source_raw);

                    if (!SecurityPolicy::is_approved_origin(source_uri, config_)) {
                        std::cerr << "[Kira Security] Rejected web message from unapproved origin: " << source_uri << std::endl;
                        return S_OK;
                    }
                } else {
                    return S_OK; // Reject if source URI cannot be verified
                }

                // 2. Retrieve raw message string
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

    if (FAILED(hr)) {
        return false;
    }

    ready_ = true;
    return true;
}

void WebViewTransport::setup_navigation_security() {
    if (!webview_) return;

    // Register NavigationStarting handler to cancel unapproved origin navigation
    webview_->add_NavigationStarting(
        Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (!args) return S_OK;
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
}

void WebViewTransport::inject_native_bootstrap() {
    if (!webview_) return;

    // Minimum required native bootstrap namespace (window.__KIRA_INTERNAL__)
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

    webview_->AddScriptToExecuteOnDocumentCreated(bootstrap_js.c_str(), nullptr);
}

bool WebViewTransport::send_message(const std::string& raw_utf8_message) {
    if (!ready_ || !webview_) {
        return false;
    }
    std::wstring wmsg = string_to_wstring(raw_utf8_message);
    HRESULT hr = webview_->PostWebMessageAsString(wmsg.c_str());
    return SUCCEEDED(hr);
}

} // namespace kira
