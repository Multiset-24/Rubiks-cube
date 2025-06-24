cmake_minimum_required(VERSION 3.16)
project(RubiksCube)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB MODEL_SOURCES
        "${CMAKE_SOURCE_DIR}/Model/*.cpp"
        "${CMAKE_SOURCE_DIR}/Model/*.h"
        "${CMAKE_SOURCE_DIR}/Solvers/*.h"
        "${CMAKE_SOURCE_DIR}/Pattern_Database/*.h"
        "${CMAKE_SOURCE_DIR}/Pattern_Database/*.cpp"
)

add_executable(RubiksCube
        main.cpp
        ${MODEL_SOURCES}
)

target_include_directories(RubiksCube PRIVATE
        "${CMAKE_SOURCE_DIR}/Model"
        "${CMAKE_SOURCE_DIR}/Solvers"
        "${CMAKE_SOURCE_DIR}/Pattern_Database"
)
