#pragma once

#include "kira/app.hpp"
#include "src/core/types.hpp"
#include "src/core/gates.hpp"
#include "src/core/shutdown_coordinator.hpp"
#include "src/core/registry.hpp"
#include "src/core/protocol.hpp"
#include "src/core/executor.hpp"
#include "src/core/worker.hpp"
#include "src/core/pipeline.hpp"
#include "src/platform/app_host.hpp"
#include "src/platform/platform_error.hpp"
#include "src/platform/platform_factory.hpp"

#include <memory>
#include <atomic>
#include <iostream>

namespace kira {

class AppImpl {
public:
    explicit AppImpl(const AppConfig& config);
    explicit AppImpl(const AppConfig& config, std::unique_ptr<platform::AppHost> host);

    ~AppImpl() {
        shutdown();
    }

    void command(const std::string& name, CommandHandler handler) {
        registry_.register_command(name, std::move(handler));
    }

    int run() {
        if (!host_) {
            return -1;
        }

        state_ = ReadinessState::initializing;
        init_result_ = platform::PlatformResult{platform::PlatformError::internal_error, "Initialization incomplete"};

        host_->start(
            [this](std::string raw_msg) {
                if (state_ == ReadinessState::ready) {
                    pipeline_.process_raw_message(raw_msg);
                }
            },
            [this](platform::PlatformResult result) {
                init_result_ = result;
                if (result.ok()) {
                    state_ = ReadinessState::ready;
                    pipeline_.set_ready(true);
                    host_->show();
                } else {
                    state_ = ReadinessState::failed;
                    std::cerr << "[Kira Error] Platform initialization failed: " << result.diagnostic << std::endl;
                    shutdown();
                }
            },
            [this]() {
                shutdown();
            },
            [this](platform::PlatformResult fatal_error) {
                std::cerr << "[Kira Error] Fatal platform error: " << fatal_error.diagnostic << std::endl;
                shutdown();
            }
        );

        int exit_code = host_->run_event_loop();

        if (!init_result_.ok()) {
            return -1;
        }

        shutdown();
        return exit_code;
    }

    void shutdown() {
        bool executed = coordinator_.execute_shutdown(
            shutdown_gate_,
            pipeline_,
            worker_,
            [this]() {
                state_ = ReadinessState::closing;
                if (host_) {
                    host_->request_close();
                }
            },
            [this]() {
                state_ = ReadinessState::closed;
            }
        );
        (void)executed;
    }

    ReadinessState get_state() const {
        return state_.load();
    }

    InvocationPipeline& get_pipeline() {
        return pipeline_;
    }

    platform::AppHost* get_host() const {
        return host_.get();
    }

private:
    AppConfig config_;
    std::unique_ptr<platform::AppHost> host_;
    std::atomic<ReadinessState> state_{ReadinessState::created};
    platform::PlatformResult init_result_;
    ShutdownGate shutdown_gate_;
    AppShutdownCoordinator coordinator_;
    CommandRegistry registry_;
    WorkerExecutor worker_;
    InvocationPipeline pipeline_;
};

inline AppImpl::AppImpl(const AppConfig& config, std::unique_ptr<platform::AppHost> host)
    : config_(config),
      host_(std::move(host)),
      state_(ReadinessState::created),
      pipeline_(registry_, worker_, [this](const std::string& raw_resp) {
          if (state_ == ReadinessState::ready && host_) {
              host_->post_message(raw_resp);
          }
      }) {}

} // namespace kira
