/// @file http_adapter.cpp
/// @brief Adapter implementation: cpp-httplib ↔ abstract request/response.

#include "http_adapter.hpp"
#include "tts/auth.hpp"
#include "tts/metrics.hpp"
#include "logging/logger.hpp"

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>

namespace tts::server {

// ============================================================================
// Construction / Destruction
// ============================================================================

HttpAdapter::HttpAdapter(TtsServer& tts_server)
    : tts_server_(tts_server)
    , server_(std::make_unique<httplib::Server>())
{
    register_routes();
}

HttpAdapter::~HttpAdapter() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool HttpAdapter::start() {
    const auto& cfg = tts_server_.config();

    // Set read/write timeouts (seconds)
    server_->set_read_timeout(cfg.request_timeout_seconds, 0);
    server_->set_write_timeout(cfg.request_timeout_seconds, 0);

    // Limit payload size to 1 MB (matches validate_body_size default)
    server_->set_payload_max_length(1048576);

    running_ = true;
    spdlog::info("HTTP listener starting on {}:{}", cfg.host, cfg.port);

    bool ok = server_->listen(cfg.host, cfg.port);
    running_ = false;

    if (!ok) {
        spdlog::error("HTTP listener failed to bind {}:{}", cfg.host, cfg.port);
    }
    return ok;
}

void HttpAdapter::stop() {
    if (server_ && running_) {
        spdlog::info("HTTP listener stopping...");
        server_->stop();
        // running_ is set to false when listen() returns in start()
    }
}

bool HttpAdapter::is_running() const {
    return running_.load();
}

// ============================================================================
// Request / Response adaptation
// ============================================================================

HttpRequest HttpAdapter::adapt_request(const httplib::Request& req) {
    HttpRequest r;
    r.method = req.method;
    r.path = req.path;
    r.body = req.body;
    r.remote_addr = req.remote_addr;

    // Copy headers with lowercase keys (httplib already lowercases)
    for (const auto& [key, value] : req.headers) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(),
                       lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        r.headers[lower_key] = value;
    }

    return r;
}

void HttpAdapter::adapt_response(const HttpResponse& from, httplib::Response& to) {
    to.status = from.status;

    // Copy headers
    for (const auto& [key, value] : from.headers) {
        to.set_header(key, value);
    }

    if (from.is_binary()) {
        // Binary response (audio/wav)
        to.set_content(
            std::string(reinterpret_cast<const char*>(from.binary_body.data()),
                        from.binary_body.size()),
            from.headers.count("Content-Type")
                ? from.headers.at("Content-Type")
                : "application/octet-stream");
    } else {
        // Text response (JSON, Prometheus text)
        to.set_content(
            from.body,
            from.headers.count("Content-Type")
                ? from.headers.at("Content-Type")
                : "text/plain");
    }
}

// ============================================================================
// Dependencies
// ============================================================================

HandlerDeps HttpAdapter::make_deps() const {
    HandlerDeps deps;
    deps.backend = tts_server_.backend();
    deps.voice_catalog = tts_server_.voice_catalog();
    deps.pool = tts_server_.pool();
    deps.config = &tts_server_.config();
    deps.start_time = tts_server_.start_time();
    return deps;
}

// ============================================================================
// Middleware: auth + rate limiting
// ============================================================================

std::optional<HttpResponse> HttpAdapter::run_middleware(
    const HttpRequest& req) const {
    const auto& cfg = tts_server_.config();

    // Internal endpoints skip auth and rate limiting
    if (is_internal_endpoint(req.path)) {
        return std::nullopt;
    }

    // Extract client IP for rate limiting
    std::string client_ip = extract_client_ip(
        req.header("x-forwarded-for"),
        req.remote_addr,
        cfg.trust_proxy,
        cfg.trusted_proxy_hops);

    // 1. Auth rate limit check (brute-force protection)
    if (cfg.require_auth &&
        tts_server_.auth_rate_limiter().is_blocked(client_ip)) {
        Metrics::instance().requests_rejected_auth.Increment();
        auto resp = make_error_response(429, "rate_limit_error",
            "auth_rate_limited",
            "Too many authentication failures. Try again later.");
        resp.headers["Retry-After"] = "60";
        return resp;
    }

    // 2. Request rate limit check
    int retry_after =
        tts_server_.request_rate_limiter().check_request(client_ip);
    if (retry_after > 0) {
        Metrics::instance().requests_rejected_rate_limit.Increment();
        auto resp = make_error_response(429, "rate_limit_error",
            "rate_limited",
            "Rate limit exceeded. Please slow down.");
        resp.headers["Retry-After"] = std::to_string(retry_after);
        return resp;
    }

    // 3. Bearer token auth
    if (cfg.require_auth) {
        auto auth_result = check_bearer_auth(
            req.header("authorization"), cfg.api_key);

        if (!auth_result.authenticated) {
            // Record failure for brute-force tracking
            tts_server_.auth_rate_limiter().check_and_record_failure(client_ip);
            Metrics::instance().requests_rejected_auth.Increment();
            return make_error_response(401, "authentication_error",
                auth_result.error_code, auth_result.error_message);
        }
    }

    return std::nullopt;  // All checks passed
}

