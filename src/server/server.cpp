/// @file server.cpp
/// @brief TtsServer implementation.

#include "tts/server.hpp"

#include <spdlog/spdlog.h>

namespace tts {

TtsServer::TtsServer(const config::ServerConfig& config,
                     std::shared_ptr<ITtsBackend> backend,
                     std::shared_ptr<VoiceCatalog> voice_catalog)
    : config_(config)
    , backend_(std::move(backend))
    , voice_catalog_(std::move(voice_catalog))
    , start_time_(std::chrono::steady_clock::now())
{
    // Configure rate limiters from config
    auth_limiter_.max_failures = static_cast<size_t>(config_.auth_rate_limit_max);
    auth_limiter_.window_secs = std::chrono::seconds(config_.auth_rate_limit_window);
    auth_limiter_.max_tracked_ips = 10000;

    request_limiter_.max_rpm = static_cast<size_t>(config_.request_rate_limit_rpm);
    request_limiter_.max_tracked_ips = 10000;
}

TtsServer::~TtsServer() {
    if (initialized_) {
        shutdown();
    }
}

bool TtsServer::initialize() {
    if (initialized_) {
        spdlog::warn("TtsServer::initialize() called more than once");
        return true;
    }

    if (!backend_) {
        spdlog::error("TtsServer::initialize() — backend is null");
        return false;
    }

    if (!backend_->is_ready()) {
        spdlog::error("TtsServer::initialize() — backend is not ready");
        return false;
    }

    try {
        pool_ = std::make_unique<InferencePool>(
            backend_,
            config_.max_queue_depth,
            config_.request_timeout_seconds);
    } catch (const std::exception& e) {
        spdlog::error("TtsServer::initialize() — failed to create pool: {}",
                      e.what());
        return false;
    }

    initialized_ = true;
    spdlog::info("TtsServer initialized — queue_depth={} timeout={}s",
                 config_.max_queue_depth, config_.request_timeout_seconds);
    return true;
}

void TtsServer::shutdown() {
    if (!initialized_) return;

    spdlog::info("TtsServer shutting down...");
    if (pool_) {
        pool_->shutdown();
    }
    initialized_ = false;
    spdlog::info("TtsServer shutdown complete");
}

bool TtsServer::is_ready() const {
    return initialized_ && backend_ && backend_->is_ready() &&
           pool_ && pool_->is_accepting();
}

}  // namespace tts
