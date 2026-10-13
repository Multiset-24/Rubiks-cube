#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace zc::server {

// Token bucket per key (client IP). Each bucket refills at `rate` tokens/second
// up to `burst`; a request costs one token. Allows short bursts while bounding
// the sustained rate. Idle buckets are evicted to bound memory.
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    RateLimiter(double ratePerSecond, double burst) : rate_(ratePerSecond), burst_(burst) {}

    struct Decision {
        bool allowed;
        double retryAfterSeconds;  // when not allowed
        double remaining;
    };

    Decision acquire(const std::string &key, Clock::time_point now = Clock::now());

    size_t trackedKeys() const;

private:
    struct Bucket {
        double tokens;
        Clock::time_point last;
    };
    void evictIdle(Clock::time_point now);

    double rate_, burst_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;
    Clock::time_point lastSweep_ = Clock::now();
};

}  // namespace zc::server
