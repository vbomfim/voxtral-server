/// @file test_auth.cpp
/// @brief Developer unit tests for authentication and rate limiting.
///
/// Tests bearer token validation, constant-time comparison,
/// rate limiting, IP extraction, and internal endpoint detection.

#include <gtest/gtest.h>

#include "tts/auth.hpp"

#include <string>
#include <vector>

using namespace tts;

// ============================================================================
// Constant-time comparison
// ============================================================================

TEST(ConstantTimeEquals, IdenticalStringsMatch) {
    EXPECT_TRUE(constant_time_equals("hello", "hello"));
}

TEST(ConstantTimeEquals, DifferentStringsDoNotMatch) {
    EXPECT_FALSE(constant_time_equals("hello", "world"));
}

TEST(ConstantTimeEquals, DifferentLengthsDoNotMatch) {
    EXPECT_FALSE(constant_time_equals("short", "longer_string"));
}

TEST(ConstantTimeEquals, EmptyStringsMatch) {
    EXPECT_TRUE(constant_time_equals("", ""));
}

TEST(ConstantTimeEquals, EmptyVsNonEmptyDoNotMatch) {
    EXPECT_FALSE(constant_time_equals("", "notempty"));
    EXPECT_FALSE(constant_time_equals("notempty", ""));
}

TEST(ConstantTimeEquals, SingleCharDifference) {
    EXPECT_FALSE(constant_time_equals("abc", "abd"));
}

TEST(ConstantTimeEquals, LongIdenticalStrings) {
    std::string a(1000, 'x');
    std::string b(1000, 'x');
    EXPECT_TRUE(constant_time_equals(a, b));
}

TEST(ConstantTimeEquals, LongDifferentStrings) {
    std::string a(1000, 'x');
    std::string b(1000, 'y');
    EXPECT_FALSE(constant_time_equals(a, b));
}

// ============================================================================
// Bearer token auth
// ============================================================================

TEST(BearerAuth, ValidTokenAuthenticated) {
    auto result = check_bearer_auth("Bearer my-secret-key", "my-secret-key");
    EXPECT_TRUE(result.authenticated);
    EXPECT_TRUE(result.error_code.empty());
}

TEST(BearerAuth, MissingHeaderReturns401) {
    auto result = check_bearer_auth("", "my-secret-key");
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error_code, "invalid_api_key");
}

TEST(BearerAuth, WrongTokenReturns401) {
    auto result = check_bearer_auth("Bearer wrong-key", "my-secret-key");
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error_code, "invalid_api_key");
}

TEST(BearerAuth, MissingBearerPrefixReturns401) {
    auto result = check_bearer_auth("my-secret-key", "my-secret-key");
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error_code, "invalid_api_key");
}

TEST(BearerAuth, BasicAuthPrefixReturns401) {
    auto result = check_bearer_auth("Basic dXNlcjpwYXNz", "my-secret-key");
    EXPECT_FALSE(result.authenticated);
}

TEST(BearerAuth, BearerOnlyNoTokenReturns401) {
    // "Bearer " with no token should fail — the empty string after prefix
    // means no actual token was provided
    auto result = check_bearer_auth("Bearer ", "my-secret-key");
    EXPECT_FALSE(result.authenticated);
}

TEST(BearerAuth, EmptyApiKeyWithEmptyBearerFails) {
    // Even if api_key is empty, "Bearer " alone doesn't pass because the
    // header check catches it as too short (size <= prefix size)
    auto result = check_bearer_auth("Bearer ", "");
    EXPECT_FALSE(result.authenticated);
}

TEST(BearerAuth, ErrorMessageDoesNotLeakKey) {
    auto result = check_bearer_auth("Bearer wrong", "super-secret-key-123");
    EXPECT_FALSE(result.authenticated);
    // Error message must NOT contain the actual key
    EXPECT_EQ(result.error_message.find("super-secret-key-123"),
              std::string::npos);
}

// ============================================================================
// Internal endpoint detection
// ============================================================================

TEST(InternalEndpoints, HealthSkipsAuth) {
    EXPECT_TRUE(is_internal_endpoint("/health"));
}

TEST(InternalEndpoints, ReadySkipsAuth) {
    EXPECT_TRUE(is_internal_endpoint("/ready"));
}

TEST(InternalEndpoints, MetricsSkipsAuth) {
    EXPECT_TRUE(is_internal_endpoint("/metrics"));
}

TEST(InternalEndpoints, SpeechDoesNotSkip) {
    EXPECT_FALSE(is_internal_endpoint("/v1/audio/speech"));
}

TEST(InternalEndpoints, VoicesDoesNotSkip) {
    EXPECT_FALSE(is_internal_endpoint("/v1/voices"));
}

TEST(InternalEndpoints, RootDoesNotSkip) {
    EXPECT_FALSE(is_internal_endpoint("/"));
}

// ============================================================================
// IP Extraction
// ============================================================================

TEST(IpExtraction, NoProxyUsesRemoteAddr) {
    auto ip = extract_client_ip("1.2.3.4", "10.0.0.1", false);
    EXPECT_EQ(ip, "10.0.0.1");
}

TEST(IpExtraction, TrustProxyUsesForwardedFor) {
    auto ip = extract_client_ip("1.2.3.4", "10.0.0.1", true);
    EXPECT_EQ(ip, "1.2.3.4");
}

