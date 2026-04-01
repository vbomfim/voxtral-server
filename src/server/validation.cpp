/// @file validation.cpp
/// @brief Request validation implementation.

#include "tts/validation.hpp"
#include "tts/voices.hpp"

#include <algorithm>
#include <cctype>

namespace tts {

ValidationResult make_validation_error(int status,
                                       const std::string& code,
                                       const std::string& message,
                                       const std::string& param) {
    ValidationResult r;
    r.valid = false;
    r.http_status = status;
    r.error_type = "invalid_request_error";
    r.error_code = code;
    r.error_message = message;
    r.error_param = param;
    return r;
}

ValidationResult validate_content_type(const std::string& content_type) {
    // Accept "application/json" or "application/json; charset=utf-8" etc.
    if (content_type.find("application/json") == std::string::npos) {
        return make_validation_error(
            400, "invalid_content_type",
            "Content-Type must be application/json.",
            "");
    }
    return {};
}

ValidationResult validate_body_size(size_t body_size, size_t max_body_bytes) {
    if (body_size > max_body_bytes) {
        return make_validation_error(
            413, "payload_too_large",
            "Request body exceeds maximum size of 1MB.",
            "");
    }
    return {};
}

ValidationResult validate_model(const std::string& model) {
    if (model.empty()) {
        return make_validation_error(
            400, "missing_input",
            "Missing required parameter: 'model'.",
            "model");
    }
    if (model != "voxtral-4b") {
        return make_validation_error(
            400, "invalid_model",
            "Invalid model. Supported models: voxtral-4b.",
            "model");
    }
    return {};
}

ValidationResult validate_input(const std::string& input, int max_chars) {
    if (input.empty()) {
        return make_validation_error(
            400, "missing_input",
            "Missing required parameter: 'input'.",
            "input");
    }

    // Reject whitespace-only input
    bool all_whitespace = std::all_of(input.begin(), input.end(),
        [](unsigned char c) { return std::isspace(c) != 0; });
    if (all_whitespace) {
        return make_validation_error(
            400, "missing_input",
            "Input must not be empty or whitespace-only.",
            "input");
    }

    if (max_chars > 0 && static_cast<int>(input.size()) > max_chars) {
        return make_validation_error(
            400, "string_above_max_length",
            "Input exceeds maximum length of " + std::to_string(max_chars) + " characters.",
            "input");
    }

    return {};
}

ValidationResult validate_voice(const std::string& voice,
                                const VoiceCatalog& catalog) {
    if (voice.empty()) {
        return make_validation_error(
            400, "missing_input",
            "Missing required parameter: 'voice'.",
            "voice");
    }

    if (!catalog.is_valid(voice)) {
        return make_validation_error(
            400, "invalid_voice",
            "Invalid voice: '" + voice + "'. Available voices: " +
                catalog.list_valid_ids() + ".",
            "voice");
    }

    return {};
}

ValidationResult validate_response_format(const std::string& format) {
    if (format.empty() || format == "wav") {
        return {};
    }
    return make_validation_error(
        400, "invalid_format",
        "Invalid response_format: '" + format +
            "'. Supported formats: wav.",
        "response_format");
}

ValidationResult validate_speed(float speed) {
    constexpr float kMinSpeed = 0.25F;
    constexpr float kMaxSpeed = 4.0F;

    if (speed < kMinSpeed || speed > kMaxSpeed) {
        return make_validation_error(
            400, "speed_out_of_range",
            "Speed must be between 0.25 and 4.0.",
            "speed");
    }
    return {};
}

}  // namespace tts
