#include "server/bootstrap.h"

#include <chrono>
#include <filesystem>
#include <thread>

#include "core/kociemba.h"
#include "core/optimal.h"
#include "server/infra/crypto.h"
#include "server/infra/logger.h"

namespace zc::server {

namespace {

std::string cornerDbPath(const Config &c) { return (std::filesystem::path(c.dataDir) / "corner.db").string(); }

Storage connectStorage(const Config &config, const std::atomic<bool> *shutdown) {
    if (config.databaseUrl.empty()) {
        log().warn("DATABASE_URL not set: using the in-memory store (accounts and runs are lost on restart)");
        return makeMemoryStorage();
    }
    // Serverless Postgres may take a few seconds to wake up: retry with
    // exponential backoff instead of crash-looping.
    auto delay = std::chrono::milliseconds(500);
    for (int attempt = 1;; ++attempt) {
        try {
            Storage s = makePostgresStorage(config.databaseUrl, config.dbPoolSize);
            log().info("connected to postgres", {{"attempt", attempt}});
            return s;
        } catch (const std::exception &e) {
            if (attempt >= 8 || (shutdown && shutdown->load())) throw;
            log().warn("database not reachable yet, retrying", {{"attempt", attempt}, {"error", e.what()},
                                                                 {"retry_in_ms", delay.count()}});
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, std::chrono::milliseconds(8000));
        }
    }
}

}  // namespace

std::shared_ptr<Services> buildServices(const Config &config, const std::atomic<bool> *shutdown) {
    auto t0 = std::chrono::steady_clock::now();
    auto tables = KociembaTables::build();
    auto t1 = std::chrono::steady_clock::now();
    log().info("kociemba tables ready", {{"ms", std::chrono::duration<double, std::milli>(t1 - t0).count()},
                                         {"bytes", tables->memoryBytes()}});

    std::shared_ptr<const OptimalSolver> optimal;
    if (auto db = OptimalSolver::loadDatabase(cornerDbPath(config))) {
        optimal = std::make_shared<OptimalSolver>(db);
        log().info("corner pattern database loaded", {{"path", cornerDbPath(config)}});
    } else {
        log().warn("corner pattern database not found: optimal solver disabled (run with --build-tables)",
                   {{"path", cornerDbPath(config)}});
    }

    auto services = std::make_shared<Services>();
    services->storage = connectStorage(config, shutdown);
    services->solve = std::make_shared<SolveService>(config, tables, optimal, shutdown);
    services->auth = std::make_shared<AuthService>(config, services->storage.users);
    services->runs = std::make_shared<RunService>(services->storage.runs);
    return services;
}

int buildTables(const Config &config) {
    std::filesystem::create_directories(config.dataDir);
    auto tables = KociembaTables::build();
    log().info("building corner pattern database", {{"path", cornerDbPath(config)}});
    auto stats = buildCornerDatabase(*tables, cornerDbPath(config));
    log().info("corner pattern database written",
               {{"states", stats.states}, {"max_depth", stats.maxDepth}, {"seconds", stats.seconds}});
    return 0;
}

}  // namespace zc::server
