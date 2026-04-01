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

static ProcessResult run_binary(const std::string& args, const std::string& env_prefix = "") {
    // Find the binary — it's in the build directory
    // We use a relative path from the test working directory
    std::string cmd = env_prefix;
    if (!cmd.empty()) cmd += " ";
    cmd += "${TTS_BINARY:-./tts_server} " + args + " 2>&1";

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
    auto result = run_binary("--config /dev/null", "TTS_MODEL_PATH=/tmp/test-model");
    // /dev/null is empty → not a valid TOML but not found as file?
    // Actually /dev/null exists but is empty → valid TOML (no keys)
    // Config loads defaults → validates → exits 0
    EXPECT_EQ(result.exit_code, 0);
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
                              "TTS_MODEL_PATH=/tmp/test-model TTS_REQUIRE_AUTH=true TTS_API_KEY=super-secret-key-12345");
    EXPECT_EQ(result.exit_code, 0);
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
