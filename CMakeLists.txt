cmake_minimum_required(VERSION 3.16)
project(ZenCube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ZENCUBE_BUILD_SERVER "Build the HTTP backend (needs libpq and OpenSSL)" ON)
option(ZENCUBE_BUILD_TESTS "Build unit tests" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Original solver engine (models, BFS/DFS/IDDFS/IDA*, corner pattern database)
add_subdirectory(engine)

# Cube core library, HTTP service and tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/backend/CMakeLists.txt")
    add_subdirectory(backend)
endif()
