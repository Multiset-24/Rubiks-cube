#include "server/infra/rate_limiter.h"

#include <algorithm>

namespace zc::server {

RateLimiter::Decision RateLimiter::acquire(const std::string &key, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mu_);
    if (now - lastSweep_ > std::chrono::minutes(1)) evictIdle(now);
    auto [it, inserted] = buckets_.try_emplace(key, Bucket{burst_, now});
    Bucket &b = it->second;
    if (!inserted) {
        double elapsed = std::chrono::duration<double>(now - b.last).count();
        b.tokens = std::min(burst_, b.tokens + elapsed * rate_);
        b.last = now;
    }
    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return {true, 0.0, b.tokens};
    }
    return {false, (1.0 - b.tokens) / rate_, 0.0};
}

void RateLimiter::evictIdle(Clock::time_point now) {
    // A bucket idle long enough to be full again carries no state worth keeping.
    double refillSeconds = burst_ / rate_;
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (std::chrono::duration<double>(now - it->second.last).count() > refillSeconds) it = buckets_.erase(it);
        else ++it;
    }
    lastSweep_ = now;
}

size_t RateLimiter::trackedKeys() const {
    std::lock_guard<std::mutex> lock(mu_);
    return buckets_.size();
}

}  // namespace zc::server
