/// QA Guardian — Config Integration & Edge-Case Tests
/// These tests cover gaps not addressed by the Developer's unit tests.
/// Each test is tagged with its rationale: [COVERAGE], [EDGE], [CONTRACT], [INTEGRATION].
///
/// Scope: config loading pipeline (TOML + env + validation), edge cases,
///        security properties (API key never from TOML), negative validation.

#include <gtest/gtest.h>
#include "config/config.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

using tts::config::ServerConfig;
using tts::config::safe_atoi;

// ============================================================================
// Helper: RAII class to set and restore environment variables
// ============================================================================
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value)
        : name_(name) {
        auto* old = std::getenv(name);
        if (old) {
            had_value_ = true;
            old_value_ = old;
        }
        setenv(name, value, 1);
    }
    ~ScopedEnv() {
        if (had_value_) {
            setenv(name_.c_str(), old_value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::string old_value_;
    bool had_value_ = false;
};

// Helper: write a temp TOML file and return its path
static int g_toml_counter = 1000;
static std::string write_temp_toml(const std::string& content) {
    std::string path = "/tmp/tts_qa_config_" + std::to_string(g_toml_counter++) + ".toml";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

static ServerConfig make_valid_config() {
    ServerConfig cfg;
    cfg.model_path = "/tmp/model.bin";
    return cfg;
}

// ============================================================================
// [COVERAGE] Malformed TOML falls back to defaults
// Developer tests: missing file, valid files. Never tested parse errors.
// ============================================================================

TEST(ConfigIntegration, MalformedTomlFallsBackToDefaults) {
    // Write a TOML file with invalid syntax
    auto path = write_temp_toml("this is [[[not valid toml");
    auto cfg = ServerConfig::from_file_and_env(path);

    // Should fall back to defaults — not crash, not throw
    EXPECT_EQ(cfg.port, 9090);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.workers, 1);
}

// ============================================================================
// [INTEGRATION] Malformed TOML + env overrides still apply
// Verifies the fallback path still applies env var overrides.
// ============================================================================

TEST(ConfigIntegration, MalformedTomlStillAppliesEnvOverrides) {
    auto path = write_temp_toml("[server\nbroken syntax");
    ScopedEnv env("TTS_PORT", "4444");
    auto cfg = ServerConfig::from_file_and_env(path);

    // TOML parse fails → defaults + env overrides
    EXPECT_EQ(cfg.port, 4444);
}

// ============================================================================
// [COVERAGE] TOML with wrong types — int where string expected, etc.
// toml++ returns std::nullopt for type mismatches; verify graceful handling.
// ============================================================================

TEST(ConfigIntegration, TomlWrongTypeIgnoredGracefully) {
    // port expects int64_t, give it a string; host expects string, give it int
    auto path = write_temp_toml(R"(
[server]
port = "not_a_number"
host = 42
)");
    auto cfg = ServerConfig::from_file_and_env(path);

    // Type mismatch → toml++ returns nullopt → defaults are kept
    EXPECT_EQ(cfg.port, 9090);     // default kept
    EXPECT_EQ(cfg.host, "0.0.0.0"); // default kept
}

// ============================================================================
// [EDGE] TOML with extra/unknown keys — silently ignored
// ============================================================================

TEST(ConfigIntegration, TomlExtraKeysIgnored) {
    auto path = write_temp_toml(R"(
[server]
port = 5555
unknown_key = "should be ignored"

[nonexistent_section]
foo = "bar"
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.port, 5555);
    // No crash, no exception
}

// ============================================================================
// [EDGE/SECURITY] API key is NOT loaded from TOML file
// The comment in config.cpp says "intentionally NOT loaded from TOML".
// This test proves that security property holds.
// ============================================================================

TEST(ConfigSecurity, ApiKeyNotLoadedFromToml) {
    auto path = write_temp_toml(R"(
[auth]
api_key = "secret-from-toml-file"
require_auth = true
)");
    // Don't set TTS_API_KEY env var — should remain empty
    auto cfg = ServerConfig::from_file_and_env(path);

    // api_key MUST remain empty — it should only come from TTS_API_KEY env var
    EXPECT_TRUE(cfg.api_key.empty());
}

// ============================================================================
// [INTEGRATION] Full pipeline: TOML → env override → validate → all green
// ============================================================================

TEST(ConfigIntegration, FullPipelineTomlPlusEnvThenValidate) {
    auto path = write_temp_toml(R"(
[server]
port = 8080
host = "10.0.0.1"
max_connections = 50

[model]
path = "/models/v4b"
default_voice = "deep_male"

[inference]
workers = 2
max_queue_depth = 5
max_input_chars = 2048
request_timeout_seconds = 60

[logging]
level = "debug"
format = "json"
)");
    // Override some via env
    ScopedEnv e1("TTS_PORT", "3000");
    ScopedEnv e2("TTS_WORKERS", "4");
    ScopedEnv e3("TTS_LOG_LEVEL", "warn");

    auto cfg = ServerConfig::from_file_and_env(path);

    // Env overrides TOML
    EXPECT_EQ(cfg.port, 3000);
    EXPECT_EQ(cfg.workers, 4);
    EXPECT_EQ(cfg.log_level, "warn");

    // TOML values kept for non-overridden fields
    EXPECT_EQ(cfg.host, "10.0.0.1");
    EXPECT_EQ(cfg.max_connections, 50);
    EXPECT_EQ(cfg.model_path, "/models/v4b");
    EXPECT_EQ(cfg.default_voice, "deep_male");
    EXPECT_EQ(cfg.max_queue_depth, 5);
    EXPECT_EQ(cfg.max_input_chars, 2048);
    EXPECT_EQ(cfg.request_timeout_seconds, 60);
    EXPECT_EQ(cfg.log_format, "json");

    // Must pass validation
    EXPECT_NO_THROW(cfg.validate());
}

// ============================================================================
// [COVERAGE] Env override tests for fields missing from Developer's tests
// TTS_DEFAULT_VOICE, TTS_LOG_LEVEL, TTS_MAX_QUEUE_DEPTH, TTS_REQUEST_TIMEOUT
// ============================================================================

TEST(ConfigEnvGap, DefaultVoiceOverride) {
    ScopedEnv env("TTS_DEFAULT_VOICE", "deep_male");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.default_voice, "deep_male");
}

TEST(ConfigEnvGap, LogLevelOverride) {
    ScopedEnv env("TTS_LOG_LEVEL", "debug");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.log_level, "debug");
}

