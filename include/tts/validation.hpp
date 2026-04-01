#pragma once

/// @file validation.hpp
/// @brief Request validation for OpenAI-compatible TTS API.
///
/// Validates POST /v1/audio/speech request bodies against the spec:
///   - model: required, whitelist
///   - input: required, length-bounded, non-empty
///   - voice: required, must exist in VoiceCatalog
///   - response_format: optional, whitelist
///   - speed: optional, bounded range
///   - Content-Type: must be application/json
///   - Body size: max 1MB

#include <string>

namespace tts {

// Forward declare to avoid pulling in the full header
class VoiceCatalog;

/// Validation result with OpenAI-compatible error information.
struct ValidationResult {
    bool valid = true;
    int http_status = 200;      ///< 400, 413, etc.
    std::string error_type;     ///< "invalid_request_error"
    std::string error_code;     ///< e.g. "missing_input", "invalid_voice"
    std::string error_message;  ///< Human-readable
    std::string error_param;    ///< Field name or empty
};

/// Build a failing ValidationResult.
ValidationResult make_validation_error(int status,
                                       const std::string& code,
                                       const std::string& message,
                                       const std::string& param = "");

/// Validate Content-Type header is application/json.
[[nodiscard]] ValidationResult validate_content_type(const std::string& content_type);

/// Validate request body size is within max_body_bytes (default 1MB).
[[nodiscard]] ValidationResult validate_body_size(size_t body_size,
                                                   size_t max_body_bytes = 1048576);

/// Validate the "model" field.
[[nodiscard]] ValidationResult validate_model(const std::string& model);

/// Validate the "input" field.
/// @param max_chars  Maximum allowed characters (default 4096).
[[nodiscard]] ValidationResult validate_input(const std::string& input,
                                               int max_chars = 4096);

/// Validate the "voice" field against the voice catalog.
[[nodiscard]] ValidationResult validate_voice(const std::string& voice,
                                               const VoiceCatalog& catalog);

/// Validate the "response_format" field (optional, defaults to "wav").
[[nodiscard]] ValidationResult validate_response_format(const std::string& format);

/// Validate the "speed" field (optional, 0.25–4.0).
[[nodiscard]] ValidationResult validate_speed(float speed);

}  // namespace tts
