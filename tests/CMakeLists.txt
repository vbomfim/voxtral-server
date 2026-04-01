add_executable(tts_tests
    test_config.cpp
    test_logger.cpp
    # Source files under test (compiled into test binary)
    ${CMAKE_SOURCE_DIR}/src/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/logger.cpp
)

target_include_directories(tts_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Mark third-party headers as SYSTEM to suppress warnings from -Werror
get_target_property(_spdlog_inc spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
if(_spdlog_inc)
    target_include_directories(tts_tests SYSTEM PRIVATE ${_spdlog_inc})
endif()
get_target_property(_json_inc nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
if(_json_inc)
    target_include_directories(tts_tests SYSTEM PRIVATE ${_json_inc})
endif()
get_target_property(_toml_inc tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
if(_toml_inc)
    target_include_directories(tts_tests SYSTEM PRIVATE ${_toml_inc})
endif()

# Apply hardened compiler flags to test code too
target_compile_options(tts_tests PRIVATE ${TTS_COMPILE_OPTIONS})

target_link_libraries(tts_tests PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    tomlplusplus::tomlplusplus
)

include(GoogleTest)
gtest_discover_tests(tts_tests)
