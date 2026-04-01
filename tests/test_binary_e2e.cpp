/// QA Guardian — Binary E2E Tests
/// Process-level tests that run the tts_server binary and verify exit behavior.
/// Tags: [INTEGRATION], [EDGE], [CONTRACT]
///
/// These tests exercise the main() function through the actual binary,
/// verifying CLI flags, config loading, and error reporting at the process level.

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

// ============================================================================
// Helper: run a command and capture stdout+stderr, return exit code
// ============================================================================

struct ProcessResult {
    std::string output;
    int exit_code;
};

static ProcessResult run_binary(const std::string& args,
                                const std::string& env_prefix = "",
                                int timeout_ms = 0) {
    // Find the binary — it's in the build directory
    // We use a relative path from the test working directory
    std::string cmd = env_prefix;
    if (!cmd.empty()) cmd += " ";

    if (timeout_ms > 0) {
        // Launch in background, wait briefly, then kill
        cmd += "${TTS_BINARY:-./tts_server} " + args + " &"
               " PID=$!; sleep " + std::to_string(timeout_ms / 1000) + ";"
               " kill $PID 2>/dev/null; wait $PID 2>/dev/null;"
               " echo EXIT_CODE=$?";
    } else {
        cmd += "${TTS_BINARY:-./tts_server} " + args;
    }
    cmd += " 2>&1";

    ProcessResult result;
    std::array<char, 4096> buffer{};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);

    // For timeout mode, parse the EXIT_CODE from output
    if (timeout_ms > 0) {
        auto pos = result.output.find("EXIT_CODE=");
        if (pos != std::string::npos) {
            result.exit_code = std::atoi(result.output.c_str() + pos + 10);
        }
    }

    return result;
}

// ============================================================================
// [CONTRACT] --version flag prints version and exits 0
// ============================================================================

TEST(BinaryE2E, VersionFlag) {
    auto result = run_binary("--version");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("voxtral-server v") != std::string::npos);
    EXPECT_TRUE(result.output.find("0.1.0") != std::string::npos);
}

// ============================================================================
// [CONTRACT] --help flag prints usage and exits 0
// ============================================================================

TEST(BinaryE2E, HelpFlag) {
    auto result = run_binary("--help");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Usage:") != std::string::npos);
    EXPECT_TRUE(result.output.find("--config") != std::string::npos);
    EXPECT_TRUE(result.output.find("--version") != std::string::npos);
    EXPECT_TRUE(result.output.find("TTS_") != std::string::npos);
}

// ============================================================================
// [INTEGRATION] --config with valid file exits 0 (Phase 1: loads and exits)
// ============================================================================

TEST(BinaryE2E, ValidConfigExitsCleanly) {
    // Now that main.cpp starts a real HTTP server, a valid config + existing
    // model_path causes listen() to block.  We send SIGTERM after a brief wait.
    // /tmp exists on all POSIX systems and satisfies the path check in the stub backend.
    auto result = run_binary("--config /dev/null",
                              "TTS_MODEL_PATH=/tmp TTS_PORT=19888",
                              /*timeout_ms=*/2000);
    // Server started and was killed → exit code may be 0 (signal handled) or 143 (SIGTERM)
    // As long as it didn't crash (segfault → 139) and didn't error out (1), it's fine.
    EXPECT_NE(result.exit_code, 1) << "Binary failed to start: " << result.output;
    EXPECT_NE(result.exit_code, 139) << "Binary crashed (segfault): " << result.output;
}

// ============================================================================
// [EDGE] --config without argument exits with error
// ============================================================================

TEST(BinaryE2E, ConfigFlagWithoutArgument) {
    auto result = run_binary("--config");
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("requires") != std::string::npos ||
                result.output.find("Error") != std::string::npos);
}

// ============================================================================
// [EDGE] Invalid config (require_auth=true, no API key) → exits 1
// ============================================================================

TEST(BinaryE2E, InvalidConfigExitsWithError) {
    auto result = run_binary("--config /dev/null",
                              "TTS_MODEL_PATH=/tmp/test-model TTS_REQUIRE_AUTH=true TTS_API_KEY=");
    EXPECT_EQ(result.exit_code, 1);
    // Should mention the config error
    EXPECT_TRUE(result.output.find("TTS_REQUIRE_AUTH") != std::string::npos ||
                result.output.find("Configuration error") != std::string::npos ||
                result.output.find("TTS_API_KEY") != std::string::npos);
}

// ============================================================================
// [INTEGRATION] Auth enabled with key → exits 0, shows REDACTED
// ============================================================================

TEST(BinaryE2E, AuthEnabledShowsRedacted) {
    auto result = run_binary("--config /dev/null",
                              "TTS_MODEL_PATH=/tmp TTS_REQUIRE_AUTH=true TTS_API_KEY=super-secret-key-12345 TTS_PORT=19889",
                              /*timeout_ms=*/2000);
    // Must show REDACTED, never the actual key
    EXPECT_TRUE(result.output.find("REDACTED") != std::string::npos);
    EXPECT_TRUE(result.output.find("super-secret-key-12345") == std::string::npos);
}

// ============================================================================
// [EDGE] Invalid port via env → exits 1 with validation error
// ============================================================================

TEST(BinaryE2E, InvalidPortExitsWithError) {
    auto result = run_binary("--config /dev/null",
                              "TTS_MODEL_PATH=/tmp/test-model TTS_PORT=0");
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_TRUE(result.output.find("port") != std::string::npos ||
                result.output.find("Invalid") != std::string::npos);
}
