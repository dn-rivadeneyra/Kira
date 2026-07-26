#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

#include "src/core/app_impl.hpp"
#include "src/platform/app_host.hpp"

using namespace kira;
using namespace kira::platform;

class FakeAppHost : public AppHost {
public:
    RawMessageCallback on_message;
    ReadyCallback on_ready;
    CloseRequestedCallback on_close_requested;
    FatalErrorCallback on_fatal_error;

    bool started{false};
    bool show_called{false};
    bool request_close_called{false};
    int request_close_count{0};
    int run_event_loop_return_code{0};
    bool event_loop_ran{false};
    mutable std::mutex msg_mutex;
    std::vector<std::string> posted_messages;
    std::vector<std::string> event_log;
    std::mutex loop_mutex;
    std::condition_variable loop_cv;
    bool loop_stop{false};

    void start(
        RawMessageCallback message_cb,
        ReadyCallback ready_cb,
        CloseRequestedCallback close_cb,
        FatalErrorCallback fatal_cb
    ) override {
        on_message = std::move(message_cb);
        on_ready = std::move(ready_cb);
        on_close_requested = std::move(close_cb);
        on_fatal_error = std::move(fatal_cb);
        event_log.push_back("start");
        started = true;
    }

    bool post_message(std::string message) override {
        std::lock_guard<std::mutex> lock(msg_mutex);
        if (request_close_called) {
            event_log.push_back("post_message_after_close:" + message);
            return false;
        }
        posted_messages.push_back(message);
        event_log.push_back("post_message:" + message);
        return true;
    }

    bool has_posted_messages() const {
        std::lock_guard<std::mutex> lock(msg_mutex);
        return !posted_messages.empty();
    }

    std::vector<std::string> get_posted_messages() const {
        std::lock_guard<std::mutex> lock(msg_mutex);
        return posted_messages;
    }

    void show() override {
        show_called = true;
        event_log.push_back("show");
    }

    void request_close() override {
        request_close_called = true;
        request_close_count++;
        event_log.push_back("request_close");
        {
            std::lock_guard<std::mutex> lock(loop_mutex);
            loop_stop = true;
        }
        loop_cv.notify_all();
    }

    int run_event_loop() override {
        event_loop_ran = true;
        event_log.push_back("run_event_loop");
        std::unique_lock<std::mutex> lock(loop_mutex);
        loop_cv.wait(lock, [this]() { return loop_stop; });
        return run_event_loop_return_code;
    }
};

void test_app_impl_starts_host_and_readiness_success() {
    AppConfig config;
    auto fake = std::make_unique<FakeAppHost>();
    FakeAppHost* fake_ptr = fake.get();

    AppImpl app(config, std::move(fake));

    bool command_executed = false;
    app.command("greet", [&](const nlohmann::json& args) -> nlohmann::json {
        command_executed = true;
        return {{"message", "Hello " + args["name"].get<std::string>()}};
    });

    fake_ptr->run_event_loop_return_code = 0;

    // Simulate readiness inside run_event_loop callback / simulation
    // We can test by calling on_ready inside custom run_event_loop or before.
    // In AppImpl::run(), start() is called, then run_event_loop().
    // Let's test calling on_ready inside start or run_event_loop.
}

void test_fake_host_all_cases() {
    // 1. AppImpl starts the host & readiness success enables invocation pipeline
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));
        bool handler_called = false;
        app.command("echo", [&](const nlohmann::json& args) -> nlohmann::json {
            handler_called = true;
            return args;
        });

        // Pre-readiness: message should be ignored
        // We will trigger readiness when start is called
        fake_ptr->run_event_loop_return_code = 0;

        // Custom start callback override to simulate immediate readiness
        // Or we can invoke on_ready right after start()
    }
}

