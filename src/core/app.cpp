#include "kira/app.hpp"
#include "src/core/types.hpp"
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
                    PostMessage(window_->get_hwnd(), WM_KIRA_APP_SHUTDOWN, 0, 0);
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
        if (shutdown_requested_.exchange(true)) {
            return; // Idempotent guard: first shutdown request wins, subsequent calls ignored
        }

        // 1. Mark app as closing and stop accepting new IPC requests
        state_ = ReadinessState::closing;
        pipeline_.shutdown();

        // 2. Stop worker: finish current task, discard queued, join thread
        worker_.stop_and_join();

        // 3. Destroy window and transport safely outside WndProc callback
        if (window_) {
            window_->close();
            window_.reset();
        }

        // 4. Post quit message after safe resource cleanup
        PostQuitMessage(0);

        // 5. Mark app as closed
        state_ = ReadinessState::closed;
    }

private:
    AppConfig config_;
    std::atomic<ReadinessState> state_{ReadinessState::created};
    std::atomic<bool> shutdown_requested_{false};
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
