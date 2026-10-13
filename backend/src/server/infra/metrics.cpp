#include "server/infra/metrics.h"

#include <cmath>
#include <sstream>

namespace zc::server {

namespace {

const std::vector<double> kDefaultBuckets = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10};

std::string escape(const std::string &v) {
    std::string out;
    for (char c: v) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

std::string labelString(const Metrics::Labels &labels, const std::string &extraKey = "", const std::string &extraVal = "") {
    if (labels.empty() && extraKey.empty()) return "";
    std::string s = "{";
    bool first = true;
    for (const auto &[k, v]: labels) {
        if (!first) s += ",";
        s += k + "=\"" + escape(v) + "\"";
        first = false;
    }
    if (!extraKey.empty()) {
        if (!first) s += ",";
        s += extraKey + "=\"" + extraVal + "\"";
    }
    return s + "}";
}

std::string number(double v) {
    if (std::isinf(v)) return v > 0 ? "+Inf" : "-Inf";
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

}  // namespace

Metrics &Metrics::instance() {
    static Metrics m;
    return m;
}

void Metrics::describe(const std::string &name, const std::string &type, const std::string &help) {
    std::lock_guard<std::mutex> lock(mu_);
    auto &f = families_[name];
    f.type = type;
    f.help = help;
}

void Metrics::inc(const std::string &name, const Labels &labels, double by) {
    std::lock_guard<std::mutex> lock(mu_);
    families_[name].values[labels] += by;
}

void Metrics::set(const std::string &name, double value, const Labels &labels) {
    std::lock_guard<std::mutex> lock(mu_);
    auto &f = families_[name];
    if (f.type == "counter") f.type = "gauge";
    f.values[labels] = value;
}

void Metrics::observe(const std::string &name, double value, const Labels &labels) {
    std::lock_guard<std::mutex> lock(mu_);
    auto &f = families_[name];
    f.type = "histogram";
    auto &h = f.histograms[labels];
    if (h.buckets.empty()) {
        h.buckets = kDefaultBuckets;
        h.counts.assign(h.buckets.size(), 0);
    }
    for (size_t i = 0; i < h.buckets.size(); ++i) {
        if (value <= h.buckets[i]) {
            ++h.counts[i];
            break;
        }
    }
    h.sum += value;
    ++h.count;
}

std::string Metrics::render() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostringstream out;
    for (const auto &[name, f]: families_) {
        if (!f.help.empty()) out << "# HELP " << name << " " << f.help << "\n";
        out << "# TYPE " << name << " " << f.type << "\n";
        for (const auto &[labels, v]: f.values) out << name << labelString(labels) << " " << number(v) << "\n";
        for (const auto &[labels, h]: f.histograms) {
            uint64_t cumulative = 0;
            for (size_t i = 0; i < h.buckets.size(); ++i) {
                cumulative += h.counts[i];
                out << name << "_bucket" << labelString(labels, "le", number(h.buckets[i])) << " " << cumulative << "\n";
            }
            out << name << "_bucket" << labelString(labels, "le", "+Inf") << " " << h.count << "\n";
            out << name << "_sum" << labelString(labels) << " " << number(h.sum) << "\n";
            out << name << "_count" << labelString(labels) << " " << h.count << "\n";
        }
    }
    return out.str();
}

void Metrics::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    families_.clear();
}

}  // namespace zc::server
