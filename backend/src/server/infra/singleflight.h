#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

namespace zc::server {

// Concurrent callers asking for the same key share one computation (like Go's
// x/sync/singleflight). Used so identical solve requests that miss the cache at
// the same time run the solver once.
template<typename V>
class SingleFlight {
public:
    struct Outcome {
        V value;
        bool shared;  // true if this caller reused another caller's computation
    };

    Outcome run(const std::string &key, const std::function<V()> &fn) {
        std::shared_future<V> fut;
        std::promise<V> promise;
        bool leader = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = inflight_.find(key);
            if (it != inflight_.end()) {
                fut = it->second;
            } else {
                fut = promise.get_future().share();
                inflight_[key] = fut;
                leader = true;
            }
        }
        if (!leader) return {fut.get(), true};  // rethrows the leader's exception
        try {
            promise.set_value(fn());
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            inflight_.erase(key);
        }
        return {fut.get(), false};
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_future<V>> inflight_;
};

}  // namespace zc::server
