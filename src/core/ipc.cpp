#include "kira/ipc.hpp"
#include <iostream>

namespace kira {

void Dispatcher::register_command(const std::string& name, CommandHandler handler) {
    handlers_[name] = std::move(handler);
}

bool Dispatcher::has_command(const std::string& name) const {
    return handlers_.contains(name);
}

std::string Dispatcher::dispatch(const std::string& raw_json_message) {
    nlohmann::json response;
    std::string req_id = "";

    try {
        auto msg = nlohmann::json::parse(raw_json_message);
        
        if (msg.contains("id") && msg["id"].is_string()) {
            req_id = msg["id"].get<std::string>();
        }
        response["id"] = req_id;

        if (!msg.contains("command") || !msg["command"].is_string()) {
            response["status"] = "error";
            response["error"] = "Invalid message format: missing or non-string 'command' field";
            return response.dump();
        }

        std::string command = msg["command"].get<std::string>();
        nlohmann::json args = msg.value("args", nlohmann::json::object());

        auto it = handlers_.find(command);
        if (it == handlers_.end()) {
            response["status"] = "error";
            response["error"] = "Command not found: '" + command + "'";
            return response.dump();
        }

        try {
            nlohmann::json result = it->second(args);
            response["status"] = "ok";
            response["result"] = result;
        } catch (const std::exception& e) {
            response["status"] = "error";
            response["error"] = std::string("Command handler exception: ") + e.what();
        } catch (...) {
            response["status"] = "error";
            response["error"] = "Unknown command handler exception";
        }

    } catch (const nlohmann::json::parse_error& e) {
        response["id"] = req_id;
        response["status"] = "error";
        response["error"] = std::string("JSON parse error: ") + e.what();
    }

    return response.dump();
}

} // namespace kira
