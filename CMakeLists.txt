cmake_minimum_required(VERSION 3.16)
project(SpectacularBid CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(sbcore src/layout.cpp src/game.cpp src/view.cpp src/search.cpp src/session.cpp src/nnue.cpp src/nnue_weights.cpp src/mlp.cpp src/models.cpp src/met.cpp src/met_table.cpp)
target_include_directories(sbcore PUBLIC src)

add_executable(test_rules tests/test_rules.cpp)
target_link_libraries(test_rules sbcore)

add_executable(selfplay tools/selfplay.cpp)
target_link_libraries(selfplay sbcore)

enable_testing()
add_test(NAME rules COMMAND test_rules)

add_executable(sbcli tools/sbcli.cpp)
target_link_libraries(sbcli sbcore)

add_executable(bidstudy tools/bidstudy.cpp)
target_link_libraries(bidstudy sbcore)
add_executable(gendata tools/gendata.cpp)
target_link_libraries(gendata sbcore)
add_executable(train_nnue tools/train_nnue.cpp)
target_link_libraries(train_nnue sbcore)
find_package(Threads)
target_link_libraries(gendata Threads::Threads)

# Self-play setting sweeps scored by games won (games run in parallel).
add_executable(sbsweep tools/sbsweep.cpp)
target_link_libraries(sbsweep sbcore Threads::Threads)

# Unattended training (games run in parallel; each search single-threaded).
add_executable(sbtrain tools/sbtrain.cpp src/mlp.cpp)
target_link_libraries(sbtrain sbcore Threads::Threads)
target_compile_definitions(sbtrain PRIVATE SB_TRAIN_THREADS)

# Match equity table: self-play hands -> P(win the game) from any score.
add_executable(sbequity tools/sbequity.cpp)
target_link_libraries(sbequity sbcore Threads::Threads)
