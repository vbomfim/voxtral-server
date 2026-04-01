#pragma once

/// @file auth.hpp
/// @brief Authentication middleware and rate limiting for the TTS HTTP server.
///
/// Implements:
///   - Bearer token extraction and constant-time comparison
///   - Per-IP auth failure rate limiting (brute-force protection)
///   - Per-IP request rate limiting (RPM)
///   - X-Forwarded-For client IP extraction with proxy trust
///
/// Patterns ported from openasr/whisperx-streaming-server.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tts {

// ============================================================================
// Constant-time comparison
// ============================================================================

/// Constant-time string comparison to prevent timing side-channel attacks.
/// Returns true if both strings are equal. Never short-circuits.
/// Uses volatile to prevent compiler from eliding the loop — same pattern
/// as mbedTLS mbedtls_ct_memcmp() and libsodium sodium_memcmp().
inline bool constant_time_equals(std::string_view a, std::string_view b) {
    volatile uint8_t result = static_cast<uint8_t>(a.size() ^ b.size());
    const std::size_t len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < len; ++i) {
        result |= static_cast<uint8_t>(
            static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]));
    }
    return result == 0;
}

// ============================================================================
// IP Extraction
// ============================================================================

/// Extract the real client IP from request context.
/// When trust_proxy is false (default), always uses the socket address.
/// When trust_proxy is true, reads X-Forwarded-For and skips trusted_hops-1
/// entries from the right.
inline std::string extract_client_ip(std::string_view forwarded_for,
                                     std::string_view remote_addr,
                                     bool trust_proxy,
                                     int trusted_hops = 1) {
    if (!trust_proxy || forwarded_for.empty()) {
        return std::string(remote_addr);
    }

    auto remaining = forwarded_for;
    for (int hop = 0; hop < trusted_hops - 1; ++hop) {
        auto pos = remaining.rfind(',');
        if (pos == std::string_view::npos) {
            return std::string(remote_addr);
        }
        remaining = remaining.substr(0, pos);
    }

    auto pos = remaining.rfind(',');
    std::string_view ip = (pos != std::string_view::npos)
        ? remaining.substr(pos + 1)
        : remaining;

    // Trim OWS (RFC 7239)
    auto is_ows = [](char c) { return c == ' ' || c == '\t'; };
    while (!ip.empty() && is_ows(ip.front())) ip.remove_prefix(1);
    while (!ip.empty() && is_ows(ip.back())) ip.remove_suffix(1);

    return ip.empty() ? std::string(remote_addr) : std::string(ip);
}

// ============================================================================
// Auth Rate Limiter (per-IP auth failure tracking)
// ============================================================================

/// Per-IP auth failure tracking for brute-force protection.
/// Thread-safe. Sliding window counter with configurable max failures.
struct AuthRateLimiter {
    struct IpState {
        size_t failures = 0;
        std::chrono::steady_clock::time_point window_start =
            std::chrono::steady_clock::now();
    };

    size_t max_failures = 10;
    std::chrono::seconds window_secs{60};
    size_t max_tracked_ips = 10000;  // Memory exhaustion cap

    /// Record a failure. Returns true if the IP is now rate-limited.
    bool check_and_record_failure(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& state = ip_states_[ip];

        if (now - state.window_start > window_secs) {
            state.failures = 0;
            state.window_start = now;
        }

        state.failures++;
        bool is_blocked = state.failures > max_failures;

        if (max_tracked_ips > 0 && ip_states_.size() > max_tracked_ips) {
            evict_to_cap(now);
        }

        return is_blocked;
    }

    /// Check if an IP is blocked without recording a failure.
    bool is_blocked(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ip_states_.find(ip);
        if (it == ip_states_.end()) return false;

        auto now = std::chrono::steady_clock::now();
        if (now - it->second.window_start > window_secs) {
            ip_states_.erase(it);
            return false;
        }
        return it->second.failures > max_failures;
    }

    /// Current number of tracked IPs.
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ip_states_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, IpState> ip_states_;

    void evict_to_cap(std::chrono::steady_clock::time_point now) {
        // First: remove expired
        for (auto it = ip_states_.begin(); it != ip_states_.end();) {
            if (now - it->second.window_start > window_secs) {
                it = ip_states_.erase(it);
            } else {
                ++it;
            }
        }
        // Second: evict oldest if still over cap
        while (ip_states_.size() > max_tracked_ips) {
            auto oldest = ip_states_.begin();
            for (auto it = ip_states_.begin(); it != ip_states_.end(); ++it) {
                if (it->second.window_start < oldest->second.window_start) {
                    oldest = it;
                }
            }
            ip_states_.erase(oldest);
        }
    }
};

// ============================================================================
// Request Rate Limiter (per-IP RPM)
// ============================================================================

/// Per-IP request rate limiter using sliding window.
/// Thread-safe. Returns seconds until retry on limit hit.
struct RequestRateLimiter {
    struct IpState {
        size_t requests = 0;
        std::chrono::steady_clock::time_point window_start =
            std::chrono::steady_clock::now();
    };

    size_t max_rpm = 10;  // requests per minute
    size_t max_tracked_ips = 10000;

    /// Check if request is allowed. Returns 0 if OK, else seconds until retry.
    int check_request(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& state = ip_states_[ip];

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - state.window_start);

        if (elapsed.count() >= 60) {
            state.requests = 0;
            state.window_start = now;
        }

        state.requests++;

        if (state.requests > max_rpm) {
            auto remaining = 60 - elapsed.count();
            if (remaining <= 0) remaining = 1;
            return static_cast<int>(remaining);
        }

        // Enforce capacity cap
        if (max_tracked_ips > 0 && ip_states_.size() > max_tracked_ips) {
            for (auto it = ip_states_.begin(); it != ip_states_.end();) {
                auto e = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.window_start);
                if (e.count() >= 60) {
                    it = ip_states_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        return 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ip_states_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, IpState> ip_states_;
};

// ============================================================================
// Auth Result
// ============================================================================

/// Result of authentication check.
struct AuthResult {
    bool authenticated = false;
    bool rate_limited = false;
    int retry_after_seconds = 0;   ///< Retry-After header value (for 429)
    std::string error_code;        ///< OpenAI error code
    std::string error_message;     ///< Human-readable error
};

/// Check bearer token authentication.
/// @param auth_header  Full "Authorization" header value.
/// @param api_key      Expected API key.
/// @return AuthResult with authenticated=true on success.
AuthResult check_bearer_auth(std::string_view auth_header,
                             std::string_view api_key);

/// Check if a path is an internal endpoint that skips auth.
/// /health, /ready, /metrics skip auth.
inline bool is_internal_endpoint(std::string_view path) {
    return path == "/health" || path == "/ready" || path == "/metrics";
}

}  // namespace tts
