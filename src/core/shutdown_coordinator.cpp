#include "src/core/shutdown_coordinator.hpp"

namespace kira {

bool AppShutdownCoordinator::execute_shutdown(
    ShutdownGate& gate,
    InvocationPipeline& pipeline,
    WorkerExecutor& worker,
    ResourceCloseCallback close_resources,
    QuitPostCallback post_quit
) {
    if (!gate.request()) {
        return false; // Idempotent guard: first call returns true; subsequent calls return false
    }

    // 1. Pipeline shutdown: stop accepting work and discard new requests
    pipeline.shutdown();

    // 2. Worker stop and join: finish currently running task, discard queued tasks, join thread
    worker.stop_and_join();

    // 3. Resource closure: destroy WebView and Win32 window handles safely
    if (close_resources) {
        close_resources();
    }

    // 4. Post quit message to event loop after safe resource cleanup
    if (post_quit) {
        post_quit();
    }

    return true;
}

} // namespace kira
