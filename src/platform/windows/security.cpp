#include "src/platform/windows/security.hpp"
#include <algorithm>
#include <iostream>
#include <shlwapi.h>

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

std::optional<NormalizedOrigin> SecurityPolicy::parse_and_normalize_origin(std::string_view url_str) {
    if (url_str.empty()) return std::nullopt;

    std::wstring wurl = string_to_wstring(std::string(url_str));
    Microsoft::WRL::ComPtr<IUri> uri;

    HRESULT hr = CreateUri(wurl.c_str(), Uri_CREATE_CANONICALIZE, 0, &uri);
    if (FAILED(hr) || !uri) {
        return std::nullopt;
    }

    // Reject userinfo (username / password in URL)
    BSTR userinfo_bstr = nullptr;
    if (SUCCEEDED(uri->GetUserInfo(&userinfo_bstr)) && userinfo_bstr) {
        bool has_userinfo = (SysStringLen(userinfo_bstr) > 0);
        SysFreeString(userinfo_bstr);
        if (has_userinfo) return std::nullopt;
    }

    // Get Scheme
    BSTR scheme_bstr = nullptr;
    if (FAILED(uri->GetSchemeName(&scheme_bstr)) || !scheme_bstr) {
        return std::nullopt;
    }
    std::string scheme = wstring_to_string(scheme_bstr);
    SysFreeString(scheme_bstr);

    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    // Get Host
    BSTR host_bstr = nullptr;
    if (FAILED(uri->GetHost(&host_bstr)) || !host_bstr) {
        return std::nullopt;
    }
    std::string host = wstring_to_string(host_bstr);
    SysFreeString(host_bstr);

    if (host.empty()) return std::nullopt;

    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    // Get Port
    DWORD port = 0;
    if (FAILED(uri->GetPort(&port))) {
        return std::nullopt;
    }

    // Default port normalization
    if (port == 0) {
        if (scheme == "http") port = 80;
        else if (scheme == "https") port = 443;
    }

    if (port < 1 || port > 65535) {
        return std::nullopt;
    }

    return NormalizedOrigin{
        .scheme = std::move(scheme),
        .host = std::move(host),
        .port = port
    };
}

bool SecurityPolicy::matches_origin(const NormalizedOrigin& a, const NormalizedOrigin& b) {
    return a.scheme == b.scheme && a.host == b.host && a.port == b.port;
}

bool SecurityPolicy::is_approved_origin(std::string_view source_uri, const WindowConfig& config) {
    auto src_opt = parse_and_normalize_origin(source_uri);
    if (!src_opt.has_value()) return false;

    if (!config.dev_url.empty()) {
        auto dev_opt = parse_and_normalize_origin(config.dev_url);
        if (!dev_opt.has_value()) return false;
        return matches_origin(*src_opt, *dev_opt);
    } else {
        // Production virtual origin: https://kira.local/
        auto prod_opt = parse_and_normalize_origin("https://kira.local/");
        if (!prod_opt.has_value()) return false;
        return matches_origin(*src_opt, *prod_opt);
    }
}

std::optional<std::filesystem::path> SecurityPolicy::validate_asset_directory(const std::string& user_asset_dir) {
    std::filesystem::path candidate;
    if (!user_asset_dir.empty()) {
        candidate = std::filesystem::absolute(user_asset_dir);
    } else {
        wchar_t exe_path_buf[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path_buf, MAX_PATH);
        std::filesystem::path exe_dir = std::filesystem::path(exe_path_buf).parent_path();
        candidate = exe_dir / "assets";
    }

    std::error_code ec;
    candidate = std::filesystem::canonical(candidate, ec);
    if (ec || !std::filesystem::exists(candidate) || !std::filesystem::is_directory(candidate)) {
        return std::nullopt;
    }

    return candidate;
}

} // namespace kira
