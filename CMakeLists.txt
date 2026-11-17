cmake_minimum_required(VERSION 3.20)
project(code4hft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 高频默认优化: -O3 + 本机架构 + 无例外路径可按需关
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)
add_compile_options($<$<CONFIG:Release>:-O3>)
# macOS arm64 / Linux x86_64 通吃, 不强制 -march=native 以保 CI 可复现
# 本地压测时可 -DCMAKE_CXX_FLAGS="-march=native" 打开

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(hft
  src/marketdata/order_book.cpp
  src/marketdata/feed_handler.cpp
  src/trading/matcher.cpp
  src/core/latency.cpp
)
target_include_directories(hft PUBLIC include)
target_compile_features(hft PUBLIC cxx_std_20)

add_executable(replay tools/replay.cpp)
target_link_libraries(replay PRIVATE hft)

add_executable(bench_orderbook tools/bench_orderbook.cpp)
target_link_libraries(bench_orderbook PRIVATE hft)

add_executable(feed_pipe tools/feed_pipe.cpp)
target_link_libraries(feed_pipe PRIVATE hft)

enable_testing()
add_executable(test_orderbook tests/test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE hft)
add_test(NAME orderbook COMMAND test_orderbook)

add_executable(test_queue tests/test_queue.cpp)
target_link_libraries(test_queue PRIVATE hft)
add_test(NAME queue COMMAND test_queue)

add_executable(test_feed tests/test_feed.cpp)
target_link_libraries(test_feed PRIVATE hft)
add_test(NAME feed COMMAND test_feed)

add_executable(test_trading tests/test_trading.cpp)
target_link_libraries(test_trading PRIVATE hft)
add_test(NAME trading COMMAND test_trading)
