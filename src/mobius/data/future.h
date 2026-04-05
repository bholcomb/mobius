#ifndef MOBIUS_DATA_FUTURE_H
#define MOBIUS_DATA_FUTURE_H

#include "data/value.h"
#include "internal/ref_counted.h"

#include <atomic>
#include <mutex>
#include <condition_variable>

enum class FutureState : uint8_t {
    PENDING,
    RESOLVED,
    REJECTED
};

class FutureValue : public RefCounted {
public:
    FutureValue() : state_(FutureState::PENDING) {}

    FutureState state() const {
        return state_.load(std::memory_order_acquire);
    }

    bool isResolved() const {
        return state_.load(std::memory_order_acquire) == FutureState::RESOLVED;
    }

    bool isRejected() const {
        return state_.load(std::memory_order_acquire) == FutureState::REJECTED;
    }

    bool isDone() const {
        auto s = state_.load(std::memory_order_acquire);
        return s == FutureState::RESOLVED || s == FutureState::REJECTED;
    }

    void resolve(const Value& val) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.load(std::memory_order_relaxed) != FutureState::PENDING) return;
        result_ = val;
        state_.store(FutureState::RESOLVED, std::memory_order_release);
        cv_.notify_all();
    }

    void reject(const Value& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.load(std::memory_order_relaxed) != FutureState::PENDING) return;
        error_ = err;
        state_.store(FutureState::REJECTED, std::memory_order_release);
        cv_.notify_all();
    }

    void cancel() {
        cancelled_.store(true, std::memory_order_release);
    }

    bool isCancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }

    const Value& result() const { return result_; }
    const Value& error() const { return error_; }

    std::mutex& mutex() { return mutex_; }
    std::condition_variable& cv() { return cv_; }

private:
    std::atomic<FutureState> state_;
    std::atomic<bool> cancelled_{false};
    Value result_;
    Value error_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

#endif // MOBIUS_DATA_FUTURE_H
