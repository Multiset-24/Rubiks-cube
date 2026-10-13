# Zen Cube backend: cube core library (models, validation, solvers), HTTP service and tests.

# ---------------------------------------------------------------- core library
add_library(zencube_core STATIC
        src/core/moves.cpp
        src/core/facelet.cpp
        src/core/cubie.cpp
        src/core/validation.cpp
        src/core/kociemba.cpp
        src/core/beginner.cpp
        src/core/optimal.cpp
)
target_include_directories(zencube_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(zencube_core PUBLIC rubiks_engine)
target_compile_features(zencube_core PUBLIC cxx_std_17)

# ---------------------------------------------------------------- HTTP service
if(ZENCUBE_BUILD_SERVER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/server/CMakeLists.txt")
    add_subdirectory(src/server)
endif()

# ---------------------------------------------------------------- tests
if(ZENCUBE_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    file(GLOB ZENCUBE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    if(NOT ZENCUBE_BUILD_SERVER)
        list(FILTER ZENCUBE_TEST_SOURCES EXCLUDE REGEX "server_|auth_|repo_|infra_")
    endif()
    add_executable(zencube_tests ${ZENCUBE_TEST_SOURCES})
    target_link_libraries(zencube_tests PRIVATE zencube_core GTest::gtest GTest::gtest_main)
    if(TARGET zencube_server_lib)
        target_link_libraries(zencube_tests PRIVATE zencube_server_lib)
    endif()
    include(GoogleTest)
    gtest_discover_tests(zencube_tests DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 300)
endif()
