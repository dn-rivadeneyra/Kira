#include "kira/app.hpp"
#include <stdexcept>
#include <iostream>

namespace kira {

App::App(const AppConfig& config)
    : config_(config)
{
    window_ = std::make_unique<NativeWindow>(config_.window);
}

App::~App() = default;

void App::register_command(const std::string& name, CommandHandler handler) {
    dispatcher_.register_command(name, std::move(handler));
}

NativeWindow& App::window() {
    return *window_;
}

int App::run() {
    if (!window_->initialize()) {
        std::cerr << "[Kira] Failed to initialize native window." << std::endl;
        return -1;
    }

    // Wire bidirectional IPC bridge
    window_->set_on_web_message([this](const std::string& raw_message) {
        std::string response_json = dispatcher_.dispatch(raw_message);
        window_->post_web_message(response_json);
    });

    window_->show();

    // Event loop handled inside NativeWindow (win32 message pump)
    return 0;
}

} // namespace kira
