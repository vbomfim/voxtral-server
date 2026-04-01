#include "config/config.hpp"
#include "logging/logger.hpp"
#include "tts/version.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config <path>   Path to TOML config file (default: config/server.toml)\n"
              << "  --version         Print version and exit\n"
              << "  --help            Print this help and exit\n"
              << "\nEnvironment variables (TTS_* prefix) override config file values.\n"
              << "See config/server.toml for all available options.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.toml";

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "voxtral-server v" << tts::VERSION << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config requires a path argument\n";
                return 1;
            }
            config_path = argv[++i];
        }
    }

    // TTS_CONFIG_PATH env var can also set the config path
    if (auto* env_path = std::getenv("TTS_CONFIG_PATH")) {
        config_path = env_path;
    }

    // Load config from TOML file + environment variables
    auto cfg = tts::config::ServerConfig::from_file_and_env(config_path);

    // Initialize logging (must happen early, before any structured log calls)
    tts::logging::initialize(cfg);

    spdlog::info("voxtral-server v{} starting...", tts::VERSION);
    spdlog::debug("Debug logging enabled (log_level={})", cfg.log_level);

    // Validate config
    try {
        cfg.validate();
    } catch (const std::invalid_argument& e) {
        spdlog::error("Configuration error: {}", e.what());
        return 1;
    }

    // Security: log auth status without revealing key value
    if (cfg.require_auth) {
        if (cfg.api_key.empty()) {
            // validate() should have caught this, but defense-in-depth
            spdlog::error("TTS_REQUIRE_AUTH=true but TTS_API_KEY is empty — aborting");
            return 1;
        }
        spdlog::info("API key authentication enabled (key=[REDACTED])");
        spdlog::info("Auth rate limit: {} attempts per {}s window",
            cfg.auth_rate_limit_max, cfg.auth_rate_limit_window);
    } else {
        spdlog::warn("Authentication disabled (TTS_REQUIRE_AUTH=false)");
    }

    spdlog::info("Config: host={} port={} workers={} max_connections={} max_input_chars={}",
        cfg.host, cfg.port, cfg.workers, cfg.max_connections, cfg.max_input_chars);
    spdlog::info("Rate limit: {} requests/min", cfg.request_rate_limit_rpm);

    // Phase 1: no server loop yet — just validate config and exit cleanly
    spdlog::info("Phase 1 scaffold — config loaded and validated. Exiting cleanly.");

    return 0;
}
