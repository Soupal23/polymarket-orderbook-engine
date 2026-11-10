cmake_minimum_required(VERSION 3.15)
project(PolymarketOrderbookEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimization and warnings
if (MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# Locate OpenSSL, ZLIB, and nlohmann_json
set(OPENSSL_ROOT_DIR "C:/msys64/mingw64" CACHE PATH "OpenSSL root path" FORCE)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED)

# Fetch and configure IXWebSocket
include(FetchContent)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_ZLIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)
FetchContent_MakeAvailable(ixwebsocket)

# Core Static Library
add_library(polymarket_core STATIC
    core/src/orderbook.cpp
    core/src/recorder.cpp
)

target_include_directories(polymarket_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

target_link_libraries(polymarket_core PUBLIC
    ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    nlohmann_json::nlohmann_json
)

if (WIN32)
    target_link_libraries(polymarket_core PUBLIC ws2_32 crypt32 shlwapi)
endif()

# Orderbook Test Executable
add_executable(test_orderbook
    core/tests/test_orderbook.cpp
)
target_link_libraries(test_orderbook PRIVATE polymarket_core)

# Orderbook Benchmark Executable
add_executable(bench_orderbook
    core/tests/bench_orderbook.cpp
)
target_link_libraries(bench_orderbook PRIVATE polymarket_core)

# Feed Recorder Executable
add_executable(polymarket_recorder
    core/src/recorder_main.cpp
)
target_link_libraries(polymarket_recorder PRIVATE polymarket_core)

# Message Counter Tool Executable
add_executable(count_messages
    core/src/count_messages.cpp
)

# Invariant Replayer Executable
add_executable(replay_invariants
    core/src/replay_invariants.cpp
)
target_link_libraries(replay_invariants PRIVATE polymarket_core)

# Recorder Test Executable
add_executable(test_recorder
    core/tests/test_recorder.cpp
)
target_link_libraries(test_recorder PRIVATE polymarket_core)

# Enable CTest
enable_testing()
add_test(NAME OrderBookTests COMMAND test_orderbook)
add_test(NAME RecorderTests COMMAND test_recorder)
add_test(NAME ReplaySampleInvariants COMMAND replay_invariants ${CMAKE_CURRENT_SOURCE_DIR}/data/sample.jsonl)
