#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace zc::server {

// Limits how many operations of one kind run at once, so slow optimal solves
// can't use up every worker. A caller that can't get a slot within the wait
// time is rejected (the API answers 503 with Retry-After) instead of queueing.
class Bulkhead {
public:
    Bulkhead(std::string name, int capacity) : name_(std::move(name)), capacity_(capacity) {}

    class Permit {
    public:
        Permit() = default;
        explicit Permit(Bulkhead *b) : b_(b) {}
        Permit(Permit &&o) noexcept : b_(o.b_) { o.b_ = nullptr; }
        Permit &operator=(Permit &&o) noexcept {
            release();
            b_ = o.b_;
            o.b_ = nullptr;
            return *this;
        }
        ~Permit() { release(); }
        explicit operator bool() const { return b_ != nullptr; }

    private:
        void release() {
            if (b_) b_->release();
            b_ = nullptr;
        }
        Bulkhead *b_ = nullptr;
    };

    Permit tryAcquire(std::chrono::milliseconds wait) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!cv_.wait_for(lock, wait, [&] { return inUse_ < capacity_; })) return Permit();
        ++inUse_;
        return Permit(this);
    }

    int inUse() const {
        std::lock_guard<std::mutex> lock(mu_);
        return inUse_;
    }
    int capacity() const { return capacity_; }
    const std::string &name() const { return name_; }

private:
    void release() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            --inUse_;
        }
        cv_.notify_one();
    }

    std::string name_;
    int capacity_;
    int inUse_ = 0;
    mutable std::mutex mu_;
    std::condition_variable cv_;
};

}  // namespace zc::server
