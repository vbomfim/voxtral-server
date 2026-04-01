/// @file test_server.cpp
/// @brief Developer unit tests for TtsServer lifecycle and wiring.

#include <gtest/gtest.h>

#include "tts/server.hpp"
#include "tts/mock_backend.hpp"

#include <memory>

using namespace tts;

// ============================================================================
// Construction and initialization
// ============================================================================

TEST(TtsServer, ConstructsSuccessfully) {
    config::ServerConfig cfg;
    auto backend = std::make_shared<MockBackend>();
    auto catalog = std::make_shared<VoiceCatalog>();

    EXPECT_NO_THROW({
        TtsServer server(cfg, backend, catalog);
    });
}

TEST(TtsServer, InitializeWithReadyBackend) {
    config::ServerConfig cfg;
    cfg.max_queue_depth = 5;
    cfg.request_timeout_seconds = 10;

    auto backend = std::make_shared<MockBackend>();
    (void)backend->initialize("/fake/model");

    auto catalog = std::make_shared<VoiceCatalog>();
    TtsServer server(cfg, backend, catalog);

    EXPECT_TRUE(server.initialize());
    EXPECT_TRUE(server.is_ready());
}

TEST(TtsServer, InitializeFailsWithUnreadyBackend) {
    config::ServerConfig cfg;
    auto backend = std::make_shared<MockBackend>();
    // Don't initialize
    auto catalog = std::make_shared<VoiceCatalog>();

    TtsServer server(cfg, backend, catalog);
    EXPECT_FALSE(server.initialize());
    EXPECT_FALSE(server.is_ready());
}

TEST(TtsServer, InitializeFailsWithNullBackend) {
    config::ServerConfig cfg;
    auto catalog = std::make_shared<VoiceCatalog>();

    TtsServer server(cfg, nullptr, catalog);
    EXPECT_FALSE(server.initialize());
}

TEST(TtsServer, ShutdownSetsNotReady) {
    config::ServerConfig cfg;
    cfg.max_queue_depth = 5;
    cfg.request_timeout_seconds = 10;

    auto backend = std::make_shared<MockBackend>();
    (void)backend->initialize("/fake/model");

    auto catalog = std::make_shared<VoiceCatalog>();
    TtsServer server(cfg, backend, catalog);

    ASSERT_TRUE(server.initialize());
    EXPECT_TRUE(server.is_ready());

    server.shutdown();
    EXPECT_FALSE(server.is_ready());
}

TEST(TtsServer, DoubleInitializeIsHarmless) {
    config::ServerConfig cfg;
    cfg.max_queue_depth = 5;
    cfg.request_timeout_seconds = 10;

    auto backend = std::make_shared<MockBackend>();
    (void)backend->initialize("/fake/model");

    auto catalog = std::make_shared<VoiceCatalog>();
    TtsServer server(cfg, backend, catalog);

    EXPECT_TRUE(server.initialize());
    EXPECT_TRUE(server.initialize());  // Second call should be harmless
}

TEST(TtsServer, DoubleShutdownIsHarmless) {
    config::ServerConfig cfg;
    cfg.max_queue_depth = 5;
    cfg.request_timeout_seconds = 10;

    auto backend = std::make_shared<MockBackend>();
    (void)backend->initialize("/fake/model");

    auto catalog = std::make_shared<VoiceCatalog>();
    TtsServer server(cfg, backend, catalog);

    ASSERT_TRUE(server.initialize());
    server.shutdown();
    EXPECT_NO_THROW(server.shutdown());
}

// ============================================================================
// Accessors
// ============================================================================

TEST(TtsServer, AccessorsReturnCorrectComponents) {
    config::ServerConfig cfg;
    cfg.max_queue_depth = 5;
    cfg.request_timeout_seconds = 10;
    cfg.port = 8080;

    auto backend = std::make_shared<MockBackend>();
    (void)backend->initialize("/fake/model");

    auto catalog = std::make_shared<VoiceCatalog>();
    TtsServer server(cfg, backend, catalog);

    EXPECT_EQ(server.backend(), backend);
    EXPECT_EQ(server.voice_catalog(), catalog.get());
    EXPECT_EQ(server.config().port, 8080);
    EXPECT_EQ(server.pool(), nullptr);  // Not initialized yet

    ASSERT_TRUE(server.initialize());
    EXPECT_NE(server.pool(), nullptr);  // Now initialized
}

TEST(TtsServer, StartTimeIsRecent) {
    config::ServerConfig cfg;
    auto backend = std::make_shared<MockBackend>();
    auto catalog = std::make_shared<VoiceCatalog>();

    auto before = std::chrono::steady_clock::now();
    TtsServer server(cfg, backend, catalog);
    auto after = std::chrono::steady_clock::now();

    EXPECT_GE(server.start_time(), before);
    EXPECT_LE(server.start_time(), after);
}

// ============================================================================
// Rate limiters
// ============================================================================

TEST(TtsServer, RateLimitersConfiguredFromConfig) {
    config::ServerConfig cfg;
    cfg.auth_rate_limit_max = 5;
    cfg.auth_rate_limit_window = 120;
    cfg.request_rate_limit_rpm = 20;

    auto backend = std::make_shared<MockBackend>();
    auto catalog = std::make_shared<VoiceCatalog>();

    TtsServer server(cfg, backend, catalog);

    // Auth limiter configured
    EXPECT_EQ(server.auth_rate_limiter().max_failures, 5U);
    EXPECT_EQ(server.auth_rate_limiter().window_secs.count(), 120);

    // Request limiter configured
    EXPECT_EQ(server.request_rate_limiter().max_rpm, 20U);
}
