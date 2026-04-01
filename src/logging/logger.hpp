#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

// Forward declare to avoid pulling config.hpp into every translation unit
namespace tts::config { struct ServerConfig; }

namespace tts::logging {

/// Initialize the global spdlog logger from configuration.
/// Sets log level and formatter (text or JSON).
/// Must be called once at startup, before any log calls.
void initialize(const tts::config::ServerConfig& cfg);

/// Set a thread-local request ID for log correlation.
/// Call at the start of request handling; clear with "".
void set_request_id(const std::string& request_id);

/// Get the current thread-local request ID.
/// Returns empty string if no request ID is set.
const std::string& get_request_id();

/// Parse a log level string to spdlog level enum.
[[nodiscard]] spdlog::level::level_enum parse_log_level(const std::string& level);

/// Create a JSON formatter instance for use with spdlog.
/// Exposed for testing and custom logger creation.
std::unique_ptr<spdlog::formatter> make_json_formatter();

} // namespace tts::logging