TEST(IpExtraction, MultipleProxiesTakesRightmost) {
    auto ip = extract_client_ip("1.1.1.1, 2.2.2.2, 3.3.3.3",
                                 "10.0.0.1", true, 1);
    EXPECT_EQ(ip, "3.3.3.3");
}

TEST(IpExtraction, TwoHopsSkipsOne) {
    auto ip = extract_client_ip("1.1.1.1, 2.2.2.2, 3.3.3.3",
                                 "10.0.0.1", true, 2);
    EXPECT_EQ(ip, "2.2.2.2");
}

TEST(IpExtraction, EmptyForwardedForFallsBack) {
    auto ip = extract_client_ip("", "10.0.0.1", true);
    EXPECT_EQ(ip, "10.0.0.1");
}

TEST(IpExtraction, TrimsWhitespace) {
    auto ip = extract_client_ip("  1.2.3.4  ", "10.0.0.1", true);
    EXPECT_EQ(ip, "1.2.3.4");
}

TEST(IpExtraction, TooManyHopsFallsBack) {
    // Only 1 IP but 3 hops requested — can't skip enough
    auto ip = extract_client_ip("1.2.3.4", "10.0.0.1", true, 3);
    EXPECT_EQ(ip, "10.0.0.1");
}

// ============================================================================
// Auth Rate Limiter
// ============================================================================

TEST(AuthRateLimiter, AllowsUpToMaxFailures) {
    AuthRateLimiter limiter;
    limiter.max_failures = 3;
    limiter.window_secs = std::chrono::seconds(60);

    // First 3 failures: not blocked yet
    EXPECT_FALSE(limiter.check_and_record_failure("1.2.3.4"));
    EXPECT_FALSE(limiter.check_and_record_failure("1.2.3.4"));
    EXPECT_FALSE(limiter.check_and_record_failure("1.2.3.4"));

    // 4th failure: now blocked
    EXPECT_TRUE(limiter.check_and_record_failure("1.2.3.4"));
}

TEST(AuthRateLimiter, DifferentIpsAreIndependent) {
    AuthRateLimiter limiter;
    limiter.max_failures = 2;

    limiter.check_and_record_failure("1.1.1.1");
    limiter.check_and_record_failure("1.1.1.1");

    // 1.1.1.1 is now at limit, but 2.2.2.2 is clean
    EXPECT_FALSE(limiter.is_blocked("2.2.2.2"));
}

TEST(AuthRateLimiter, IsBlockedReflectsState) {
    AuthRateLimiter limiter;
    limiter.max_failures = 1;

    EXPECT_FALSE(limiter.is_blocked("3.3.3.3"));

    limiter.check_and_record_failure("3.3.3.3");  // 1st: not blocked yet
    EXPECT_FALSE(limiter.is_blocked("3.3.3.3"));

    limiter.check_and_record_failure("3.3.3.3");  // 2nd: now blocked
    EXPECT_TRUE(limiter.is_blocked("3.3.3.3"));
}

TEST(AuthRateLimiter, TracksSize) {
    AuthRateLimiter limiter;
    EXPECT_EQ(limiter.size(), 0U);

    limiter.check_and_record_failure("a");
    limiter.check_and_record_failure("b");
    EXPECT_EQ(limiter.size(), 2U);
}

TEST(AuthRateLimiter, MemoryCapEnforced) {
    AuthRateLimiter limiter;
    limiter.max_tracked_ips = 5;
    limiter.max_failures = 100;  // won't block

    for (int i = 0; i < 20; i++) {
        limiter.check_and_record_failure("ip_" + std::to_string(i));
    }
    // Should be capped
    EXPECT_LE(limiter.size(), 6U);  // Allow +1 for insertion before eviction
}

// ============================================================================
// Request Rate Limiter
// ============================================================================

TEST(RequestRateLimiter, AllowsUpToMaxRPM) {
    RequestRateLimiter limiter;
    limiter.max_rpm = 3;

    EXPECT_EQ(limiter.check_request("1.2.3.4"), 0);  // 1st
    EXPECT_EQ(limiter.check_request("1.2.3.4"), 0);  // 2nd
    EXPECT_EQ(limiter.check_request("1.2.3.4"), 0);  // 3rd

    // 4th: rate limited, returns retry-after seconds
    int retry_after = limiter.check_request("1.2.3.4");
    EXPECT_GT(retry_after, 0);
}

TEST(RequestRateLimiter, DifferentIpsAreIndependent) {
    RequestRateLimiter limiter;
    limiter.max_rpm = 1;

    EXPECT_EQ(limiter.check_request("1.1.1.1"), 0);  // OK
    EXPECT_EQ(limiter.check_request("2.2.2.2"), 0);  // OK — different IP
}

TEST(RequestRateLimiter, RetryAfterIsPositive) {
    RequestRateLimiter limiter;
    limiter.max_rpm = 1;

    limiter.check_request("test_ip");
    int retry = limiter.check_request("test_ip");  // 2nd = over limit
    EXPECT_GT(retry, 0);
    EXPECT_LE(retry, 60);
}

TEST(RequestRateLimiter, TracksSize) {
    RequestRateLimiter limiter;
    EXPECT_EQ(limiter.size(), 0U);
    limiter.check_request("a");
    limiter.check_request("b");
    EXPECT_EQ(limiter.size(), 2U);
}