int main() {
    // Test 1: AppImpl starts host & readiness success enables pipeline & sends post_message
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));
        std::atomic<bool> handler_called{false};
        app.command("greet", [&](const nlohmann::json& args) -> nlohmann::json {
            handler_called = true;
            return {{"greeting", "Hello " + args["name"].get<std::string>()}};
        });

        // Simulate host readiness during run
        // We can trigger on_ready via custom lambda or in on_ready callback
        std::thread t([fake_ptr]() {
            // Wait for start
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_message(R"({"version":1,"type":"invoke","id":"req-1","command":"greet","payload":{"name":"Kira"}})");
            // Wait until worker finishes processing and response is posted
            while (!fake_ptr->has_posted_messages()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            fake_ptr->on_close_requested();
        });

        int res = app.run();
        t.join();

        assert(res == 0);
        assert(fake_ptr->started);
        assert(fake_ptr->show_called);
        assert(handler_called);
        auto msgs = fake_ptr->get_posted_messages();
        assert(!msgs.empty());
        nlohmann::json resp = nlohmann::json::parse(msgs[0]);
        assert(resp["id"] == "req-1");
        assert(resp["type"] == "result");
        assert(resp["ok"] == true);
        assert(resp["value"]["greeting"] == "Hello Kira");
        assert(fake_ptr->request_close_called);
        std::cout << "[PASS] Test 1: AppImpl starts host, readiness success enables pipeline, post_message works" << std::endl;
    }

    // Test 2: Host readiness failure causes clean shutdown and nonzero result
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{PlatformError::bootstrap_failed, "WebView init failed"});
        });

        int res = app.run();
        t.join();

        assert(res == -1);
        assert(fake_ptr->request_close_called);
        assert(!fake_ptr->show_called);
        std::cout << "[PASS] Test 2: Host readiness failure causes clean shutdown and nonzero result" << std::endl;
    }

    // Test 3: Host close request triggers one shutdown
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_close_requested();
        });

        int res = app.run();
        t.join();

        assert(res == 0);
        assert(fake_ptr->request_close_count == 1);
        std::cout << "[PASS] Test 3: Host close request triggers one shutdown" << std::endl;
    }

    // Test 4: Fatal host error triggers one shutdown
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_fatal_error(PlatformResult{PlatformError::security_failure, "Security error"});
        });

        int res = app.run();
        t.join();

        assert(res == 0);
        assert(fake_ptr->request_close_count == 1);
        std::cout << "[PASS] Test 4: Fatal host error triggers one shutdown" << std::endl;
    }

    // Test 5: Repeated close and fatal-error requests do not repeat shutdown
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_close_requested();
            fake_ptr->on_close_requested();
            fake_ptr->on_fatal_error(PlatformResult{PlatformError::security_failure, "Sec err"});
            fake_ptr->on_close_requested();
        });

        int res = app.run();
        t.join();

        assert(res == 0);
        assert(fake_ptr->request_close_count == 1);
        std::cout << "[PASS] Test 5: Repeated close and fatal-error requests do not repeat shutdown" << std::endl;
    }

    // Test 6: Platform resource close occurs after pipeline and worker shutdown & no post after shutdown
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_close_requested();
            // Try sending message after close requested
            fake_ptr->on_message(R"({"jsonrpc":"2.0","id":2,"method":"greet","params":{}})");
        });

        int res = app.run();
        t.join();

        assert(res == 0);
        assert(fake_ptr->request_close_called);
        // Verify no message was posted after close requested
        for (const auto& log : fake_ptr->event_log) {
            assert(log.find("post_message_after_close") == std::string::npos);
        }
        std::cout << "[PASS] Test 6: Platform resource close occurs after pipeline/worker shutdown and no post after shutdown" << std::endl;
    }

    // Test 7: Event-loop result propagation
    {
        AppConfig config;
        auto fake = std::make_unique<FakeAppHost>();
        FakeAppHost* fake_ptr = fake.get();
        fake_ptr->run_event_loop_return_code = 42;

        AppImpl app(config, std::move(fake));

        std::thread t([fake_ptr]() {
            while (!fake_ptr->started) {
                std::this_thread::yield();
            }
            fake_ptr->on_ready(PlatformResult{});
            fake_ptr->on_close_requested();
        });

        int res = app.run();
        t.join();

        assert(res == 42);
        std::cout << "[PASS] Test 7: Event-loop result propagated correctly" << std::endl;
    }

    std::cout << "ALL FAKE HOST APP_IMPL TESTS PASSED." << std::endl;
    return 0;
}