// ============================================================================
// Route registration
// ============================================================================

void HttpAdapter::register_routes() {
    // GET /health — liveness probe (no auth, no rate limit)
    server_->Get("/health", [this](const httplib::Request& req,
                                    httplib::Response& res) {
        auto abstract_req = adapt_request(req);
        auto request_id = generate_request_id();
        logging::set_request_id(request_id);

        auto response = handle_health(make_deps());
        apply_security_headers(response, request_id);
        adapt_response(response, res);

        logging::set_request_id("");
    });

    // GET /ready — readiness probe (no auth, no rate limit)
    server_->Get("/ready", [this](const httplib::Request& req,
                                   httplib::Response& res) {
        auto abstract_req = adapt_request(req);
        auto request_id = generate_request_id();
        logging::set_request_id(request_id);

        auto response = handle_ready(make_deps());
        apply_security_headers(response, request_id);
        adapt_response(response, res);

        logging::set_request_id("");
    });

    // GET /metrics — Prometheus exposition (no auth, no rate limit)
    server_->Get("/metrics", [](const httplib::Request& /*req*/,
                                     httplib::Response& res) {
        auto response = handle_metrics();
        adapt_response(response, res);
    });

    // GET /v1/voices — voice catalog
    server_->Get("/v1/voices", [this](const httplib::Request& req,
                                       httplib::Response& res) {
        auto abstract_req = adapt_request(req);
        auto request_id = generate_request_id();
        logging::set_request_id(request_id);

        // Run middleware (auth + rate limit)
        auto middleware_err = run_middleware(abstract_req);
        if (middleware_err) {
            apply_security_headers(*middleware_err, request_id);
            adapt_response(*middleware_err, res);
            logging::set_request_id("");
            return;
        }

        auto response = handle_voices(make_deps());
        apply_security_headers(response, request_id);
        adapt_response(response, res);

        logging::set_request_id("");
    });

    // POST /v1/audio/speech — TTS synthesis
    server_->Post("/v1/audio/speech", [this](const httplib::Request& req,
                                              httplib::Response& res) {
        auto abstract_req = adapt_request(req);
        auto request_id = generate_request_id();
        logging::set_request_id(request_id);

        spdlog::info("POST /v1/audio/speech from {} ({} bytes)",
                     abstract_req.remote_addr, abstract_req.body.size());

        // Run middleware (auth + rate limit)
        auto middleware_err = run_middleware(abstract_req);
        if (middleware_err) {
            apply_security_headers(*middleware_err, request_id);
            adapt_response(*middleware_err, res);
            logging::set_request_id("");
            return;
        }

        auto response = handle_speech(abstract_req, make_deps());
        apply_security_headers(response, request_id);
        adapt_response(response, res);

        spdlog::info("POST /v1/audio/speech → {} ({} bytes)",
                     response.status,
                     response.is_binary()
                         ? response.binary_body.size()
                         : response.body.size());
        logging::set_request_id("");
    });

    // Catch-all: 404 for unknown routes
    server_->set_error_handler([](const httplib::Request& /*req*/,
                                   httplib::Response& res) {
        if (res.status == 404) {
            auto response = make_error_response(404, "invalid_request_error",
                "not_found", "The requested endpoint does not exist.");
            auto request_id = generate_request_id();
            apply_security_headers(response, request_id);

            for (const auto& [key, value] : response.headers) {
                res.set_header(key, value);
            }
            res.set_content(response.body, "application/json");
        }
    });
}

}  // namespace tts::server
