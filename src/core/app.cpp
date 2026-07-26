#include "kira/app.hpp"
#include "src/core/types.hpp"
#include "src/core/gates.hpp"
#include "src/core/shutdown_coordinator.hpp"
#include "src/core/registry.hpp"
#include "src/core/protocol.hpp"
#include "src/core/executor.hpp"
#include "src/core/worker.hpp"
#include "src/core/pipeline.hpp"
#include "src/platform/windows/win32_window.hpp"

#include <iostream>
#include <memory>
#include <atomic>

namespace kira {

class AppImpl {
public:
    explicit AppImpl(const AppConfig& config)
        : config_(config),
          state_(ReadinessState::created),
          pipeline_(registry_, worker_, [this](const std::string& raw_resp) {
              if (state_ == ReadinessState::ready && window_) {
                  window_->post_ui_response(raw_resp);
              }
          }) {}

    ~AppImpl() {
        shutdown();
    }

    void command(const std::string& name, CommandHandler handler) {
        registry_.register_command(name, std::move(handler));
    }

    int run() {
        state_ = ReadinessState::initializing;

        bool init_success = false;
        bool init_finished = false;

        window_ = std::make_unique<NativeWindow>(
            config_.window,
            [this](const std::string& raw_msg) {
                if (state_ == ReadinessState::ready) {
                    pipeline_.process_raw_message(raw_msg);
                }
            },
            [&init_success, &init_finished](bool success) {
                init_success = success;
                init_finished = true;
            },
            [this]() {
                // Window close requested callback (WM_CLOSE).
                // Posts deferred application shutdown message to Win32 message queue so WndProc returns safely first.
                if (window_ && window_->get_hwnd()) {
                    BOOL posted = PostMessage(window_->get_hwnd(), WM_KIRA_APP_SHUTDOWN, 0, 0);
                    if (!posted) {
                        std::cerr << "[Kira App Error] PostMessage(WM_KIRA_APP_SHUTDOWN) failed, err=" << GetLastError() << std::endl;
                        PostThreadMessage(GetCurrentThreadId(), WM_KIRA_APP_SHUTDOWN, 0, 0);
                    }
                } else {
                    PostThreadMessage(GetCurrentThreadId(), WM_KIRA_APP_SHUTDOWN, 0, 0);
                }
            }
        );

        if (!window_->initialize()) {
            state_ = ReadinessState::failed;
            std::cerr << "[Kira Error] Native window initialization failed." << std::endl;
            shutdown();
            return -1;
        }

        // Win32 Message Loop - processes messages until initialization completes and app exits
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KIRA_APP_SHUTDOWN) {
                shutdown();
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (state_ == ReadinessState::initializing && init_finished) {
                if (init_success) {
                    state_ = ReadinessState::ready;
                    pipeline_.set_ready(true);
                    window_->show();
                } else {
                    state_ = ReadinessState::failed;
                    std::cerr << "[Kira Error] WebView2 initialization or navigation failed." << std::endl;
                    shutdown();
                    return -1;
                }
            }

            if (state_ == ReadinessState::closing || state_ == ReadinessState::closed) {
                break;
            }
        }

        if (state_ == ReadinessState::failed) {
            return -1;
        }

        shutdown();
        return 0;
    }

    void shutdown() {
        // Production AppShutdownCoordinator execution
        bool executed = coordinator_.execute_shutdown(
            shutdown_gate_,
            pipeline_,
            worker_,
            [this]() {
                state_ = ReadinessState::closing;
                if (window_) {
                    window_->close();
                    window_.reset();
                }
            },
            [this]() {
                PostQuitMessage(0);
                state_ = ReadinessState::closed;
            }
        );

        (void)executed;
    }

    ShutdownGate& shutdown_gate() { return shutdown_gate_; }

private:
    AppConfig config_;
    std::atomic<ReadinessState> state_{ReadinessState::created};
    ShutdownGate shutdown_gate_;
    AppShutdownCoordinator coordinator_;
    CommandRegistry registry_;
    WorkerExecutor worker_;
    InvocationPipeline pipeline_;
    std::unique_ptr<NativeWindow> window_;
};

// Public App wrapper methods
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
