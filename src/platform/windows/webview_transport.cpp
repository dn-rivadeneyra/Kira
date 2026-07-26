#include "src/platform/windows/webview_transport.hpp"
#include "src/platform/windows/security.hpp"

#include <cassert>
#include <iostream>
#include <utility>

namespace kira {

namespace {

std::wstring string_to_wstring(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );

    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required
    );

    if (written != required) {
        return {};
    }

    return result;
}

std::string wstring_to_string(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr
    );

    if (written != required) {
        return {};
    }

    return result;
}

void report_security_failure(
    const std::shared_ptr<AttachOperationState>& state,
    HRESULT hr
) {
    if (!state || !state->alive.load()) {
        return;
    }

    state->ready.store(false);

    const auto callback = state->on_security_failure;
    if (callback) {
        callback(hr);
    }
}

} // namespace

WebViewTransport::WebViewTransport(
    const WindowConfig& config,
    MessageCallback on_message,
    TransportFailureCallback on_security_failure
)
    : config_(config),
      on_message_(std::move(on_message)),
      on_security_failure_(std::move(on_security_failure)),
      ui_thread_id_(GetCurrentThreadId()) {}

WebViewTransport::~WebViewTransport() {
    detach();
}

bool WebViewTransport::check_ui_thread() const {
    if (GetCurrentThreadId() == ui_thread_id_) {
        return true;
    }

    std::cerr
        << "[Kira Critical Error] WebViewTransport operation attempted off UI thread."
        << std::endl;
    assert(false && "WebViewTransport operation attempted off UI thread");
    return false;
}

void WebViewTransport::cleanup_state(
    const std::shared_ptr<AttachOperationState>& state
) {
    if (!state) {
        return;
    }

    state->ready.store(false);
    state->alive.store(false);

    const auto webview = state->webview;
    if (webview) {
        if (state->web_message_token.value != 0) {
            const HRESULT hr =
                webview->remove_WebMessageReceived(state->web_message_token);
            if (FAILED(hr)) {
                std::cerr
                    << "[Kira Transport Error] remove_WebMessageReceived failed, hr=0x"
                    << std::hex << hr << std::endl;
            }
            state->web_message_token.value = 0;
        }

        if (state->nav_starting_token.value != 0) {
            const HRESULT hr =
                webview->remove_NavigationStarting(state->nav_starting_token);
            if (FAILED(hr)) {
                std::cerr
                    << "[Kira Transport Error] remove_NavigationStarting failed, hr=0x"
                    << std::hex << hr << std::endl;
            }
            state->nav_starting_token.value = 0;
        }

        if (!state->script_id.empty()) {
            const HRESULT hr =
                webview->RemoveScriptToExecuteOnDocumentCreated(
                    state->script_id.c_str()
                );
            if (FAILED(hr)) {
                std::cerr
                    << "[Kira Transport Error] "
                       "RemoveScriptToExecuteOnDocumentCreated failed, hr=0x"
                    << std::hex << hr << std::endl;
            }
            state->script_id.clear();
        }
    }

    state->on_message = nullptr;
    state->on_security_failure = nullptr;
    state->webview.Reset();
}

void WebViewTransport::detach() {
    if (!check_ui_thread()) {
        return;
    }

    auto state = std::exchange(state_, nullptr);
    if (!state) {
        return;
    }

    cleanup_state(state);

    // If attachment never reached a terminal callback, terminate it now.
    state->complete_attach(E_ABORT);
}

