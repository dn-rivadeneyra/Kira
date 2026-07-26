#pragma once

#include <string_view>

namespace kira {

inline bool is_valid_command_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) {
        return false;
    }

    char first = name[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) {
        return false;
    }

    for (size_t i = 1; i < name.size(); ++i) {
        char c = name[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  (c == '_') || (c == '.') || (c == '-');
        if (!ok) {
            return false;
        }
    }

    return true;
}

} // namespace kira
