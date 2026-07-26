#pragma once

#include <functional>
#include "src/core/gates.hpp"
#include "src/core/pipeline.hpp"
#include "src/core/worker.hpp"

namespace kira {

using ResourceCloseCallback = std::function<void()>;
using QuitPostCallback      = std::function<void()>;

class AppShutdownCoordinator {
public:
    AppShutdownCoordinator() = default;
    ~AppShutdownCoordinator() = default;

    // Production shutdown execution sequence:
    // 1. Check ShutdownGate (idempotent request guard).
    // 2. Shut down InvocationPipeline (stops accepting new IPC requests).
    // 3. Stop and join WorkerExecutor (completes running task, discards queued tasks).
    // 4. Close window and platform resources.
    // 5. Post quit message to event loop.
    bool execute_shutdown(
        ShutdownGate& gate,
        InvocationPipeline& pipeline,
        WorkerExecutor& worker,
        ResourceCloseCallback close_resources,
        QuitPostCallback post_quit
    );
};

} // namespace kira
