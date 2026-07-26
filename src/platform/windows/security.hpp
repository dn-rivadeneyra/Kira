#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include "kira/app.hpp"

namespace kira {

struct ParsedOrigin {
    std::string scheme;
    std::string host;
    int port{0};
};

class SecurityPolicy {
public:
    static std::optional<ParsedOrigin> parse_origin(std::string_view url_str);
    static bool matches_origin(const ParsedOrigin& a, const ParsedOrigin& b);
    static bool is_approved_origin(std::string_view source_uri, const WindowConfig& config);
    static std::optional<std::filesystem::path> validate_asset_directory(const std::string& user_asset_dir);
};

} // namespace kira
