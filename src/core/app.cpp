#include "kira/app.hpp"
#include "src/core/app_impl.hpp"

namespace kira {

AppImpl::AppImpl(const AppConfig& config)
    : AppImpl(config, platform::create_app_host(config.window)) {}

App::App(const AppConfig& config)
    : impl_(std::make_unique<AppImpl>(config)) {}

App::~App() = default;

void App::command(const std::string& name, CommandHandler handler) {
    impl_->command(name, std::move(handler));
}

int App::run() {
    return impl_->run();
}

} // namespace kira
