# --- Helper: configure a test target with hardened flags and SYSTEM includes ---
macro(tts_configure_test_target TARGET_NAME)
    tts_add_system_includes(${TARGET_NAME})
    target_compile_options(${TARGET_NAME} PRIVATE ${TTS_COMPILE_OPTIONS})
    target_link_libraries(${TARGET_NAME} PRIVATE
        tts_core
        GTest::gtest_main
    )
endmacro()

# --- Developer unit tests ---
add_executable(tts_tests
    test_config.cpp
    test_logger.cpp
    test_backend.cpp
    test_voices.cpp
    test_inference_pool.cpp
)
tts_configure_test_target(tts_tests)
include(GoogleTest)
gtest_discover_tests(tts_tests)

# --- QA integration & edge-case tests ---
add_executable(tts_qa_tests
    test_config_integration.cpp
    test_logger_integration.cpp
    test_inference_pool_integration.cpp
    test_wav_contract.cpp
    test_voices_integration.cpp
)
tts_configure_test_target(tts_qa_tests)
gtest_discover_tests(tts_qa_tests)

# --- QA binary E2E tests (process-level) ---
add_executable(tts_e2e_tests
    test_binary_e2e.cpp
)
target_compile_options(tts_e2e_tests PRIVATE ${TTS_COMPILE_OPTIONS})
target_link_libraries(tts_e2e_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(tts_e2e_tests
    PROPERTIES ENVIRONMENT "TTS_BINARY=$<TARGET_FILE:tts_server>"
)
