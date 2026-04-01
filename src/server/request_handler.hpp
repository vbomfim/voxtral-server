#pragma once

/// @file request_handler.hpp
/// @brief Abstract HTTP request/response types and route handlers.
///
/// Provides testable abstractions that decouple route logic from the
/// actual HTTP server (uWebSockets). Handlers are pure functions:
///   (HttpRequest, Dependencies) → HttpResponse
///
/// This facade pattern allows testing all request logic without
/// starting a real listening server.

#include "tts/auth.hpp"
#include "tts/backend.hpp"
#include "tts/inference_pool.hpp"
#include "tts/metrics.hpp"
#include "tts/validation.hpp"
#include "tts/voices.hpp"
#include "config/config.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tts::server {

// ============================================================================
// Abstract HTTP types
// ============================================================================

/// Abstract HTTP request — populated from uWebSockets (or test code).
struct HttpRequest {
    std::string method;                              ///< "GET", "POST"
    std::string path;                                ///< "/v1/audio/speech"
    std::string body;                                ///< Raw body content
    std::map<std::string, std::string> headers;      ///< Lowercase header names
    std::string remote_addr = "127.0.0.1";           ///< Socket peer address

    /// Get a header value (case-insensitive lookup; keys stored lowercase).
    [[nodiscard]] std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it != headers.end() ? it->second : "";
    }
};

/// Abstract HTTP response — returned by handlers, sent via uWebSockets.
struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;                      ///< Text body (JSON errors, health)
    std::vector<uint8_t> binary_body;      ///< Binary body (audio/wav)

    /// Whether the response carries binary data (audio).
    [[nodiscard]] bool is_binary() const { return !binary_body.empty(); }
};

// ============================================================================
// Dependencies — injected into handlers
// ============================================================================

/// All dependencies a request handler needs, injected for testability.
struct HandlerDeps {
    std::shared_ptr<ITtsBackend> backend;
    VoiceCatalog* voice_catalog = nullptr;
    InferencePool* pool = nullptr;
    const config::ServerConfig* config = nullptr;
    std::chrono::steady_clock::time_point start_time =
        std::chrono::steady_clock::now();
};

// ============================================================================
// Route handlers — pure functions
// ============================================================================

/// POST /v1/audio/speech — synthesize text to audio.
[[nodiscard]] HttpResponse handle_speech(const HttpRequest& req,
                                         const HandlerDeps& deps);

/// GET /v1/voices — list available voices.
[[nodiscard]] HttpResponse handle_voices(const HandlerDeps& deps);

/// GET /health — liveness probe.
[[nodiscard]] HttpResponse handle_health(const HandlerDeps& deps);

/// GET /ready — readiness probe.
[[nodiscard]] HttpResponse handle_ready(const HandlerDeps& deps);

/// GET /metrics — Prometheus text format.
[[nodiscard]] HttpResponse handle_metrics();

// ============================================================================
// Error response helpers
// ============================================================================

/// Build an OpenAI-format JSON error response.
[[nodiscard]] HttpResponse make_error_response(
    int status,
    const std::string& error_type,
    const std::string& error_code,
    const std::string& message,
    const std::string& param = "");

/// Build an error response from a ValidationResult.
[[nodiscard]] HttpResponse make_error_from_validation(const ValidationResult& v);

// ============================================================================
// Security headers
// ============================================================================

/// Apply mandatory security headers to a response.
void apply_security_headers(HttpResponse& resp, const std::string& request_id);

/// Generate a UUID v4 request ID.
[[nodiscard]] std::string generate_request_id();

}  // namespace tts::server
