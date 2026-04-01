#pragma once

/// @file http_adapter.hpp
/// @brief Adapter: bridges cpp-httplib ↔ abstract HttpRequest/HttpResponse.
///
/// This is the infrastructure adapter in the Hexagonal Architecture.
/// It translates between the concrete HTTP library (cpp-httplib) and
/// our abstract request/response types (request_handler.hpp).
/// Handler logic remains untouched — only this file knows about httplib.

#include "request_handler.hpp"
#include "tts/server.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// Forward declare httplib types to keep the header clean.
namespace httplib {
class Server;
class Request;
class Response;
}  // namespace httplib

namespace tts::server {

/// HTTP listener that adapts cpp-httplib to the TtsServer route handlers.
///
/// Lifecycle:
///   1. Construct with TtsServer reference
///   2. Call start() to begin listening (blocks until stop)
///   3. Call stop() from signal handler or another thread for graceful shutdown
///
/// The adapter owns the httplib::Server and registers routes that convert
/// httplib Request/Response ↔ our HttpRequest/HttpResponse abstractions.
class HttpAdapter {
public:
    /// @param tts_server  Fully initialized TtsServer (owns backend, pool, catalog).
    explicit HttpAdapter(TtsServer& tts_server);

    ~HttpAdapter();

    // Non-copyable, non-movable
    HttpAdapter(const HttpAdapter&) = delete;
    HttpAdapter& operator=(const HttpAdapter&) = delete;
    HttpAdapter(HttpAdapter&&) = delete;
    HttpAdapter& operator=(HttpAdapter&&) = delete;

    /// Start listening on the configured host:port. Blocks until stop().
    /// @return true if the server started and ran; false on bind failure.
    [[nodiscard]] bool start();

    /// Signal the listener to stop. Thread-safe; safe to call from signal handlers.
    void stop();

    /// Whether the listener is currently running.
    [[nodiscard]] bool is_running() const;

private:
    /// Convert httplib::Request → our HttpRequest.
    [[nodiscard]] static HttpRequest adapt_request(const httplib::Request& req);

    /// Write our HttpResponse → httplib::Response.
    static void adapt_response(const HttpResponse& from, httplib::Response& to);

    /// Register all routes on the httplib server.
    void register_routes();

    /// Build HandlerDeps from the TtsServer.
    [[nodiscard]] HandlerDeps make_deps() const;

    /// Run auth + rate-limit middleware. Returns nullopt if allowed, or error response.
    [[nodiscard]] std::optional<HttpResponse> run_middleware(
        const HttpRequest& req) const;

    TtsServer& tts_server_;
    std::unique_ptr<httplib::Server> server_;
    std::atomic<bool> running_{false};
};

}  // namespace tts::server
