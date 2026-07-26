#pragma once

#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "kira/export.hpp"

namespace kira {

using CommandHandler = std::function<nlohmann::json(const nlohmann::json&)>;

struct WindowConfig {
    std::string title = "Kira Application";
    int width = 1024;
    int height = 768;
    bool resizable = true;
    std::string dev_url = "";    // e.g. "http://localhost:5173"
    std::string asset_dir = "";  // Path to local assets directory (mapped to https://kira.local/)
};

struct AppConfig {
    WindowConfig window;
};

class AppImpl; // Forward declaration for internal implementation

class KIRA_API App {
public:
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Registers a command handler. Throws std::invalid_argument if name is invalid/duplicate or handler is empty.
    void command(const std::string& name, CommandHandler handler);

    // Runs the application event loop. Returns process exit code (non-zero on initialization failure).
    int run();

private:
    std::unique_ptr<AppImpl> impl_;
};

} // namespace kira
