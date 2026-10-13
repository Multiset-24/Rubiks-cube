#pragma once

#include <atomic>
#include <memory>

#include "server/api/app.h"
#include "server/config.h"

namespace zc::server {

// Builds the services (solver tables, pattern database, storage). This is the
// only place that chooses concrete implementations.
std::shared_ptr<Services> buildServices(const Config &config, const std::atomic<bool> *shutdown);

// Precomputes data files (corner pattern database) into config.dataDir.
int buildTables(const Config &config);

}  // namespace zc::server
