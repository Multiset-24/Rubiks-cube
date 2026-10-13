#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace zc::server {

// Minimal Prometheus-compatible metrics registry (text exposition format 0.0.4).
// Labels are a fixed, low-cardinality set chosen by the caller (route templates,
// never raw paths), which keeps memory bounded.
class Metrics {
public:
    using Labels = std::map<std::string, std::string>;

    static Metrics &instance();

    void inc(const std::string &name, const Labels &labels = {}, double by = 1);
    void set(const std::string &name, double value, const Labels &labels = {});
    void observe(const std::string &name, double value, const Labels &labels = {});

    void describe(const std::string &name, const std::string &type, const std::string &help);

    std::string render() const;
    void reset();  // tests

private:
    struct Histogram {
        std::vector<double> buckets;
        std::vector<uint64_t> counts;  // cumulative at render time
        double sum = 0;
        uint64_t count = 0;
    };
    struct Family {
        std::string type = "counter";
        std::string help;
        std::map<Labels, double> values;
        std::map<Labels, Histogram> histograms;
    };

    mutable std::mutex mu_;
    std::map<std::string, Family> families_;
};

}  // namespace zc::server
