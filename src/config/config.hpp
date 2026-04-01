#pragma once

#include <string>

namespace tts::config {

/// Safe integer parsing with fallback — replaces raw atoi/stoi.
/// Returns default_val on any parse failure (empty string, non-numeric, overflow).
int safe_atoi(const char* str, int default_val = 0);

/// Server configuration loaded from TOML file and/or environment variables.
/// Environment variables (TTS_* prefix) take precedence over file values (12-factor).
struct ServerConfig {
    // Server
    std::string host = "0.0.0.0";
    int port = 9090;
    int max_connections = 100;

    // Model
    std::string model_path;  // TTS_MODEL_PATH (required for runtime, not for config loading)
    std::string default_voice = "neutral_female";

    // Inference
    int max_queue_depth = 10;
    int workers = 1;
    int max_input_chars = 4096;
    int request_timeout_seconds = 300;

    // Auth
    std::string api_key;  // TTS_API_KEY — NEVER log this value
    bool require_auth = false;
    int auth_rate_limit_max = 10;
    int auth_rate_limit_window = 60;
    int request_rate_limit_rpm = 10;
    bool trust_proxy = false;
    int trusted_proxy_hops = 1;

    // Logging
    std::string log_level = "info";
    std::string log_format = "text";  // "text" or "json"

    // Config path
    std::string config_path = "config/server.toml";

    /// Load configuration from TOML file, then override with environment variables.
    static ServerConfig from_file_and_env(const std::string& config_path);

    /// Load configuration from environment variables only (no file).
    static ServerConfig from_env();

    /// Validate configuration. Throws std::invalid_argument on invalid config.
    void validate() const;
};

} // namespace tts::config
