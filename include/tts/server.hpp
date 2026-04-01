#pragma once

/// @file server.hpp
/// @brief TtsServer — HTTP server facade for the TTS service.
///
/// Wires configuration, backend, voice catalog, inference pool,
/// authentication, and metrics into a cohesive server. The actual
/// HTTP transport (uWebSockets) is not wired yet — this class
/// provides the routing/dispatch logic that will be connected later.

#include "tts/auth.hpp"
#include "tts/backend.hpp"
#include "tts/inference_pool.hpp"
#include "tts/voices.hpp"
#include "config/config.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace tts {

/// The main TTS server class — owns the inference pool and routes requests.
///
/// Lifecycle:
///   1. Construct with config, backend, and voice catalog
///   2. Call initialize() to set up the inference pool
///   3. Route requests through route_request()
///   4. Call shutdown() for graceful stop (or destructor handles it)
class TtsServer {
public:
    TtsServer(const config::ServerConfig& config,
              std::shared_ptr<ITtsBackend> backend,
              std::shared_ptr<VoiceCatalog> voice_catalog);

    ~TtsServer();

    // Non-copyable, non-movable
    TtsServer(const TtsServer&) = delete;
    TtsServer& operator=(const TtsServer&) = delete;
    TtsServer(TtsServer&&) = delete;
    TtsServer& operator=(TtsServer&&) = delete;

    /// Initialize the inference pool. Returns false on failure.
    [[nodiscard]] bool initialize();

    /// Gracefully shut down: stop accepting, drain pool.
    void shutdown();

    /// Whether the server is initialized and ready.
    [[nodiscard]] bool is_ready() const;

    /// Server start time (for uptime calculation).
    [[nodiscard]] std::chrono::steady_clock::time_point start_time() const {
        return start_time_;
    }

    /// Access the underlying inference pool (for /ready probe metrics).
    [[nodiscard]] InferencePool* pool() const { return pool_.get(); }

    /// Access the voice catalog.
    [[nodiscard]] VoiceCatalog* voice_catalog() const {
        return voice_catalog_.get();
    }

    /// Access the backend.
    [[nodiscard]] std::shared_ptr<ITtsBackend> backend() const {
        return backend_;
    }

    /// Access the config.
    [[nodiscard]] const config::ServerConfig& config() const { return config_; }

    /// Auth rate limiter (for middleware).
    AuthRateLimiter& auth_rate_limiter() { return auth_limiter_; }

    /// Request rate limiter (for middleware).
    RequestRateLimiter& request_rate_limiter() { return request_limiter_; }

private:
    config::ServerConfig config_;
    std::shared_ptr<ITtsBackend> backend_;
    std::shared_ptr<VoiceCatalog> voice_catalog_;
    std::unique_ptr<InferencePool> pool_;

    AuthRateLimiter auth_limiter_;
    RequestRateLimiter request_limiter_;

    std::chrono::steady_clock::time_point start_time_;
    bool initialized_ = false;
};

}  // namespace tts
