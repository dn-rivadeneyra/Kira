#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>
#include "src/core/types.hpp"

namespace kira {

using CommandHandler = std::function<nlohmann::json(const nlohmann::json&)>;

class CommandRegistry {
public:
    CommandRegistry() = default;
    ~CommandRegistry() = default;

    // Registers a command. Returns true on success, throws std::invalid_argument on invalid/duplicate name or empty handler.
    void register_command(const std::string& name, CommandHandler handler);

    // Checks if command exists
    bool has_command(const std::string& name) const;

    // Retrieves handler copy if present
    std::optional<CommandHandler> get_command(const std::string& name) const;

private:
    std::unordered_map<std::string, CommandHandler> handlers_;
};

} // namespace kira
