#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "src/core/app_impl.hpp"
#include "src/platform/app_host.hpp"

using namespace kira;
using namespace kira::platform;

class FakeAppHost final : public AppHost {
public:
    void start(
        RawMessageCallback message_cb,
        ReadyCallback ready_cb,
        CloseRequestedCallback close_cb,
        FatalErrorCallback fatal_cb
    ) override {
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            on_message_ = std::move(message_cb);
            on_ready_ = std::move(ready_cb);
            on_close_requested_ = std::move(close_cb);
            on_fatal_error_ = std::move(fatal_cb);
        }
        started_.store(true);
        started_cv_.notify_all();
    }

    bool post_message(std::string message) override {
        std::lock_guard<std::mutex> lock(message_mutex_);
        if (close_requested_.load()) {
            posted_after_close_.store(true);
            return false;
        }
        posted_messages_.push_back(std::move(message));
        message_cv_.notify_all();
        return true;
    }

    void show() override { shown_.store(true); }

    void request_close() override {
        if (before_request_close) {
            before_request_close();
        }
        close_requested_.store(true);
        request_close_count_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(loop_mutex_);
            loop_stop_ = true;
        }
        loop_cv_.notify_all();
    }

    int run_event_loop() override {
        event_loop_ran_.store(true);
        std::unique_lock<std::mutex> lock(loop_mutex_);
        loop_cv_.wait(lock, [this] { return loop_stop_; });
        return event_loop_return_code;
    }

    void wait_started() {
        std::unique_lock<std::mutex> lock(started_mutex_);
        started_cv_.wait(lock, [this] { return started_.load(); });
    }

    void trigger_ready(PlatformResult result = {}) {
        ReadyCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = on_ready_;
        }
        assert(callback);
        callback(std::move(result));
    }

    void trigger_message(std::string message) {
        RawMessageCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = on_message_;
        }
        assert(callback);
        callback(std::move(message));
    }

    void trigger_close() {
        CloseRequestedCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = on_close_requested_;
        }
        assert(callback);
        callback();
    }

    void trigger_fatal(PlatformResult result) {
        FatalErrorCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = on_fatal_error_;
        }
        assert(callback);
        callback(std::move(result));
    }

    std::vector<std::string> wait_for_messages(std::size_t count) {
        std::unique_lock<std::mutex> lock(message_mutex_);
        message_cv_.wait(lock, [this, count] { return posted_messages_.size() >= count; });
        return posted_messages_;
    }

    bool shown() const { return shown_.load(); }
    bool close_requested() const { return close_requested_.load(); }
    int request_close_count() const { return request_close_count_.load(); }
    bool posted_after_close() const { return posted_after_close_.load(); }

    int event_loop_return_code{0};
    std::function<void()> before_request_close;

private:
    std::mutex callback_mutex_;
    RawMessageCallback on_message_;
    ReadyCallback on_ready_;
    CloseRequestedCallback on_close_requested_;
    FatalErrorCallback on_fatal_error_;

    std::atomic<bool> started_{false};
    std::mutex started_mutex_;
    std::condition_variable started_cv_;

    std::atomic<bool> shown_{false};
    std::atomic<bool> close_requested_{false};
    std::atomic<int> request_close_count_{0};
    std::atomic<bool> event_loop_ran_{false};
    std::atomic<bool> posted_after_close_{false};

    std::mutex message_mutex_;
    std::condition_variable message_cv_;
    std::vector<std::string> posted_messages_;

    std::mutex loop_mutex_;
    std::condition_variable loop_cv_;
    bool loop_stop_{false};
};

int main() {
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        auto* host = fake.get();
        AppImpl app(config, std::move(fake));

        std::atomic<bool> handler_called{false};
        app.command("greet", [&](const nlohmann::json& args) {
            handler_called.store(true);
            return nlohmann::json{{"greeting", "Hello " + args.at("name").get<std::string>()}};
        });

        std::thread driver([host] {
            host->wait_started();
            host->trigger_ready();
            host->trigger_message(R"({"version":1,"type":"invoke","id":"req-1","command":"greet","payload":{"name":"Kira"}})");
            const auto messages = host->wait_for_messages(1);
            const auto response = nlohmann::json::parse(messages.front());
            assert(response.at("id") == "req-1");
            assert(response.at("ok") == true);
            assert(response.at("value").at("greeting") == "Hello Kira");
            host->trigger_close();
        });

        assert(app.run() == 0);
        driver.join();
        assert(handler_called.load());
        assert(host->shown());
        assert(host->request_close_count() == 1);
    }

    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        auto* host = fake.get();
        AppImpl app(config, std::move(fake));

        std::thread driver([host] {
            host->wait_started();
            host->trigger_ready({PlatformError::bootstrap_failed, "init failed"});
        });

        assert(app.run() != 0);
        driver.join();
        assert(!host->shown());
        assert(host->request_close_count() == 1);
    }

    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        auto* host = fake.get();
        AppImpl app(config, std::move(fake));

        std::thread driver([host] {
            host->wait_started();
            host->trigger_ready();
            host->trigger_fatal({PlatformError::security_failure, "fatal security error"});
        });

        assert(app.run() != 0);
        driver.join();
        assert(host->request_close_count() == 1);
    }

    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        auto* host = fake.get();
        AppImpl app(config, std::move(fake));

        std::atomic<bool> handler_started{false};
        std::atomic<bool> allow_finish{false};
        std::atomic<bool> handler_finished{false};

        app.command("slow", [&](const nlohmann::json&) {
            handler_started.store(true);
            while (!allow_finish.load()) {
                std::this_thread::yield();
            }
            handler_finished.store(true);
            return nlohmann::json{{"done", true}};
        });

        host->before_request_close = [&] {
            assert(!app.get_pipeline().is_ready());
            assert(handler_finished.load());
        };

        std::thread driver([host, &handler_started, &allow_finish] {
            host->wait_started();
            host->trigger_ready();
            host->trigger_message(R"({"version":1,"type":"invoke","id":"slow-1","command":"slow","payload":{}})");
            while (!handler_started.load()) {
                std::this_thread::yield();
            }
            std::thread releaser([&allow_finish] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                allow_finish.store(true);
            });
            host->trigger_close();
            releaser.join();
        });

        assert(app.run() == 0);
        driver.join();
        assert(handler_finished.load());
        assert(!host->posted_after_close());
        assert(host->request_close_count() == 1);
    }

    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        auto* host = fake.get();
        host->event_loop_return_code = 42;
        AppImpl app(config, std::move(fake));

        std::thread driver([host] {
            host->wait_started();
            host->trigger_ready();
            host->trigger_close();
        });

        assert(app.run() == 42);
        driver.join();
    }

    std::cout << "ALL FAKE HOST APPIMPL TESTS PASSED.\n";
    return 0;
}
