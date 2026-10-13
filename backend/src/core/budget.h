#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace zc {

// Limits for a CPU-bound search. Every solver checks this cooperatively, so a
// request can never hold a worker thread past its deadline.
struct SearchBudget {
    using Clock = std::chrono::steady_clock;

    Clock::time_point deadline = Clock::time_point::max();
    uint64_t maxNodes = 0;                     // 0 = unlimited
    const std::atomic<bool> *cancel = nullptr;  // e.g. set on server shutdown

    static SearchBudget withTimeout(std::chrono::milliseconds ms) {
        SearchBudget b;
        b.deadline = Clock::now() + ms;
        return b;
    }

    bool expired(uint64_t nodes) const {
        if (maxNodes && nodes >= maxNodes) return true;
        if (cancel && cancel->load(std::memory_order_relaxed)) return true;
        return Clock::now() >= deadline;
    }
};

}  // namespace zc
