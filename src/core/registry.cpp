#include "src/core/registry.hpp"
#include "src/core/validator.hpp"
#include <stdexcept>

namespace kira {

void CommandRegistry::register_command(const std::string& name, CommandHandler handler) {
    if (!is_valid_command_name(name)) {
        throw std::invalid_argument("Invalid command name: '" + name + "'");
    }
    if (!handler) {
        throw std::invalid_argument("Command handler cannot be empty for command: '" + name + "'");
    }
    if (handlers_.contains(name)) {
        throw std::invalid_argument("Duplicate command registration for command: '" + name + "'");
    }
    handlers_[name] = std::move(handler);
}

bool CommandRegistry::has_command(const std::string& name) const {
    return handlers_.contains(name);
}

std::optional<CommandHandler> CommandRegistry::get_command(const std::string& name) const {
    auto it = handlers_.find(name);
    if (it != handlers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace kira
