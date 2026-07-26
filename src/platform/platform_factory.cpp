#include "src/platform/platform_factory.hpp"

#ifdef _WIN32
#include "src/platform/windows/windows_app_host.hpp"
#endif

namespace kira::platform {

std::unique_ptr<AppHost> create_app_host(const WindowConfig& config) {
#ifdef _WIN32
    return std::make_unique<WindowsAppHost>(config);
#else
    static_assert(false, "Kira currently supports Windows only");
#endif
}

} // namespace kira::platform
