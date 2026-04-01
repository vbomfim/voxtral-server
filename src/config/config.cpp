#include "config/config.hpp"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace tts::config {

int safe_atoi(const char* str, int default_val) {
    if (str == nullptr || str[0] == '\0') {
        return default_val;
    }
    try {
        std::size_t pos = 0;
        int result = std::stoi(str, &pos);
        // Reject trailing garbage: "42abc" should not parse as 42
        if (pos != std::strlen(str)) {
            spdlog::warn("Invalid integer value '{}', using default {}", str, default_val);
            return default_val;
        }
        return result;
    } catch (const std::exception&) {
        spdlog::warn("Invalid integer value '{}', using default {}", str, default_val);
        return default_val;
    }
}

int safe_narrow(int64_t val, const char* name, int default_val) {
    if (val < static_cast<int64_t>(INT_MIN) || val > static_cast<int64_t>(INT_MAX)) {
        spdlog::warn("TOML value for '{}' ({}) overflows int range, using default {}",
                     name, val, default_val);
        return default_val;
    }
    return static_cast<int>(val);
}

/// Parse boolean from string: "true" or "1" → true, anything else → false.
static bool parse_bool(const char* str) {
    if (str == nullptr) return false;
    std::string s(str);
    return s == "true" || s == "1";
}

/// Apply environment variable overrides to config.
static void apply_env_overrides(ServerConfig& cfg) {
    // Server
    if (auto* v = std::getenv("TTS_HOST"))              cfg.host = v;
    if (auto* v = std::getenv("TTS_PORT"))              cfg.port = safe_atoi(v, cfg.port);
    if (auto* v = std::getenv("TTS_MAX_CONNECTIONS"))   cfg.max_connections = safe_atoi(v, cfg.max_connections);

    // Model
    if (auto* v = std::getenv("TTS_MODEL_PATH"))        cfg.model_path = v;
    if (auto* v = std::getenv("TTS_DEFAULT_VOICE"))     cfg.default_voice = v;

    // Inference
    if (auto* v = std::getenv("TTS_MAX_QUEUE_DEPTH"))   cfg.max_queue_depth = safe_atoi(v, cfg.max_queue_depth);
    if (auto* v = std::getenv("TTS_WORKERS"))           cfg.workers = safe_atoi(v, cfg.workers);
    if (auto* v = std::getenv("TTS_MAX_INPUT_CHARS"))   cfg.max_input_chars = safe_atoi(v, cfg.max_input_chars);
    if (auto* v = std::getenv("TTS_REQUEST_TIMEOUT"))   cfg.request_timeout_seconds = safe_atoi(v, cfg.request_timeout_seconds);

    // Auth
    if (auto* v = std::getenv("TTS_API_KEY"))           cfg.api_key = v;
    if (auto* v = std::getenv("TTS_REQUIRE_AUTH"))      cfg.require_auth = parse_bool(v);
    if (auto* v = std::getenv("TTS_AUTH_RATE_LIMIT_MAX"))    cfg.auth_rate_limit_max = safe_atoi(v, cfg.auth_rate_limit_max);
    if (auto* v = std::getenv("TTS_AUTH_RATE_LIMIT_WINDOW")) cfg.auth_rate_limit_window = safe_atoi(v, cfg.auth_rate_limit_window);
    if (auto* v = std::getenv("TTS_RATE_LIMIT_RPM"))    cfg.request_rate_limit_rpm = safe_atoi(v, cfg.request_rate_limit_rpm);
    if (auto* v = std::getenv("TTS_TRUST_PROXY"))       cfg.trust_proxy = parse_bool(v);
    if (auto* v = std::getenv("TTS_TRUSTED_PROXY_HOPS"))cfg.trusted_proxy_hops = safe_atoi(v, cfg.trusted_proxy_hops);

    // Logging
    if (auto* v = std::getenv("TTS_LOG_LEVEL"))         cfg.log_level = v;
    if (auto* v = std::getenv("TTS_LOG_FORMAT"))        cfg.log_format = v;

    // Config path (meta — usually set before loading)
    if (auto* v = std::getenv("TTS_CONFIG_PATH"))       cfg.config_path = v;
}

ServerConfig ServerConfig::from_env() {
    ServerConfig cfg;
    apply_env_overrides(cfg);
    return cfg;
}

