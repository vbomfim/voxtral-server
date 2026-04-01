/// @file auth.cpp
/// @brief Authentication implementation — bearer token validation.

#include "tts/auth.hpp"

namespace tts {

AuthResult check_bearer_auth(std::string_view auth_header,
                             std::string_view api_key) {
    AuthResult result;

    if (auth_header.empty()) {
        result.error_code = "invalid_api_key";
        result.error_message = "Missing Authorization header. "
                               "Use: Authorization: Bearer <api_key>";
        return result;
    }

    // Must start with "Bearer "
    constexpr std::string_view prefix = "Bearer ";
    if (auth_header.size() <= prefix.size() ||
        auth_header.substr(0, prefix.size()) != prefix) {
        result.error_code = "invalid_api_key";
        result.error_message = "Invalid Authorization header format. "
                               "Use: Authorization: Bearer <api_key>";
        return result;
    }

    auto token = auth_header.substr(prefix.size());

    if (!constant_time_equals(token, api_key)) {
        result.error_code = "invalid_api_key";
        result.error_message = "Invalid API key provided.";
        return result;
    }

    result.authenticated = true;
    return result;
}

}  // namespace tts
