cmake_minimum_required(VERSION 3.20)
project(voxtral-server
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Security-hardened compiler flags (applied to our targets only) ---
# These are NOT set globally to avoid breaking third-party code.
set(TTS_COMPILE_OPTIONS
    -Wall -Wextra -Wpedantic -Werror
    -Wformat=2 -Wformat-security
    -Wconversion -Wsign-conversion
    -Wnull-dereference
    -Wstack-protector
    -fstack-protector-strong
    -fPIE
    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
)

set(TTS_LINK_OPTIONS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TTS_LINK_OPTIONS -pie -Wl,-z,relro -Wl,-z,now)
endif()

# --- Default build type ---
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Version header (single source of truth from project() VERSION) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/tts/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/tts/version.hpp
    @ONLY
)

# --- FetchContent dependencies (all pinned to exact commit SHAs) ---
include(FetchContent)

# nlohmann/json (header-only)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03  # v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        f355b3d58f7067eee1706ff3c801c2361011f3d5  # v1.15.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# toml++ (header-only)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        30172438cee64926dc41fdd9c11fb3ba5b2ba9de  # v3.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# prometheus-cpp
FetchContent_Declare(
    prometheus_cpp
    GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
    GIT_TAG        e5fada43131d251e9c4786b04263ce98b6767ba5  # v1.3.0
    GIT_SHALLOW    TRUE
)
set(ENABLE_PUSH OFF CACHE BOOL "" FORCE)
set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GENERATE_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(prometheus_cpp)

# cpp-httplib (header-only, MIT) — lightweight HTTP server for API listener
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        787a34ad7f01f20922a237d5142aae469828be72  # v0.20.0
    GIT_SHALLOW    TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# --- Macro: mark third-party headers as SYSTEM to suppress -Werror ---
macro(tts_add_system_includes TARGET_NAME)
    get_target_property(_spdlog_inc spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
    if(_spdlog_inc)
        target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${_spdlog_inc})
    endif()
    get_target_property(_json_inc nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(_json_inc)
        target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${_json_inc})
    endif()
    get_target_property(_toml_inc tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    if(_toml_inc)
        target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${_toml_inc})
    endif()
    get_target_property(_httplib_inc httplib::httplib INTERFACE_INCLUDE_DIRECTORIES)
    if(_httplib_inc)
        target_include_directories(${TARGET_NAME} SYSTEM PRIVATE ${_httplib_inc})
    endif()
endmacro()

# --- Core library (compiled once, shared by server and tests) ---
add_library(tts_core OBJECT
    src/config/config.cpp
    src/logging/logger.cpp
    src/tts/voxtral_backend.cpp
    src/tts/voices.cpp
    src/tts/inference_pool.cpp
    src/server/metrics.cpp
    src/server/auth.cpp
    src/server/validation.cpp
    src/server/request_handler.cpp
    src/server/server.cpp
    src/server/http_adapter.cpp
)
target_include_directories(tts_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
tts_add_system_includes(tts_core)
# Suppress warnings from prometheus-cpp headers
get_target_property(_prom_core_inc prometheus-cpp::core INTERFACE_INCLUDE_DIRECTORIES)
if(_prom_core_inc)
    target_include_directories(tts_core SYSTEM PUBLIC ${_prom_core_inc})
endif()
target_compile_options(tts_core PRIVATE ${TTS_COMPILE_OPTIONS})
target_link_libraries(tts_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    tomlplusplus::tomlplusplus
    prometheus-cpp::core
    httplib::httplib
)

# --- Main server target ---
add_executable(tts_server src/main.cpp)
tts_add_system_includes(tts_server)
target_compile_options(tts_server PRIVATE ${TTS_COMPILE_OPTIONS})
if(TTS_LINK_OPTIONS)
    target_link_options(tts_server PRIVATE ${TTS_LINK_OPTIONS})
endif()
target_link_libraries(tts_server PRIVATE
    tts_core
)

# --- Tests ---
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        b514bdc898e2951020cbdca1304b75f5950d1f59  # v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Benchmarks ---
option(BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        12235e24652fc7f809373e7c11a5f73c5763fc4c  # v1.9.1
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()
