/// @file test_http_adapter.cpp
/// @brief Unit tests for the HTTP adapter layer.
///
/// Tests the adapter that bridges cpp-httplib ↔ abstract HttpRequest/HttpResponse.
/// Verifies route wiring, middleware dispatch, and request/response translation.
/// Uses a real httplib::Server in a background thread for integration-level validation.

#include <gtest/gtest.h>

#include "server/http_adapter.hpp"
#include "tts/mock_backend.hpp"
#include "tts/server.hpp"
#include "tts/voices.hpp"
#include "config/config.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using json = nlohmann::json;
using namespace tts;
using namespace tts::server;

// ============================================================================
// Test fixture: starts HttpAdapter in background thread, uses httplib::Client
// ============================================================================

class HttpAdapterTest : public ::testing::Test {
protected:
    static constexpr int kTestPort = 19090;  // Avoid conflict with production port

    void SetUp() override {
        backend_ = std::make_shared<MockBackend>();
        (void)backend_->initialize("/fake/model");

        catalog_ = std::make_shared<VoiceCatalog>();

        cfg_.port = kTestPort;
        cfg_.host = "127.0.0.1";
        cfg_.max_queue_depth = 10;
        cfg_.request_timeout_seconds = 30;
        cfg_.require_auth = false;

        tts_server_ = std::make_unique<TtsServer>(cfg_, backend_, catalog_);
        ASSERT_TRUE(tts_server_->initialize());

        adapter_ = std::make_unique<HttpAdapter>(*tts_server_);

        // Start listener in background thread
        server_thread_ = std::thread([this]() {
            (void)adapter_->start();
        });

        // Wait for the server to be ready
        wait_for_server();
    }

    void TearDown() override {
        if (adapter_) {
            adapter_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        if (tts_server_) {
            tts_server_->shutdown();
        }
    }

    /// Create an httplib client pointing at the test server.
    httplib::Client make_client() {
        httplib::Client cli("127.0.0.1", kTestPort);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        return cli;
    }

private:
    void wait_for_server() {
        // Poll until the server accepts connections (max 3 seconds)
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            httplib::Client cli("127.0.0.1", kTestPort);
            cli.set_connection_timeout(1, 0);
            auto res = cli.Get("/health");
            if (res && res->status == 200) return;
        }
        FAIL() << "HttpAdapter did not start within 3 seconds";
    }

protected:
    std::shared_ptr<MockBackend> backend_;
    std::shared_ptr<VoiceCatalog> catalog_;
    config::ServerConfig cfg_;
    std::unique_ptr<TtsServer> tts_server_;
    std::unique_ptr<HttpAdapter> adapter_;
    std::thread server_thread_;
};

// ============================================================================
// GET /health
// ============================================================================

TEST_F(HttpAdapterTest, HealthReturns200Json) {
    auto cli = make_client();
    auto res = cli.Get("/health");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    auto body = json::parse(res->body);
    EXPECT_EQ(body["status"], "ok");
    EXPECT_EQ(body["version"], "0.1.0");
    EXPECT_TRUE(body.contains("uptime_seconds"));
}

TEST_F(HttpAdapterTest, HealthHasSecurityHeaders) {
    auto cli = make_client();
    auto res = cli.Get("/health");

    ASSERT_TRUE(res);
    EXPECT_TRUE(res->has_header("X-Content-Type-Options"));
    EXPECT_EQ(res->get_header_value("X-Content-Type-Options"), "nosniff");
    EXPECT_TRUE(res->has_header("Cache-Control"));
    EXPECT_TRUE(res->has_header("X-Request-Id"));
}

// ============================================================================
// GET /ready
// ============================================================================

TEST_F(HttpAdapterTest, ReadyReturns200WhenReady) {
    auto cli = make_client();
    auto res = cli.Get("/ready");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    auto body = json::parse(res->body);
    EXPECT_TRUE(body["ready"].get<bool>());
}

// ============================================================================
// GET /metrics
// ============================================================================

TEST_F(HttpAdapterTest, MetricsReturnsPrometheusText) {
    auto cli = make_client();
    auto res = cli.Get("/metrics");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("# HELP"), std::string::npos);
    EXPECT_NE(res->body.find("# TYPE"), std::string::npos);
}

// ============================================================================
// GET /v1/voices
// ============================================================================

TEST_F(HttpAdapterTest, VoicesReturns20Voices) {
    auto cli = make_client();
    auto res = cli.Get("/v1/voices");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    auto body = json::parse(res->body);
    EXPECT_EQ(body["voices"].size(), 20U);
}

TEST_F(HttpAdapterTest, VoicesHasSecurityHeaders) {
    auto cli = make_client();
    auto res = cli.Get("/v1/voices");

    ASSERT_TRUE(res);
    EXPECT_TRUE(res->has_header("X-Request-Id"));
}

// ============================================================================
// POST /v1/audio/speech — happy path
// ============================================================================

