#include "server/bootstrap.h"

#include <chrono>
#include <filesystem>
#include <thread>

#include "core/kociemba.h"
#include "core/optimal.h"
#include "server/infra/logger.h"

namespace zc::server {

namespace {

std::string cornerDbPath(const Config &c) { return (std::filesystem::path(c.dataDir) / "corner.db").string(); }

}  // namespace

std::shared_ptr<SolveService> buildSolveService(const Config &config, const std::atomic<bool> *shutdown) {
    auto t0 = std::chrono::steady_clock::now();
    auto tables = KociembaTables::build();
    log().info("kociemba tables ready",
               {{"ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()},
                {"bytes", tables->memoryBytes()}});

    std::shared_ptr<const OptimalSolver> optimal;
    if (auto db = OptimalSolver::loadDatabase(cornerDbPath(config))) {
        optimal = std::make_shared<OptimalSolver>(db);
        log().info("corner pattern database loaded", {{"path", cornerDbPath(config)}});
    } else {
        log().warn("corner pattern database not found: optimal solver disabled (run with --build-tables)",
                   {{"path", cornerDbPath(config)}});
    }
    return std::make_shared<SolveService>(config, tables, optimal, shutdown);
}

std::shared_ptr<AccountServices> connectAccounts(const Config &config, const std::atomic<bool> *shutdown) {
    Storage storage;
    if (config.databaseUrl.empty()) {
        log().warn("DATABASE_URL not set: using the in-memory store (accounts and runs are lost on restart)");
        storage = makeMemoryStorage();
    } else {
        // Serverless Postgres can take a while to wake, and a database outage
        // shouldn't stop the solver, so keep retrying in the background.
        auto delay = std::chrono::milliseconds(500);
        for (int attempt = 1;; ++attempt) {
            if (shutdown && shutdown->load()) return nullptr;
            try {
                storage = makePostgresStorage(config.databaseUrl, config.dbPoolSize);
                log().info("connected to postgres", {{"attempt", attempt}});
                break;
            } catch (const std::exception &e) {
                log().warn("database not reachable, retrying",
                           {{"attempt", attempt}, {"error", redactSecrets(e.what(), config.databaseUrl)},
                            {"retry_in_ms", delay.count()}});
                for (auto waited = std::chrono::milliseconds(0); waited < delay; waited += std::chrono::milliseconds(100)) {
                    if (shutdown && shutdown->load()) return nullptr;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                delay = std::min(delay * 2, std::chrono::milliseconds(30000));
            }
        }
    }
    auto accounts = std::make_shared<AccountServices>();
    accounts->storage = storage;
    accounts->auth = std::make_shared<AuthService>(config, storage.users);
    accounts->runs = std::make_shared<RunService>(storage.runs, config.maxRunsPerUser);
    return accounts;
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
