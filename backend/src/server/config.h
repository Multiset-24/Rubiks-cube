#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace zc::server {

// Runtime configuration, read from environment variables so the same image
// runs locally, in CI and on Render.
struct Config {
    std::string host = "0.0.0.0";
    int port = 8080;                     // PORT
    std::string version = "dev";         // APP_VERSION (set by the Docker build)
    std::string logLevel = "info";       // LOG_LEVEL: debug | info | warn | error
    std::string dataDir = "./data";      // DATA_DIR: precomputed tables (corner DB)
    std::string databaseUrl;             // DATABASE_URL: Postgres; empty = in-memory store
    int dbPoolSize = 4;                  // DB_POOL_SIZE
    std::string jwtSecret;               // JWT_SECRET (required in production)
    int jwtTtlHours = 24 * 7;            // JWT_TTL_HOURS
    int passwordHashIterations = 150000; // PASSWORD_HASH_ITERATIONS (PBKDF2-HMAC-SHA256)
    std::vector<std::string> corsOrigins = {"*"};  // CORS_ORIGINS, comma-separated
    // Number of proxies in front of the server that append to X-Forwarded-For.
    // 0 = use the socket address. N = take the Nth entry from the right; entries
    // further left were sent by the client and can be forged.
    int trustedProxyHops = 0;            // TRUSTED_PROXY_HOPS
    std::string metricsToken;            // METRICS_TOKEN: if set, /metrics needs "Bearer <token>"
    int workerThreads = 8;               // WORKER_THREADS
    int maxConcurrentSolves = 4;         // MAX_CONCURRENT_SOLVES (bulkhead)
    int maxConcurrentOptimal = 1;        // MAX_CONCURRENT_OPTIMAL
    int maxConcurrentAuth = 2;           // MAX_CONCURRENT_AUTH (password hashing is CPU-heavy)
    std::chrono::milliseconds solveTimeout{4000};    // SOLVE_TIMEOUT_MS
    std::chrono::milliseconds optimalTimeout{3000};  // OPTIMAL_TIMEOUT_MS
    std::chrono::milliseconds queueTimeout{2000};    // QUEUE_TIMEOUT_MS: wait for a bulkhead slot
    size_t solveCacheSize = 2000;        // SOLVE_CACHE_SIZE (entries)
    double rateLimitRps = 10;            // RATE_LIMIT_RPS (per client IP, all routes)
    double rateLimitBurst = 40;          // RATE_LIMIT_BURST
    double solveRateRps = 2;             // SOLVE_RATE_RPS (per client IP, /solve)
    double solveRateBurst = 10;          // SOLVE_RATE_BURST
    double authRatePerMinute = 10;       // AUTH_RATE_PER_MINUTE (per IP, login/register)
    double runWritesPerMinute = 30;      // RUN_WRITES_PER_MINUTE (per user, POST /runs and /runs/batch)
    int maxRunsPerUser = 5000;           // MAX_RUNS_PER_USER
    size_t maxBodyBytes = 256 * 1024;    // MAX_BODY_BYTES
    bool buildTablesOnly = false;        // --build-tables: precompute data files and exit

    static Config fromEnv(int argc, char **argv);
    bool production() const { return !databaseUrl.empty(); }
};

}  // namespace zc::server
