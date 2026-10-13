# The original solver engine: cube models, BFS/DFS/IDDFS/IDA* solvers and Korf's
# corner pattern database.

add_library(rubiks_engine STATIC
        Model/RubiksCube.cpp
        Pattern_Database/CornerDBMaker.cpp
        Pattern_Database/CornerPatternDatabase.cpp
        Pattern_Database/NibbleArray.cpp
        Pattern_Database/PatternDatabase.cpp
        Pattern_Database/cube_math.cpp
)
target_include_directories(rubiks_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(rubiks_engine PUBLIC cxx_std_17)

add_executable(rubiks_cli main.cpp)
target_link_libraries(rubiks_cli PRIVATE rubiks_engine)
