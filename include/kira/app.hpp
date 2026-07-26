#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "kira/export.hpp"
#include "kira/window.hpp"
#include "kira/ipc.hpp"

namespace kira {

struct AppConfig {
    std::string app_id = "com.kira.app";
    WindowConfig window;
};

class KIRA_API App {
public:
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Register IPC command
    void register_command(const std::string& name, CommandHandler handler);

    // Initialize window and run main event loop
    int run();

    // Access underlying window
    NativeWindow& window();

private:
    AppConfig config_;
    Dispatcher dispatcher_;
    std::unique_ptr<NativeWindow> window_;
};

} // namespace kira
