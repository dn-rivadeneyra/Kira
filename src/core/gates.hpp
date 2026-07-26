#pragma once

#include <atomic>

namespace kira {

enum class InitializationResult {
    pending,
    succeeded,
    failed
};

class InitializationGate {
public:
    InitializationGate() : state_(InitializationResult::pending) {}
    ~InitializationGate() = default;

    // First completion attempt wins. Returns true if this call transitioned state, false if already completed.
    bool complete(bool success) {
        InitializationResult expected = InitializationResult::pending;
        InitializationResult desired = success ? InitializationResult::succeeded : InitializationResult::failed;
        return state_.compare_exchange_strong(expected, desired);
    }

    InitializationResult state() const {
        return state_.load();
    }

    bool is_completed() const {
        return state_.load() != InitializationResult::pending;
    }

private:
    std::atomic<InitializationResult> state_{InitializationResult::pending};
};

class ShutdownGate {
public:
    ShutdownGate() : shutdown_requested_(false) {}
    ~ShutdownGate() = default;

    // First shutdown request returns true. Subsequent requests return false.
    bool request() {
        return !shutdown_requested_.exchange(true);
    }

    bool requested() const {
        return shutdown_requested_.load();
    }

private:
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace kira
