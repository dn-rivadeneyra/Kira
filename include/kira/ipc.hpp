#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "kira/export.hpp"

namespace kira {

using CommandHandler = std::function<nlohmann::json(const nlohmann::json&)>;

class KIRA_API Dispatcher {
public:
    Dispatcher() = default;
    ~Dispatcher() = default;

    // Register a command handler
    void register_command(const std::string& name, CommandHandler handler);

    // Process incoming JSON payload string from JS, returning JSON response string
    std::string dispatch(const std::string& raw_json_message);

    // Check if command is registered
    bool has_command(const std::string& name) const;

private:
    std::unordered_map<std::string, CommandHandler> handlers_;
};

} // namespace kira
