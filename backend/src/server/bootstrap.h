#pragma once

#include <atomic>
#include <memory>

#include "server/api/app.h"
#include "server/config.h"

namespace zc::server {

// Builds the solver (Kociemba tables, optional pattern database). No I/O besides
// reading the database file.
std::shared_ptr<SolveService> buildSolveService(const Config &config, const std::atomic<bool> *shutdown);

// Connects storage and builds the account services. With Postgres it retries with
// backoff until it succeeds or `shutdown` is set (then returns nullptr). This is
// the only place that chooses concrete storage.
std::shared_ptr<AccountServices> connectAccounts(const Config &config, const std::atomic<bool> *shutdown);

// Precomputes data files (corner pattern database) into config.dataDir.
int buildTables(const Config &config);

}  // namespace zc::server
