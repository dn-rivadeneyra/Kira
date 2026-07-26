#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <windows.h>
#include <urlmon.h>
#include <wrl.h>
#include "kira/app.hpp"

namespace kira {

struct NormalizedOrigin {
    std::string scheme;
    std::string host;
    DWORD port{0};
};

class SecurityPolicy {
public:
    // Parses and normalizes URI using Windows CreateUri API. Returns std::nullopt if invalid or contains userinfo/invalid port.
    static std::optional<NormalizedOrigin> parse_and_normalize_origin(std::string_view url_str);

    // Compares two normalized origins for exact match
    static bool matches_origin(const NormalizedOrigin& a, const NormalizedOrigin& b);

    // Evaluates if source_uri is approved under current WindowConfig
    static bool is_approved_origin(std::string_view source_uri, const WindowConfig& config);

    // Validates local asset directory for production virtual host mapping
    static std::optional<std::filesystem::path> validate_asset_directory(const std::string& user_asset_dir);
};

} // namespace kira