TEST_F(HttpAdapterTest, SpeechReturnsWavAudio) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello world";
    body["voice"] = "neutral_female";

    auto cli = make_client();
    auto res = cli.Post("/v1/audio/speech", body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    // Check RIFF header
    ASSERT_GE(res->body.size(), 4U);
    EXPECT_EQ(res->body[0], 'R');
    EXPECT_EQ(res->body[1], 'I');
    EXPECT_EQ(res->body[2], 'F');
    EXPECT_EQ(res->body[3], 'F');
}

TEST_F(HttpAdapterTest, SpeechHasSecurityHeaders) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    auto cli = make_client();
    auto res = cli.Post("/v1/audio/speech", body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_TRUE(res->has_header("X-Request-Id"));
    EXPECT_TRUE(res->has_header("X-Content-Type-Options"));
}

// ============================================================================
// POST /v1/audio/speech — validation errors pass through
// ============================================================================

TEST_F(HttpAdapterTest, SpeechRejectsMissingModel) {
    json body;
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    auto cli = make_client();
    auto res = cli.Post("/v1/audio/speech", body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

TEST_F(HttpAdapterTest, SpeechRejectsInvalidVoice) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "nonexistent_voice_xyz";

    auto cli = make_client();
    auto res = cli.Post("/v1/audio/speech", body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);

    auto rbody = json::parse(res->body);
    EXPECT_EQ(rbody["error"]["code"], "invalid_voice");
}

// ============================================================================
// 404 for unknown routes
// ============================================================================

TEST_F(HttpAdapterTest, UnknownRouteReturns404) {
    auto cli = make_client();
    auto res = cli.Get("/nonexistent/path");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

// ============================================================================
// Auth middleware (when enabled)
// ============================================================================

class HttpAdapterAuthTest : public ::testing::Test {
protected:
    static constexpr int kTestPort = 19091;

    void SetUp() override {
        backend_ = std::make_shared<MockBackend>();
        (void)backend_->initialize("/fake/model");

        catalog_ = std::make_shared<VoiceCatalog>();

        cfg_.port = kTestPort;
        cfg_.host = "127.0.0.1";
        cfg_.max_queue_depth = 10;
        cfg_.request_timeout_seconds = 30;
        cfg_.require_auth = true;
        cfg_.api_key = "test-secret-key";
        cfg_.request_rate_limit_rpm = 1000;  // High limit for tests

        tts_server_ = std::make_unique<TtsServer>(cfg_, backend_, catalog_);
        ASSERT_TRUE(tts_server_->initialize());

        adapter_ = std::make_unique<HttpAdapter>(*tts_server_);

        server_thread_ = std::thread([this]() {
            (void)adapter_->start();
        });

        wait_for_server();
    }

    void TearDown() override {
        if (adapter_) adapter_->stop();
        if (server_thread_.joinable()) server_thread_.join();
        if (tts_server_) tts_server_->shutdown();
    }

    httplib::Client make_client() {
        httplib::Client cli("127.0.0.1", kTestPort);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        return cli;
    }

private:
    void wait_for_server() {
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            httplib::Client cli("127.0.0.1", kTestPort);
            cli.set_connection_timeout(1, 0);
            auto res = cli.Get("/health");
            if (res && res->status == 200) return;
        }
        FAIL() << "HttpAdapter did not start within 3 seconds";
    }

protected:
    std::shared_ptr<MockBackend> backend_;
    std::shared_ptr<VoiceCatalog> catalog_;
    config::ServerConfig cfg_;
    std::unique_ptr<TtsServer> tts_server_;
    std::unique_ptr<HttpAdapter> adapter_;
    std::thread server_thread_;
};

TEST_F(HttpAdapterAuthTest, HealthSkipsAuth) {
    // /health is internal — should work without auth
    auto cli = make_client();
    auto res = cli.Get("/health");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST_F(HttpAdapterAuthTest, VoicesRequiresAuth) {
    auto cli = make_client();
    // No Authorization header
    auto res = cli.Get("/v1/voices");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}

TEST_F(HttpAdapterAuthTest, VoicesWithValidAuth) {
    auto cli = make_client();
    httplib::Headers headers = {
        {"Authorization", "Bearer test-secret-key"}
    };
    auto res = cli.Get("/v1/voices", headers);

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST_F(HttpAdapterAuthTest, SpeechWithValidAuth) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    auto cli = make_client();
    httplib::Headers headers = {
        {"Authorization", "Bearer test-secret-key"}
    };
    auto res = cli.Post("/v1/audio/speech", headers, body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST_F(HttpAdapterAuthTest, SpeechRejectsInvalidAuth) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    auto cli = make_client();
    httplib::Headers headers = {
        {"Authorization", "Bearer wrong-key"}
    };
    auto res = cli.Post("/v1/audio/speech", headers, body.dump(), "application/json");

    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);
}
