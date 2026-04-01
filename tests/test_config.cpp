#include <gtest/gtest.h>
#include "config/config.hpp"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

using tts::config::ServerConfig;
using tts::config::safe_atoi;

// ============================================================================
// Helper: create a valid config (all required fields set)
// ============================================================================
static ServerConfig make_valid_config() {
    ServerConfig cfg;
    cfg.model_path = "/tmp/model.bin";
    return cfg;
}

// ============================================================================
// safe_atoi tests
// ============================================================================

TEST(SafeAtoi, ValidPositiveInteger) {
    EXPECT_EQ(safe_atoi("42"), 42);
}

TEST(SafeAtoi, ValidNegativeInteger) {
    EXPECT_EQ(safe_atoi("-7"), -7);
}

TEST(SafeAtoi, ValidZero) {
    EXPECT_EQ(safe_atoi("0"), 0);
}

TEST(SafeAtoi, NonNumericReturnsDefault) {
    EXPECT_EQ(safe_atoi("abc", 99), 99);
}

TEST(SafeAtoi, EmptyStringReturnsDefault) {
    EXPECT_EQ(safe_atoi("", 55), 55);
}

TEST(SafeAtoi, NullReturnsDefault) {
    EXPECT_EQ(safe_atoi(nullptr, 77), 77);
}

TEST(SafeAtoi, TrailingGarbageReturnsDefault) {
    EXPECT_EQ(safe_atoi("42abc", 10), 10);
}

TEST(SafeAtoi, WhitespaceOnlyReturnsDefault) {
    EXPECT_EQ(safe_atoi("   ", 3), 3);
}

TEST(SafeAtoi, OverflowReturnsDefault) {
    EXPECT_EQ(safe_atoi("99999999999999999999", 5), 5);
}

// ============================================================================
// Default values tests
// ============================================================================

TEST(ServerConfig, DefaultValues) {
    ServerConfig cfg;
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.port, 9090);
    EXPECT_EQ(cfg.max_connections, 100);
    EXPECT_EQ(cfg.default_voice, "neutral_female");
    EXPECT_EQ(cfg.max_queue_depth, 10);
    EXPECT_EQ(cfg.workers, 1);
    EXPECT_EQ(cfg.max_input_chars, 4096);
    EXPECT_EQ(cfg.request_timeout_seconds, 300);
    EXPECT_TRUE(cfg.api_key.empty());
    EXPECT_FALSE(cfg.require_auth);
    EXPECT_EQ(cfg.auth_rate_limit_max, 10);
    EXPECT_EQ(cfg.auth_rate_limit_window, 60);
    EXPECT_EQ(cfg.request_rate_limit_rpm, 10);
    EXPECT_FALSE(cfg.trust_proxy);
    EXPECT_EQ(cfg.trusted_proxy_hops, 1);
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_EQ(cfg.log_format, "text");
}

// ============================================================================
// Validation tests — valid configs
// ============================================================================

TEST(ServerConfigValidation, ValidConfig_NoThrow) {
    auto cfg = make_valid_config();
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ServerConfigValidation, ValidEdgeCases_Pass) {
    ServerConfig cfg;
    cfg.model_path = "/m";
    cfg.port = 1;
    cfg.max_connections = 1;
    cfg.max_input_chars = 1;
    cfg.workers = 1;
    cfg.max_queue_depth = 1;
    cfg.request_timeout_seconds = 1;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ServerConfigValidation, MaxPort_Pass) {
    auto cfg = make_valid_config();
    cfg.port = 65535;
    EXPECT_NO_THROW(cfg.validate());
}

// ============================================================================
// Validation tests — invalid configs
// ============================================================================

TEST(ServerConfigValidation, PortZero_Throws) {
    auto cfg = make_valid_config();
    cfg.port = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, PortTooHigh_Throws) {
    auto cfg = make_valid_config();
    cfg.port = 70000;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, NegativePort_Throws) {
    auto cfg = make_valid_config();
    cfg.port = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, ZeroMaxConnections_Throws) {
    auto cfg = make_valid_config();
    cfg.max_connections = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, ZeroMaxInputChars_Throws) {
    auto cfg = make_valid_config();
    cfg.max_input_chars = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, ZeroWorkers_Throws) {
    auto cfg = make_valid_config();
    cfg.workers = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, ZeroMaxQueueDepth_Throws) {
    auto cfg = make_valid_config();
    cfg.max_queue_depth = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, ZeroRequestTimeout_Throws) {
    auto cfg = make_valid_config();
    cfg.request_timeout_seconds = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, RequireAuthWithoutKey_Throws) {
    auto cfg = make_valid_config();
    cfg.require_auth = true;
    cfg.api_key = "";
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, RequireAuthWithKey_NoThrow) {
    auto cfg = make_valid_config();
    cfg.require_auth = true;
    cfg.api_key = "secret-key-value";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ServerConfigValidation, InvalidLogLevel_Throws) {
    auto cfg = make_valid_config();
    cfg.log_level = "verbose";
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ServerConfigValidation, InvalidLogFormat_Throws) {
    auto cfg = make_valid_config();
    cfg.log_format = "xml";
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// ============================================================================
// Environment variable override tests
// ============================================================================

// Helper: RAII class to set and restore environment variables
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

TEST(ServerConfigEnv, PortOverride) {
    ScopedEnv env("TTS_PORT", "8080");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.port, 8080);
}

