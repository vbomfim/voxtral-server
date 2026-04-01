#include "config/config.hpp"
#include "logging/logger.hpp"
#include "server/http_adapter.hpp"
#include "tts/server.hpp"
#include "tts/version.hpp"
#include "tts/voxtral_backend.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

// ============================================================================
// Signal handling — graceful shutdown on SIGTERM / SIGINT
// ============================================================================

namespace {
std::atomic<bool> g_shutdown_requested{false};
tts::server::HttpAdapter* g_adapter = nullptr;

void signal_handler(int signum) {
    if (!g_shutdown_requested.exchange(true)) {
        // write() is async-signal-safe; spdlog is not — avoid it here.
        const char* msg = (signum == SIGTERM)
            ? "\nReceived SIGTERM, shutting down...\n"
            : "\nReceived SIGINT, shutting down...\n";
        // Suppress unused-result: write in signal handler is best-effort
        ssize_t result = write(STDOUT_FILENO, msg, strlen(msg));
        (void)result;

        if (g_adapter) {
            g_adapter->stop();
        }
    }
}
}  // namespace

// ============================================================================
// CLI
// ============================================================================

static void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config <path>   Path to TOML config file (default: config/server.toml)\n"
              << "  --version         Print version and exit\n"
              << "  --help            Print this help and exit\n"
              << "\nEnvironment variables (TTS_* prefix) override config file values.\n"
              << "See config/server.toml for all available options.\n";
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.toml";

    // TTS_CONFIG_PATH env var can override the default (lower priority than CLI)
    if (auto* env_path = std::getenv("TTS_CONFIG_PATH")) {
        config_path = env_path;
    }

    // Parse command-line arguments (--config wins over env var)
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

    // --- Initialize backend (stub until real model is wired) ---
    auto backend = std::make_shared<tts::VoxtralBackend>();
    if (!backend->initialize(cfg.model_path)) {
        spdlog::error("Failed to initialize TTS backend (model_path={})",
                      cfg.model_path);
        return 1;
    }
    spdlog::info("TTS backend initialized: {}", backend->model_name());

    // --- Initialize voice catalog ---
    auto voice_catalog = std::make_shared<tts::VoiceCatalog>();
    spdlog::info("Voice catalog loaded: {} voices",
                 voice_catalog->all().size());

    // --- Initialize TtsServer (inference pool, rate limiters) ---
    tts::TtsServer tts_server(cfg, backend, voice_catalog);
    if (!tts_server.initialize()) {
        spdlog::error("Failed to initialize TTS server");
        return 1;
    }

    // --- Install signal handlers ---
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART — let listen() be interrupted
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // --- Start HTTP listener (blocks until signal) ---
    tts::server::HttpAdapter adapter(tts_server);
    g_adapter = &adapter;

    spdlog::info("Starting HTTP listener on {}:{}...", cfg.host, cfg.port);
    bool listen_ok = adapter.start();

    g_adapter = nullptr;

    // --- Graceful shutdown ---
    spdlog::info("Shutting down TTS server...");
    tts_server.shutdown();
    spdlog::info("voxtral-server stopped.");

    return listen_ok ? 0 : 1;
}
