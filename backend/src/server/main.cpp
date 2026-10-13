// Zen Cube API server.
//
// Start-up is ordered for platforms like Render that health-check quickly:
//   1. bind the port immediately (liveness /healthz answers at once),
//   2. warm up in the background (solver tables, pattern database, database
//      connection + migrations), with /readyz returning 503 until done,
//   3. on SIGTERM, stop accepting connections, let in-flight requests finish
//      and cancel long searches via a shared flag.

#include <csignal>
#include <thread>

#include <httplib.h>

#include "server/api/app.h"
#include "server/bootstrap.h"
#include "server/config.h"
#include "server/infra/crypto.h"
#include "server/infra/logger.h"

using namespace zc::server;

namespace {
std::atomic<bool> gShutdown{false};
std::atomic<httplib::Server *> gServer{nullptr};

void onSignal(int) {
    gShutdown.store(true);
    // httplib::Server::stop() only flips flags and shuts the listening socket
    // down, which is safe to call from a signal handler.
    if (auto *s = gServer.load()) s->stop();
}
}  // namespace

int main(int argc, char **argv) {
    Config config;
    try {
        config = Config::fromEnv(argc, argv);
    } catch (const std::exception &e) {
        log().error("invalid configuration", {{"error", e.what()}});
        return 2;
    }
    log().setLevel(config.logLevel);

    if (config.buildTablesOnly) return buildTables(config);

    if (config.jwtSecret.empty()) {
        if (config.production()) {
            log().error("JWT_SECRET must be set when DATABASE_URL is set");
            return 2;
        }
        config.jwtSecret = crypto::randomHex(32);
        log().warn("JWT_SECRET not set: using a random secret (sessions end on restart)");
    }

    httplib::Server server;
    server.new_task_queue = [n = config.workerThreads] { return new httplib::ThreadPool(static_cast<size_t>(n)); };
    server.set_payload_max_length(config.maxBodyBytes);
    server.set_read_timeout(10, 0);
    server.set_write_timeout(10, 0);
    server.set_keep_alive_timeout(5);

    App app(config, &gShutdown);
    app.attach(server);

    std::thread warmup([&] {
        try {
            app.setServices(buildServices(config, &gShutdown));
            log().info("ready", {{"version", config.version}});
        } catch (const std::exception &e) {
            app.setStartupError(e.what());
            log().error("start-up failed; staying alive so /readyz can report it", {{"error", e.what()}});
        }
    });

    gServer.store(&server);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    log().info("listening", {{"host", config.host}, {"port", config.port}, {"version", config.version},
                             {"workers", config.workerThreads}});
    bool ok = server.listen(config.host, config.port);
    gServer.store(nullptr);
    gShutdown.store(true);
    warmup.join();
    if (!ok && !gShutdown.load()) {
        log().error("could not listen", {{"port", config.port}});
        return 1;
    }
    log().info("shut down cleanly");
    return 0;
}
