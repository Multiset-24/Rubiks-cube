find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)

add_library(zencube_server_lib STATIC
        config.cpp
        bootstrap.cpp
        api/app.cpp
        infra/logger.cpp
        infra/metrics.cpp
        infra/rate_limiter.cpp
        infra/crypto.cpp
        infra/jwt.cpp
        repo/memory_repo.cpp
        repo/pg_pool.cpp
        repo/pg_repo.cpp
        services/solve_service.cpp
        services/auth_service.cpp
        services/run_service.cpp
)
target_include_directories(zencube_server_lib PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/httplib")
# Third-party headers: no warnings from code we don't own.
target_include_directories(zencube_server_lib SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/httplib")
target_link_libraries(zencube_server_lib PUBLIC zencube_core OpenSSL::Crypto PostgreSQL::PostgreSQL Threads::Threads)

add_executable(zencube_server main.cpp)
target_link_libraries(zencube_server PRIVATE zencube_server_lib)
