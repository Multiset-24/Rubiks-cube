#pragma once

#include <atomic>
#include <memory>

#include "server/api/app.h"
#include "server/config.h"

namespace zc::server {

// Builds everything that needs warm-up and returns the wired services
// (composition root: the only place that knows concrete implementations).
std::shared_ptr<Services> buildServices(const Config &config, const std::atomic<bool> *shutdown);

// Precomputes data files (corner pattern database) into config.dataDir.
int buildTables(const Config &config);

}  // namespace zc::server
