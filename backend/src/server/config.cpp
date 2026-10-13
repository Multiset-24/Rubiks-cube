#include "server/config.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace zc::server {

namespace {

std::string env(const char *name, const std::string &fallback) {
    const char *v = std::getenv(name);
    return v && *v ? std::string(v) : fallback;
}

template<typename T>
T envNumber(const char *name, T fallback) {
    const char *v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        if constexpr (std::is_floating_point_v<T>) return static_cast<T>(std::stod(v));
        else return static_cast<T>(std::stoll(v));
    } catch (const std::exception &) {
        throw std::invalid_argument(std::string("invalid number in ") + name + ": " + v);
    }
}

std::vector<std::string> splitList(const std::string &s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t/");
        if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
    }
    return out;
}

}  // namespace

Config Config::fromEnv(int argc, char **argv) {
    Config c;
    c.port = envNumber("PORT", c.port);
    // Render exposes the deployed commit; use it when no explicit version is set.
    std::string commit = env("RENDER_GIT_COMMIT", "");
    c.version = env("APP_VERSION", commit.empty() ? c.version : commit.substr(0, 7));
    c.logLevel = env("LOG_LEVEL", c.logLevel);
    c.dataDir = env("DATA_DIR", c.dataDir);
    c.databaseUrl = env("DATABASE_URL", "");
    c.dbPoolSize = envNumber("DB_POOL_SIZE", c.dbPoolSize);
    c.jwtSecret = env("JWT_SECRET", "");
    c.jwtTtlHours = envNumber("JWT_TTL_HOURS", c.jwtTtlHours);
    c.passwordHashIterations = envNumber("PASSWORD_HASH_ITERATIONS", c.passwordHashIterations);
    c.corsOrigins = splitList(env("CORS_ORIGINS", "*"));
    c.trustProxy = env("TRUST_PROXY", "true") != "false";
    c.workerThreads = envNumber("WORKER_THREADS", c.workerThreads);
    c.maxConcurrentSolves = envNumber("MAX_CONCURRENT_SOLVES", c.maxConcurrentSolves);
    c.maxConcurrentOptimal = envNumber("MAX_CONCURRENT_OPTIMAL", c.maxConcurrentOptimal);
    c.maxConcurrentAuth = envNumber("MAX_CONCURRENT_AUTH", c.maxConcurrentAuth);
    c.solveTimeout = std::chrono::milliseconds(envNumber("SOLVE_TIMEOUT_MS", static_cast<long long>(c.solveTimeout.count())));
    c.optimalTimeout = std::chrono::milliseconds(envNumber("OPTIMAL_TIMEOUT_MS", static_cast<long long>(c.optimalTimeout.count())));
    c.queueTimeout = std::chrono::milliseconds(envNumber("QUEUE_TIMEOUT_MS", static_cast<long long>(c.queueTimeout.count())));
    c.solveCacheSize = envNumber("SOLVE_CACHE_SIZE", c.solveCacheSize);
    c.rateLimitRps = envNumber("RATE_LIMIT_RPS", c.rateLimitRps);
    c.rateLimitBurst = envNumber("RATE_LIMIT_BURST", c.rateLimitBurst);
    c.solveRateRps = envNumber("SOLVE_RATE_RPS", c.solveRateRps);
    c.solveRateBurst = envNumber("SOLVE_RATE_BURST", c.solveRateBurst);
    c.authRatePerMinute = envNumber("AUTH_RATE_PER_MINUTE", c.authRatePerMinute);
    c.maxBodyBytes = envNumber("MAX_BODY_BYTES", c.maxBodyBytes);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--build-tables") c.buildTablesOnly = true;
        else if (a == "--port" && i + 1 < argc) c.port = std::stoi(argv[++i]);
        else if (a == "--data-dir" && i + 1 < argc) c.dataDir = argv[++i];
    }
    return c;
}

}  // namespace zc::server
