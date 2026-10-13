#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "core/kociemba.h"
#include "core/optimal.h"
#include "core/validation.h"
#include "server/config.h"
#include "server/infra/bulkhead.h"
#include "server/infra/lru_cache.h"
#include "server/infra/singleflight.h"

namespace zc::server {

// One implementation per solving method; SolveService picks one by name.
class SolverStrategy {
public:
    virtual ~SolverStrategy() = default;
    virtual std::string id() const = 0;           // API name: beginner | kociemba | optimal
    virtual std::string displayName() const = 0;
    virtual std::string description() const = 0;
    virtual bool heavy() const { return false; }  // runs in its own bulkhead
    // Returns the response body. Throws ApiError (e.g. 504) or SolverBudgetExceeded.
    virtual nlohmann::json solve(const FaceletCube &cube, const CubieCube &cubie, const ColorNames &names,
                                 const SearchBudget &budget) const = 0;
};

struct SolverBudgetExceeded : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct SolveRequest {
    std::string cube;
    std::string method = "beginner";
    ColorNames names = defaultColorNames();
};

struct SolveResponse {
    nlohmann::json body;
    bool cacheHit = false;
    bool coalesced = false;
};

class SolveService {
public:
    SolveService(const Config &config, std::shared_ptr<const KociembaTables> tables,
                 std::shared_ptr<const OptimalSolver> optimal, const std::atomic<bool> *shutdown);

    SolveResponse solve(const SolveRequest &req);
    nlohmann::json validate(const std::string &cube, const ColorNames &names) const;
    nlohmann::json scramble() const;
    nlohmann::json describeSolvers() const;

    size_t cacheSize() const { return cache_.size(); }

private:
    nlohmann::json runStrategy(const SolverStrategy &s, const FaceletCube &fc, const CubieCube &cubie,
                               const ColorNames &names);

    const Config &config_;
    std::shared_ptr<const KociembaTables> tables_;
    std::map<std::string, std::unique_ptr<SolverStrategy>> strategies_;
    LruCache<std::string, nlohmann::json> cache_;
    SingleFlight<nlohmann::json> flights_;
    Bulkhead solveBulkhead_;
    Bulkhead heavyBulkhead_;
    const std::atomic<bool> *shutdown_;
};

// Human-readable description of a move ("Turn the right face clockwise: front goes up").
std::string describeMove(const Move &m);

}  // namespace zc::server