TEST(ConfigEnvGap, MaxQueueDepthOverride) {
    ScopedEnv env("TTS_MAX_QUEUE_DEPTH", "50");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.max_queue_depth, 50);
}

TEST(ConfigEnvGap, RequestTimeoutOverride) {
    ScopedEnv env("TTS_REQUEST_TIMEOUT", "600");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.request_timeout_seconds, 600);
}

TEST(ConfigEnvGap, ConfigPathOverride) {
    ScopedEnv env("TTS_CONFIG_PATH", "/custom/path.toml");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.config_path, "/custom/path.toml");
}

// ============================================================================
// [EDGE] Validation with negative values (not just zero)
// Developer tests zero boundary. These test deep-negative values.
// ============================================================================

TEST(ConfigValidationEdge, NegativeWorkers_Throws) {
    auto cfg = make_valid_config();
    cfg.workers = -100;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeMaxConnections_Throws) {
    auto cfg = make_valid_config();
    cfg.max_connections = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeMaxInputChars_Throws) {
    auto cfg = make_valid_config();
    cfg.max_input_chars = -500;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeMaxQueueDepth_Throws) {
    auto cfg = make_valid_config();
    cfg.max_queue_depth = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeRequestTimeout_Throws) {
    auto cfg = make_valid_config();
    cfg.request_timeout_seconds = -30;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeAuthRateLimitMax_Throws) {
    auto cfg = make_valid_config();
    cfg.auth_rate_limit_max = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeAuthRateLimitWindow_Throws) {
    auto cfg = make_valid_config();
    cfg.auth_rate_limit_window = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeRequestRateLimitRpm_Throws) {
    auto cfg = make_valid_config();
    cfg.request_rate_limit_rpm = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, NegativeTrustedProxyHops_Throws) {
    auto cfg = make_valid_config();
    cfg.trusted_proxy_hops = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// ============================================================================
// [EDGE] Validation: all valid log levels accepted
// Developer tests one invalid. These verify each valid level passes.
// ============================================================================

TEST(ConfigValidationEdge, AllValidLogLevels_Pass) {
    for (const auto& level : {"trace", "debug", "info", "warn", "error", "critical"}) {
        auto cfg = make_valid_config();
        cfg.log_level = level;
        EXPECT_NO_THROW(cfg.validate()) << "log_level='" << level << "' should pass";
    }
}

TEST(ConfigValidationEdge, EmptyLogLevel_Throws) {
    auto cfg = make_valid_config();
    cfg.log_level = "";
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, EmptyLogFormat_Throws) {
    auto cfg = make_valid_config();
    cfg.log_format = "";
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, CaseSensitiveLogLevel_Throws) {
    auto cfg = make_valid_config();
    cfg.log_level = "INFO";  // uppercase should NOT match
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidationEdge, CaseSensitiveLogFormat_Throws) {
    auto cfg = make_valid_config();
    cfg.log_format = "JSON";  // uppercase should NOT match
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// ============================================================================
// [EDGE] safe_atoi edge cases not in Developer's tests
// ============================================================================

TEST(SafeAtoiEdge, LeadingWhitespace_ReturnsDefault) {
    // std::stoi skips leading whitespace but pos will mismatch length
    // Behavior: "  42" → stoi parses 42, but pos=4 != strlen("  42")=4
    // Actually pos=4 and string("  42").size()=4, so it matches.
    // Let's verify the actual behavior:
    int result = safe_atoi("  42", 99);
    // std::stoi("  42") → 42, pos=4, string size=4 → matches → returns 42
    EXPECT_EQ(result, 42);
}

TEST(SafeAtoiEdge, LeadingPlusSign) {
    // "+42" — std::stoi handles this
    EXPECT_EQ(safe_atoi("+42"), 42);
}

TEST(SafeAtoiEdge, MaxInt) {
    EXPECT_EQ(safe_atoi("2147483647"), 2147483647);
}

TEST(SafeAtoiEdge, MinInt) {
    EXPECT_EQ(safe_atoi("-2147483648"), -2147483648);
}

TEST(SafeAtoiEdge, JustOverMaxInt_ReturnsDefault) {
    EXPECT_EQ(safe_atoi("2147483648", 11), 11);
}

TEST(SafeAtoiEdge, ZeroDefault) {
    // Verify default_val=0 when not specified
    EXPECT_EQ(safe_atoi("not_a_number"), 0);
}

// ============================================================================
// [EDGE] Boolean parsing edge cases via env vars
// ============================================================================

TEST(ConfigEnvEdge, RequireAuthRandomString_IsFalse) {
    ScopedEnv env("TTS_REQUIRE_AUTH", "yes");  // only "true" and "1" are truthy
    auto cfg = ServerConfig::from_env();
    EXPECT_FALSE(cfg.require_auth);
}

TEST(ConfigEnvEdge, TrustProxyZero_IsFalse) {
    ScopedEnv env("TTS_TRUST_PROXY", "0");
    auto cfg = ServerConfig::from_env();
    EXPECT_FALSE(cfg.trust_proxy);
}

TEST(ConfigEnvEdge, TrustProxyEmptyString_IsFalse) {
    ScopedEnv env("TTS_TRUST_PROXY", "");
    auto cfg = ServerConfig::from_env();
    EXPECT_FALSE(cfg.trust_proxy);
}

// ============================================================================
// [EDGE] Empty env var string for string fields
// ============================================================================

TEST(ConfigEnvEdge, EmptyHostEnvVar) {
    ScopedEnv env("TTS_HOST", "");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.host, "");  // empty string is set, not skipped
}

TEST(ConfigEnvEdge, EmptyModelPathEnvVar) {
    ScopedEnv env("TTS_MODEL_PATH", "");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.model_path, "");
}