void WebViewTransport::attach(
    ICoreWebView2* webview,
    AttachCallback callback
) {
    if (!check_ui_thread()) {
        if (callback) {
            callback(E_FAIL);
        }
        return;
    }

    if (!webview || !callback) {
        if (callback) {
            callback(E_POINTER);
        }
        return;
    }

    // Attaching twice is not supported. Cleanly detach the prior state first.
    if (state_) {
        detach();
    }

    auto state = std::make_shared<AttachOperationState>();
    state->ui_thread_id = ui_thread_id_;
    state->webview = webview;
    state->config = config_;
    state->on_message = on_message_;
    state->on_security_failure = on_security_failure_;
    state->attach_callback = std::move(callback);
    state_ = state;

    const std::wstring bootstrap_js =
        L"(function() {\n"
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
        L"        callbacks.forEach(function(cb) {\n"
        L"            try { cb(event.data); }\n"
        L"            catch (error) { console.error('[Kira Bootstrap]', error); }\n"
        L"        });\n"
        L"    });\n"
        L"})();";

    const HRESULT start_hr =
        webview->AddScriptToExecuteOnDocumentCreated(
            bootstrap_js.c_str(),
            Microsoft::WRL::Callback<
                ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler
            >(
                [weak_state = std::weak_ptr<AttachOperationState>(state)](
                    HRESULT script_hr,
                    LPCWSTR script_id
                ) -> HRESULT {
                    const auto operation = weak_state.lock();
                    if (!operation || !operation->alive.load()) {
                        return S_OK;
                    }

                    if (FAILED(script_hr) ||
                        !script_id ||
                        script_id[0] == L'\0') {
                        std::cerr
                            << "[Kira Transport Error] Bootstrap registration failed, hr=0x"
                            << std::hex << script_hr << std::endl;
                        WebViewTransport::cleanup_state(operation);
                        operation->complete_attach(
                            FAILED(script_hr) ? script_hr : E_FAIL
                        );
                        return S_OK;
                    }

                    operation->script_id = script_id;

                    const HRESULT nav_hr =
                        operation->webview->add_NavigationStarting(
                            Microsoft::WRL::Callback<
                                ICoreWebView2NavigationStartingEventHandler
                            >(
                                [weak_state](
                                    ICoreWebView2*,
                                    ICoreWebView2NavigationStartingEventArgs* args
                                ) -> HRESULT {
                                    const auto active = weak_state.lock();
                                    if (!active || !active->alive.load()) {
                                        return S_OK;
                                    }

                                    if (!args) {
                                        std::cerr
                                            << "[Kira Security Critical] "
                                               "NavigationStarting delivered null args."
                                            << std::endl;
                                        report_security_failure(active, E_POINTER);
                                        return E_POINTER;
                                    }

                                    PWSTR uri_raw = nullptr;
                                    const HRESULT uri_hr = args->get_Uri(&uri_raw);

                                    bool authorized = false;
                                    if (SUCCEEDED(uri_hr) &&
                                        uri_raw &&
                                        uri_raw[0] != L'\0') {
                                        const std::wstring uri_wide(uri_raw);
                                        CoTaskMemFree(uri_raw);
                                        uri_raw = nullptr;

                                        const std::string uri =
                                            wstring_to_string(uri_wide);
                                        authorized =
                                            !uri.empty() &&
                                            SecurityPolicy::is_approved_origin(
                                                uri,
                                                active->config
                                            );
                                    } else if (uri_raw) {
                                        CoTaskMemFree(uri_raw);
                                        uri_raw = nullptr;
                                    }

                                    if (authorized) {
                                        return S_OK;
                                    }

                                    const HRESULT cancel_hr =
                                        args->put_Cancel(TRUE);
                                    if (FAILED(cancel_hr)) {
                                        std::cerr
                                            << "[Kira Security Critical] "
                                               "put_Cancel(TRUE) failed, hr=0x"
                                            << std::hex << cancel_hr << std::endl;
                                        report_security_failure(active, cancel_hr);
                                        return cancel_hr;
                                    }

                                    return S_OK;
                                }
                            ).Get(),
                            &operation->nav_starting_token
                        );

                    if (FAILED(nav_hr)) {
                        std::cerr
                            << "[Kira Transport Error] "
                               "add_NavigationStarting failed, hr=0x"
                            << std::hex << nav_hr << std::endl;
                        WebViewTransport::cleanup_state(operation);
                        operation->complete_attach(nav_hr);
                        return S_OK;
                    }

                    const HRESULT message_hr =
                        operation->webview->add_WebMessageReceived(
                            Microsoft::WRL::Callback<
                                ICoreWebView2WebMessageReceivedEventHandler
                            >(
                                [weak_state](
                                    ICoreWebView2*,
                                    ICoreWebView2WebMessageReceivedEventArgs* args
                                ) -> HRESULT {
                                    const auto active = weak_state.lock();
                                    if (!active ||
                                        !active->alive.load() ||
                                        !active->ready.load() ||
                                        !active->webview ||
                                        !args) {
                                        return S_OK;
                                    }

                                    PWSTR top_raw = nullptr;
                                    const HRESULT top_hr =
                                        active->webview->get_Source(&top_raw);
                                    if (FAILED(top_hr) ||
                                        !top_raw ||
                                        top_raw[0] == L'\0') {
                                        if (top_raw) {
                                            CoTaskMemFree(top_raw);
                                        }
                                        return S_OK;
                                    }

                                    const std::wstring top_wide(top_raw);
                                    CoTaskMemFree(top_raw);
                                    const std::string top_uri =
                                        wstring_to_string(top_wide);

                                    PWSTR source_raw = nullptr;
                                    const HRESULT source_hr =
                                        args->get_Source(&source_raw);
                                    if (FAILED(source_hr) ||
                                        !source_raw ||
                                        source_raw[0] == L'\0') {
                                        if (source_raw) {
                                            CoTaskMemFree(source_raw);
                                        }
                                        return S_OK;
                                    }

                                    const std::wstring source_wide(source_raw);
                                    CoTaskMemFree(source_raw);
                                    const std::string source_uri =
                                        wstring_to_string(source_wide);

                                    if (top_uri.empty() ||
                                        source_uri.empty() ||
                                        !SecurityPolicy::is_approved_origin(
                                            top_uri,
                                            active->config
                                        ) ||
                                        !SecurityPolicy::is_approved_origin(
                                            source_uri,
                                            active->config
                                        )) {
                                        return S_OK;
                                    }

                                    const auto top_origin =
                                        SecurityPolicy::parse_and_normalize_origin(
                                            top_uri
                                        );
                                    const auto source_origin =
                                        SecurityPolicy::parse_and_normalize_origin(
                                            source_uri
                                        );

                                    if (!top_origin ||
                                        !source_origin ||
                                        !SecurityPolicy::matches_origin(
                                            *top_origin,
                                            *source_origin
                                        )) {
                                        return S_OK;
                                    }

                                    PWSTR message_raw = nullptr;
                                    const HRESULT message_extract_hr =
                                        args->TryGetWebMessageAsString(
                                            &message_raw
                                        );
                                    if (FAILED(message_extract_hr) ||
                                        !message_raw) {
                                        if (message_raw) {
                                            CoTaskMemFree(message_raw);
                                        }
                                        return S_OK;
                                    }

                                    const std::wstring message_wide(message_raw);
                                    CoTaskMemFree(message_raw);
                                    const std::string message =
                                        wstring_to_string(message_wide);

                                    if (!message.empty() && active->on_message) {
                                        active->on_message(message);
                                    }

                                    return S_OK;
                                }
                            ).Get(),
                            &operation->web_message_token
                        );

                    if (FAILED(message_hr)) {
                        std::cerr
                            << "[Kira Transport Error] "
                               "add_WebMessageReceived failed, hr=0x"
                            << std::hex << message_hr << std::endl;
                        WebViewTransport::cleanup_state(operation);
                        operation->complete_attach(message_hr);
                        return S_OK;
                    }

                    // The state itself is now the committed transport state.
                    operation->ready.store(true);
                    operation->complete_attach(S_OK);
                    return S_OK;
                }
            ).Get()
        );

    if (FAILED(start_hr)) {
        // Keep a local strong reference because cleanup may clear state_.
        auto failed_state = std::exchange(state_, nullptr);
        cleanup_state(failed_state);
        if (failed_state) {
            failed_state->complete_attach(start_hr);
        }
    }
}

bool WebViewTransport::send_message(
    const std::string& raw_utf8_message
) {
    if (!check_ui_thread()) {
        return false;
    }

    const auto state = state_;
    if (!state ||
        !state->alive.load() ||
        !state->ready.load() ||
        !state->webview) {
        return false;
    }

    const std::wstring message = string_to_wstring(raw_utf8_message);
    if (message.empty() && !raw_utf8_message.empty()) {
        return false;
    }

    const HRESULT hr =
        state->webview->PostWebMessageAsString(message.c_str());
    if (FAILED(hr)) {
        std::cerr
            << "[Kira Transport Error] PostWebMessageAsString failed, hr=0x"
            << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

} // namespace kira
