#pragma once

#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace zc::server {

// Thread-safe least-recently-used cache. Solving is a pure function of the cube
// state, so results can be cached forever; LRU bounds the memory.
template<typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    std::optional<V> get(const K &key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            return std::nullopt;
        }
        order_.splice(order_.begin(), order_, it->second);  // mark most recently used
        ++hits_;
        return it->second->second;
    }

    void put(const K &key, V value) {
        if (capacity_ == 0) return;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(key, std::move(value));
        index_[key] = order_.begin();
        if (index_.size() > capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return index_.size();
    }
    uint64_t hits() const { std::lock_guard<std::mutex> lock(mu_); return hits_; }
    uint64_t misses() const { std::lock_guard<std::mutex> lock(mu_); return misses_; }

private:
    size_t capacity_;
    mutable std::mutex mu_;
    std::list<std::pair<K, V>> order_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> index_;
    uint64_t hits_ = 0, misses_ = 0;
};

}  // namespace zc::server