ServerConfig ServerConfig::from_file_and_env(const std::string& config_path) {
    ServerConfig cfg;
    cfg.config_path = config_path;

    // Parse TOML file if it exists
    if (std::filesystem::exists(config_path)) {
        try {
            auto tbl = toml::parse_file(config_path);
            spdlog::info("Loaded config from {}", config_path);

            // [server]
            if (auto v = tbl["server"]["host"].value<std::string>())
                cfg.host = *v;
            if (auto v = tbl["server"]["port"].value<int64_t>())
                cfg.port = safe_narrow(*v, "server.port", cfg.port);
            if (auto v = tbl["server"]["max_connections"].value<int64_t>())
                cfg.max_connections = safe_narrow(*v, "server.max_connections", cfg.max_connections);

            // [model]
            if (auto v = tbl["model"]["path"].value<std::string>())
                cfg.model_path = *v;
            if (auto v = tbl["model"]["default_voice"].value<std::string>())
                cfg.default_voice = *v;

            // [inference]
            if (auto v = tbl["inference"]["max_queue_depth"].value<int64_t>())
                cfg.max_queue_depth = safe_narrow(*v, "inference.max_queue_depth", cfg.max_queue_depth);
            if (auto v = tbl["inference"]["workers"].value<int64_t>())
                cfg.workers = safe_narrow(*v, "inference.workers", cfg.workers);
            if (auto v = tbl["inference"]["max_input_chars"].value<int64_t>())
                cfg.max_input_chars = safe_narrow(*v, "inference.max_input_chars", cfg.max_input_chars);
            if (auto v = tbl["inference"]["request_timeout_seconds"].value<int64_t>())
                cfg.request_timeout_seconds = safe_narrow(*v, "inference.request_timeout_seconds", cfg.request_timeout_seconds);

            // [auth]
            if (auto v = tbl["auth"]["require_auth"].value<bool>())
                cfg.require_auth = *v;
            if (auto v = tbl["auth"]["rate_limit_max"].value<int64_t>())
                cfg.auth_rate_limit_max = safe_narrow(*v, "auth.rate_limit_max", cfg.auth_rate_limit_max);
            if (auto v = tbl["auth"]["rate_limit_window"].value<int64_t>())
                cfg.auth_rate_limit_window = safe_narrow(*v, "auth.rate_limit_window", cfg.auth_rate_limit_window);
            if (auto v = tbl["auth"]["request_rate_limit_rpm"].value<int64_t>())
                cfg.request_rate_limit_rpm = safe_narrow(*v, "auth.request_rate_limit_rpm", cfg.request_rate_limit_rpm);
            if (auto v = tbl["auth"]["trust_proxy"].value<bool>())
                cfg.trust_proxy = *v;
            if (auto v = tbl["auth"]["trusted_proxy_hops"].value<int64_t>())
                cfg.trusted_proxy_hops = safe_narrow(*v, "auth.trusted_proxy_hops", cfg.trusted_proxy_hops);
            // Note: api_key is intentionally NOT loaded from TOML (security).
            // It must be set via TTS_API_KEY environment variable.

            // [logging]
            if (auto v = tbl["logging"]["level"].value<std::string>())
                cfg.log_level = *v;
            if (auto v = tbl["logging"]["format"].value<std::string>())
                cfg.log_format = *v;

        } catch (const toml::parse_error& err) {
            spdlog::error("Failed to parse {}: {}", config_path, err.description());
            spdlog::warn("Falling back to defaults + env vars");
        }
    } else {
        spdlog::warn("Config file {} not found, using defaults + env vars", config_path);
    }

    // Environment variables override TOML values
    apply_env_overrides(cfg);

    return cfg;
}

void ServerConfig::validate() const {
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid port: must be 1-65535, got " + std::to_string(port));
    }
    if (max_connections < 1) {
        throw std::invalid_argument("max_connections must be >= 1");
    }
    if (model_path.empty()) {
        throw std::invalid_argument(
            "model_path must not be empty — "
            "set [model].path in config or TTS_MODEL_PATH env var");
    }
    if (max_input_chars < 1) {
        throw std::invalid_argument("max_input_chars must be > 0");
    }
    if (workers < 1) {
        throw std::invalid_argument("workers must be >= 1");
    }
    if (max_queue_depth < 1) {
        throw std::invalid_argument("max_queue_depth must be >= 1");
    }
    if (request_timeout_seconds < 1) {
        throw std::invalid_argument("request_timeout_seconds must be >= 1");
    }
    if (auth_rate_limit_max < 1) {
        throw std::invalid_argument("auth_rate_limit_max must be >= 1");
    }
    if (auth_rate_limit_window < 1) {
        throw std::invalid_argument("auth_rate_limit_window must be >= 1");
    }
    if (request_rate_limit_rpm < 1) {
        throw std::invalid_argument("request_rate_limit_rpm must be >= 1");
    }
    if (trusted_proxy_hops < 1) {
        throw std::invalid_argument("trusted_proxy_hops must be >= 1");
    }
    if (require_auth && api_key.empty()) {
        throw std::invalid_argument(
            "TTS_REQUIRE_AUTH=true but TTS_API_KEY is empty — "
            "set TTS_API_KEY or disable auth with TTS_REQUIRE_AUTH=false");
    }
    // Validate log_level
    if (log_level != "trace" && log_level != "debug" && log_level != "info" &&
        log_level != "warn" && log_level != "error" && log_level != "critical") {
        throw std::invalid_argument(
            "Invalid log_level '" + log_level + "' — "
            "must be one of: trace, debug, info, warn, error, critical");
    }
    // Validate log_format
    if (log_format != "text" && log_format != "json") {
        throw std::invalid_argument(
            "Invalid log_format '" + log_format + "' — must be 'text' or 'json'");
    }
}

} // namespace tts::config
