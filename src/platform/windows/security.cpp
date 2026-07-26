#include "src/platform/windows/security.hpp"
#include <algorithm>
#include <iostream>
#include <windows.h>

namespace kira {

std::optional<ParsedOrigin> SecurityPolicy::parse_origin(std::string_view url_str) {
    if (url_str.empty()) return std::nullopt;

    size_t scheme_end = url_str.find("://");
    if (scheme_end == std::string_view::npos) return std::nullopt;

    std::string scheme(url_str.substr(0, scheme_end));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    std::string_view remainder = url_str.substr(scheme_end + 3);
    size_t path_start = remainder.find('/');
    std::string_view host_port = (path_start == std::string_view::npos) ? remainder : remainder.substr(0, path_start);

    if (host_port.empty()) return std::nullopt;

    std::string host;
    int port = 0;

    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string_view::npos) {
        host = std::string(host_port.substr(0, colon_pos));
        std::string port_str(host_port.substr(colon_pos + 1));
        try {
            port = std::stoi(port_str);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        host = std::string(host_port);
        if (scheme == "http") port = 80;
        else if (scheme == "https") port = 443;
    }

    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return ParsedOrigin{
        .scheme = std::move(scheme),
        .host = std::move(host),
        .port = port
    };
}

bool SecurityPolicy::matches_origin(const ParsedOrigin& a, const ParsedOrigin& b) {
    return a.scheme == b.scheme && a.host == b.host && a.port == b.port;
}

bool SecurityPolicy::is_approved_origin(std::string_view source_uri, const WindowConfig& config) {
    auto src_opt = parse_origin(source_uri);
    if (!src_opt.has_value()) return false;

    if (!config.dev_url.empty()) {
        auto dev_opt = parse_origin(config.dev_url);
        if (!dev_opt.has_value()) return false;
        return matches_origin(*src_opt, *dev_opt);
    } else {
        // Production virtual origin: https://kira.local/
        auto prod_opt = parse_origin("https://kira.local/");
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