TEST(ServerConfigEnv, HostOverride) {
    ScopedEnv env("TTS_HOST", "127.0.0.1");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.host, "127.0.0.1");
}

TEST(ServerConfigEnv, ModelPathOverride) {
    ScopedEnv env("TTS_MODEL_PATH", "/opt/models/v4b");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.model_path, "/opt/models/v4b");
}

TEST(ServerConfigEnv, LogFormatOverride) {
    ScopedEnv env("TTS_LOG_FORMAT", "json");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.log_format, "json");
}

TEST(ServerConfigEnv, RequireAuthTrue) {
    ScopedEnv env("TTS_REQUIRE_AUTH", "true");
    auto cfg = ServerConfig::from_env();
    EXPECT_TRUE(cfg.require_auth);
}

TEST(ServerConfigEnv, RequireAuthOne) {
    ScopedEnv env("TTS_REQUIRE_AUTH", "1");
    auto cfg = ServerConfig::from_env();
    EXPECT_TRUE(cfg.require_auth);
}

TEST(ServerConfigEnv, RequireAuthFalse) {
    ScopedEnv env("TTS_REQUIRE_AUTH", "false");
    auto cfg = ServerConfig::from_env();
    EXPECT_FALSE(cfg.require_auth);
}

TEST(ServerConfigEnv, TrustProxyOverride) {
    ScopedEnv env("TTS_TRUST_PROXY", "true");
    auto cfg = ServerConfig::from_env();
    EXPECT_TRUE(cfg.trust_proxy);
}

TEST(ServerConfigEnv, WorkersOverride) {
    ScopedEnv env("TTS_WORKERS", "4");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.workers, 4);
}

TEST(ServerConfigEnv, InvalidPortFallsBackToDefault) {
    ScopedEnv env("TTS_PORT", "not_a_number");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.port, 9090);  // default
}

TEST(ServerConfigEnv, ApiKeyOverride) {
    ScopedEnv env("TTS_API_KEY", "my-secret-key");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.api_key, "my-secret-key");
}

TEST(ServerConfigEnv, MaxInputCharsOverride) {
    ScopedEnv env("TTS_MAX_INPUT_CHARS", "8192");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.max_input_chars, 8192);
}

TEST(ServerConfigEnv, MaxConnectionsOverride) {
    ScopedEnv env("TTS_MAX_CONNECTIONS", "50");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.max_connections, 50);
}

TEST(ServerConfigEnv, TrustedProxyHopsOverride) {
    ScopedEnv env("TTS_TRUSTED_PROXY_HOPS", "3");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.trusted_proxy_hops, 3);
}

TEST(ServerConfigEnv, RateLimitOverrides) {
    ScopedEnv e1("TTS_AUTH_RATE_LIMIT_MAX", "20");
    ScopedEnv e2("TTS_AUTH_RATE_LIMIT_WINDOW", "120");
    ScopedEnv e3("TTS_RATE_LIMIT_RPM", "30");
    auto cfg = ServerConfig::from_env();
    EXPECT_EQ(cfg.auth_rate_limit_max, 20);
    EXPECT_EQ(cfg.auth_rate_limit_window, 120);
    EXPECT_EQ(cfg.request_rate_limit_rpm, 30);
}

// ============================================================================
// TOML file loading tests
// ============================================================================

