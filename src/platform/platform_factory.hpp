#pragma once

#include <memory>
#include "kira/app.hpp"
#include "src/platform/app_host.hpp"

namespace kira::platform {

std::unique_ptr<AppHost> create_app_host(
    const WindowConfig& config
);

} // namespace kira::platform