// Helper: write a temp TOML file and return its path
static std::string write_temp_toml(const std::string& content) {
    // Use a fixed path in /tmp for test TOML files
    static int counter = 0;
    std::string path = "/tmp/tts_test_config_" + std::to_string(counter++) + ".toml";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

TEST(ServerConfigToml, LoadsPortFromFile) {
    auto path = write_temp_toml(R"(
[server]
port = 7777
host = "10.0.0.1"
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.port, 7777);
    EXPECT_EQ(cfg.host, "10.0.0.1");
}

TEST(ServerConfigToml, LoadsModelSettings) {
    auto path = write_temp_toml(R"(
[model]
path = "/data/models/v4b"
default_voice = "deep_male"
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.model_path, "/data/models/v4b");
    EXPECT_EQ(cfg.default_voice, "deep_male");
}

TEST(ServerConfigToml, LoadsInferenceSettings) {
    auto path = write_temp_toml(R"(
[inference]
max_queue_depth = 20
workers = 4
max_input_chars = 2048
request_timeout_seconds = 120
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.max_queue_depth, 20);
    EXPECT_EQ(cfg.workers, 4);
    EXPECT_EQ(cfg.max_input_chars, 2048);
    EXPECT_EQ(cfg.request_timeout_seconds, 120);
}

TEST(ServerConfigToml, LoadsAuthSettings) {
    auto path = write_temp_toml(R"(
[auth]
require_auth = true
rate_limit_max = 5
rate_limit_window = 30
request_rate_limit_rpm = 20
trust_proxy = true
trusted_proxy_hops = 2
)");
    // Must also set API key via env for require_auth=true to validate
    ScopedEnv env("TTS_API_KEY", "test-key");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_TRUE(cfg.require_auth);
    EXPECT_EQ(cfg.auth_rate_limit_max, 5);
    EXPECT_EQ(cfg.auth_rate_limit_window, 30);
    EXPECT_EQ(cfg.request_rate_limit_rpm, 20);
    EXPECT_TRUE(cfg.trust_proxy);
    EXPECT_EQ(cfg.trusted_proxy_hops, 2);
}

TEST(ServerConfigToml, LoadsLoggingSettings) {
    auto path = write_temp_toml(R"(
[logging]
level = "debug"
format = "json"
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.log_level, "debug");
    EXPECT_EQ(cfg.log_format, "json");
}

TEST(ServerConfigToml, EnvOverridesToml) {
    auto path = write_temp_toml(R"(
[server]
port = 7777
)");
    ScopedEnv env("TTS_PORT", "3000");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.port, 3000);  // env wins
}

TEST(ServerConfigToml, MissingFileFallsBackToDefaults) {
    auto cfg = ServerConfig::from_file_and_env("/nonexistent/path/config.toml");
    EXPECT_EQ(cfg.port, 9090);  // default
    EXPECT_EQ(cfg.host, "0.0.0.0");  // default
}

TEST(ServerConfigToml, LoadsMaxConnections) {
    auto path = write_temp_toml(R"(
[server]
max_connections = 200
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.max_connections, 200);
}

// ============================================================================
// safe_narrow tests (int64_t → int with overflow protection)
// ============================================================================

TEST(SafeNarrow, ValidInRange) {
    using tts::config::safe_narrow;
    EXPECT_EQ(safe_narrow(42, "test", 0), 42);
    EXPECT_EQ(safe_narrow(-100, "test", 0), -100);
    EXPECT_EQ(safe_narrow(0, "test", 99), 0);
}

TEST(SafeNarrow, ExactIntMax) {
    using tts::config::safe_narrow;
    EXPECT_EQ(safe_narrow(INT_MAX, "test", 0), INT_MAX);
}

TEST(SafeNarrow, ExactIntMin) {
    using tts::config::safe_narrow;
    EXPECT_EQ(safe_narrow(INT_MIN, "test", 0), INT_MIN);
}

TEST(SafeNarrow, PositiveOverflow) {
    using tts::config::safe_narrow;
    int64_t big = static_cast<int64_t>(INT_MAX) + 1;
    EXPECT_EQ(safe_narrow(big, "test_field", -1), -1);
}

TEST(SafeNarrow, NegativeOverflow) {
    using tts::config::safe_narrow;
    int64_t small = static_cast<int64_t>(INT_MIN) - 1;
    EXPECT_EQ(safe_narrow(small, "test_field", -1), -1);
}

// ============================================================================
// model_path validation
// ============================================================================

TEST(ServerConfigValidation, EmptyModelPath_Throws) {
    ServerConfig cfg;
    // model_path is empty by default — validate() must reject
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

// ============================================================================
// Oversized TOML integer (exercises safe_narrow via TOML loading path)
// ============================================================================

TEST(ServerConfigToml, OversizedTomlIntegerFallsBackToDefault) {
    auto path = write_temp_toml(R"(
[server]
port = 99999999999
)");
    auto cfg = ServerConfig::from_file_and_env(path);
    EXPECT_EQ(cfg.port, 9090);  // default kept — oversized int64 was rejected
}
