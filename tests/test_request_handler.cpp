/// @file test_request_handler.cpp
/// @brief Developer unit tests for HTTP request handlers.
///
/// Tests route handlers using the abstract HttpRequest/HttpResponse types.
/// No real HTTP server is started — handlers are pure functions.

#include <gtest/gtest.h>

#include "server/request_handler.hpp"
#include "tts/mock_backend.hpp"
#include "tts/version.hpp"
#include "tts/voices.hpp"
#include "config/config.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>

using json = nlohmann::json;
using namespace tts;
using namespace tts::server;

// ============================================================================
// Test fixtures
// ============================================================================

class RequestHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = std::make_shared<MockBackend>();
        (void)backend_->initialize("/fake/model");

        catalog_ = std::make_unique<VoiceCatalog>();

        config_.max_queue_depth = 10;
        config_.request_timeout_seconds = 30;
        config_.max_input_chars = 4096;

        pool_ = std::make_unique<InferencePool>(
            backend_, config_.max_queue_depth,
            config_.request_timeout_seconds);

        deps_.backend = backend_;
        deps_.voice_catalog = catalog_.get();
        deps_.pool = pool_.get();
        deps_.config = &config_;
        deps_.start_time = std::chrono::steady_clock::now();
    }

    void TearDown() override {
        if (pool_) pool_->shutdown();
    }

    /// Build a valid speech request for happy-path tests.
    HttpRequest make_speech_request(const std::string& input = "Hello world",
                                     const std::string& voice = "neutral_female") {
        json body;
        body["model"] = "voxtral-4b";
        body["input"] = input;
        body["voice"] = voice;

        HttpRequest req;
        req.method = "POST";
        req.path = "/v1/audio/speech";
        req.headers["content-type"] = "application/json";
        req.body = body.dump();
        return req;
    }

    std::shared_ptr<MockBackend> backend_;
    std::unique_ptr<VoiceCatalog> catalog_;
    std::unique_ptr<InferencePool> pool_;
    config::ServerConfig config_;
    HandlerDeps deps_;
};

// ============================================================================
// GET /health
// ============================================================================

TEST_F(RequestHandlerTest, HealthReturns200) {
    auto resp = handle_health(deps_);
    EXPECT_EQ(resp.status, 200);

    auto body = json::parse(resp.body);
    EXPECT_EQ(body["status"], "ok");
    EXPECT_TRUE(body.contains("version"));
    EXPECT_TRUE(body.contains("uptime_seconds"));
    EXPECT_GE(body["uptime_seconds"].get<int>(), 0);
}

TEST_F(RequestHandlerTest, HealthContainsVersion) {
    auto resp = handle_health(deps_);
    auto body = json::parse(resp.body);
    EXPECT_EQ(body["version"], std::string(tts::VERSION));
}

TEST_F(RequestHandlerTest, HealthContentTypeIsJson) {
    auto resp = handle_health(deps_);
    EXPECT_EQ(resp.headers.at("Content-Type"), "application/json");
}

// ============================================================================
// GET /ready
// ============================================================================

TEST_F(RequestHandlerTest, ReadyReturns200WhenReady) {
    auto resp = handle_ready(deps_);
    EXPECT_EQ(resp.status, 200);

    auto body = json::parse(resp.body);
    EXPECT_TRUE(body["ready"].get<bool>());
    EXPECT_TRUE(body["model_loaded"].get<bool>());
    EXPECT_EQ(body["queue_depth"].get<int>(), 0);
    EXPECT_EQ(body["queue_capacity"].get<int>(), 10);
}

TEST_F(RequestHandlerTest, ReadyReturns503WhenBackendNotReady) {
    auto unready_backend = std::make_shared<MockBackend>();
    // Don't initialize — not ready
    HandlerDeps deps;
    deps.backend = unready_backend;
    deps.pool = pool_.get();
    deps.config = &config_;

    auto resp = handle_ready(deps);
    EXPECT_EQ(resp.status, 503);

    auto body = json::parse(resp.body);
    EXPECT_FALSE(body["ready"].get<bool>());
    EXPECT_FALSE(body["model_loaded"].get<bool>());
}

TEST_F(RequestHandlerTest, ReadyReturns503WhenPoolIsNull) {
    HandlerDeps deps;
    deps.backend = backend_;
    deps.pool = nullptr;
    deps.config = &config_;

    auto resp = handle_ready(deps);
    EXPECT_EQ(resp.status, 503);
}

// ============================================================================
// GET /v1/voices
// ============================================================================

TEST_F(RequestHandlerTest, VoicesReturns200WithAllVoices) {
    auto resp = handle_voices(deps_);
    EXPECT_EQ(resp.status, 200);

    auto body = json::parse(resp.body);
    ASSERT_TRUE(body.contains("voices"));
    EXPECT_EQ(body["voices"].size(), 20U);
}

TEST_F(RequestHandlerTest, VoicesContainsExpectedFields) {
    auto resp = handle_voices(deps_);
    auto body = json::parse(resp.body);

    auto& first = body["voices"][0];
    EXPECT_TRUE(first.contains("id"));
    EXPECT_TRUE(first.contains("name"));
    EXPECT_TRUE(first.contains("language"));
}

TEST_F(RequestHandlerTest, VoicesContentTypeIsJson) {
    auto resp = handle_voices(deps_);
    EXPECT_EQ(resp.headers.at("Content-Type"), "application/json");
}

// ============================================================================
// GET /metrics
// ============================================================================

TEST_F(RequestHandlerTest, MetricsReturns200) {
    auto resp = handle_metrics();
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.headers.at("Content-Type"),
              "text/plain; version=0.0.4; charset=utf-8");
    EXPECT_FALSE(resp.body.empty());
}

TEST_F(RequestHandlerTest, MetricsContainsPrometheusFormat) {
    auto resp = handle_metrics();
    EXPECT_NE(resp.body.find("# HELP"), std::string::npos);
    EXPECT_NE(resp.body.find("# TYPE"), std::string::npos);
}

// ============================================================================
// POST /v1/audio/speech — happy path
// ============================================================================

TEST_F(RequestHandlerTest, SpeechHappyPathReturns200) {
    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 200);
    EXPECT_TRUE(resp.is_binary());
    EXPECT_EQ(resp.headers.at("Content-Type"), "audio/wav");
    EXPECT_FALSE(resp.binary_body.empty());
}

TEST_F(RequestHandlerTest, SpeechReturnsWavBytes) {
    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    // Should start with RIFF header
    ASSERT_GE(resp.binary_body.size(), 4U);
    EXPECT_EQ(resp.binary_body[0], 'R');
    EXPECT_EQ(resp.binary_body[1], 'I');
    EXPECT_EQ(resp.binary_body[2], 'F');
    EXPECT_EQ(resp.binary_body[3], 'F');
}

TEST_F(RequestHandlerTest, SpeechPassesCorrectTextToBackend) {
    auto req = make_speech_request("Test synthesis text");
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(backend_->last_request.text, "Test synthesis text");
}

TEST_F(RequestHandlerTest, SpeechPassesCorrectVoiceToBackend) {
    auto req = make_speech_request("Hello", "casual_male");
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(backend_->last_request.voice, "casual_male");
}

TEST_F(RequestHandlerTest, SpeechWithOptionalSpeed) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";
    body["speed"] = 1.5;

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 200);
    EXPECT_FLOAT_EQ(backend_->last_request.speed, 1.5F);
}

TEST_F(RequestHandlerTest, SpeechContentLengthHeader) {
    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.headers.at("Content-Length"),
              std::to_string(resp.binary_body.size()));
}

// ============================================================================
// POST /v1/audio/speech — validation errors
// ============================================================================

TEST_F(RequestHandlerTest, SpeechRejectsWrongContentType) {
    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "text/plain";
    req.body = "{}";

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto body = json::parse(resp.body);
    EXPECT_EQ(body["error"]["code"], "invalid_content_type");
}

TEST_F(RequestHandlerTest, SpeechRejectsInvalidJson) {
    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = "not valid json{{{";

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);
}

TEST_F(RequestHandlerTest, SpeechRejectsMissingModel) {
    json body;
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["param"], "model");
}

TEST_F(RequestHandlerTest, SpeechRejectsInvalidModel) {
    json body;
    body["model"] = "gpt-4o";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "invalid_model");
}

TEST_F(RequestHandlerTest, SpeechRejectsMissingInput) {
    json body;
    body["model"] = "voxtral-4b";
    body["voice"] = "neutral_female";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["param"], "input");
}

TEST_F(RequestHandlerTest, SpeechRejectsWhitespaceOnlyInput) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "   \n\t  ";
    body["voice"] = "neutral_female";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);
}

TEST_F(RequestHandlerTest, SpeechRejectsInputTooLong) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = std::string(4097, 'x');
    body["voice"] = "neutral_female";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "string_above_max_length");
}

TEST_F(RequestHandlerTest, SpeechRejectsInvalidVoice) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "nonexistent_voice";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "invalid_voice");
}

TEST_F(RequestHandlerTest, SpeechRejectsInvalidFormat) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";
    body["response_format"] = "mp3";

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "invalid_format");
}

TEST_F(RequestHandlerTest, SpeechRejectsSpeedOutOfRange) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello";
    body["voice"] = "neutral_female";
    body["speed"] = 5.0;

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "speed_out_of_range");
}

TEST_F(RequestHandlerTest, SpeechRejectsOversizedBody) {
    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = std::string(1048577, 'x');  // > 1MB

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 413);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["code"], "payload_too_large");
}

// ============================================================================
// POST /v1/audio/speech — server errors
// ============================================================================

TEST_F(RequestHandlerTest, SpeechReturns503WhenPoolNotAccepting) {
    pool_->shutdown();

    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 503);
    auto body = json::parse(resp.body);
    EXPECT_EQ(body["error"]["code"], "model_not_ready");
}

TEST_F(RequestHandlerTest, SpeechReturns503WhenQueueFull) {
    // Create a pool with depth 1 and slow backend
    backend_->latency_ms = 500;
    auto small_pool = std::make_unique<InferencePool>(backend_, 1, 30);
    deps_.pool = small_pool.get();

    // Fill the queue: submit one that blocks the worker
    InferenceJob blocking_job;
    blocking_job.request.text = "blocking";
    blocking_job.on_success = [](SynthesisResult) {};
    blocking_job.on_error = [](std::string) {};
    ASSERT_TRUE(small_pool->submit(std::move(blocking_job)));

    // Give worker time to pick up job
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Fill the 1-slot queue
    InferenceJob fill_job;
    fill_job.request.text = "filler";
    fill_job.on_success = [](SynthesisResult) {};
    fill_job.on_error = [](std::string) {};
    small_pool->submit(std::move(fill_job));

    // Now the speech handler should get queue_full
    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 503);
    auto body = json::parse(resp.body);
    EXPECT_EQ(body["error"]["code"], "queue_full");

    small_pool->shutdown();
}

TEST_F(RequestHandlerTest, SpeechReturns500OnInferenceFailure) {
    backend_->should_fail = true;
    backend_->fail_message = "GPU out of memory";

    auto req = make_speech_request();
    auto resp = handle_speech(req, deps_);

    EXPECT_EQ(resp.status, 500);
    auto body = json::parse(resp.body);
    EXPECT_EQ(body["error"]["code"], "inference_failed");
    // Must NOT leak internal error details
    EXPECT_EQ(body["error"]["message"].get<std::string>().find("GPU"),
              std::string::npos);
}

// ============================================================================
// OpenAI error format compliance
// ============================================================================

TEST_F(RequestHandlerTest, ErrorFormatHasRequiredFields) {
    auto resp = make_error_response(400, "invalid_request_error",
                                     "missing_input", "Missing input", "input");
    auto body = json::parse(resp.body);

    ASSERT_TRUE(body.contains("error"));
    EXPECT_TRUE(body["error"].contains("message"));
    EXPECT_TRUE(body["error"].contains("type"));
    EXPECT_TRUE(body["error"].contains("param"));
    EXPECT_TRUE(body["error"].contains("code"));
}

TEST_F(RequestHandlerTest, ErrorParamIsNullWhenEmpty) {
    auto resp = make_error_response(400, "invalid_request_error",
                                     "invalid_content_type", "Bad CT");
    auto body = json::parse(resp.body);
    EXPECT_TRUE(body["error"]["param"].is_null());
}

TEST_F(RequestHandlerTest, ErrorResponseContentType) {
    auto resp = make_error_response(400, "invalid_request_error",
                                     "missing_input", "test");
    EXPECT_EQ(resp.headers.at("Content-Type"), "application/json");
}

// ============================================================================
// Security headers
// ============================================================================

TEST_F(RequestHandlerTest, SecurityHeadersApplied) {
    HttpResponse resp;
    apply_security_headers(resp, "test-req-id-123");

    EXPECT_EQ(resp.headers.at("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(resp.headers.at("Cache-Control"), "no-store");
    EXPECT_EQ(resp.headers.at("X-Request-Id"), "test-req-id-123");
}

TEST_F(RequestHandlerTest, RequestIdGeneration) {
    auto id1 = generate_request_id();
    auto id2 = generate_request_id();

    // UUID format: 8-4-4-4-12 = 36 chars
    EXPECT_EQ(id1.size(), 36U);
    EXPECT_EQ(id2.size(), 36U);

    // Must be unique
    EXPECT_NE(id1, id2);

    // Check dashes at correct positions
    EXPECT_EQ(id1[8], '-');
    EXPECT_EQ(id1[13], '-');
    EXPECT_EQ(id1[18], '-');
    EXPECT_EQ(id1[23], '-');
}

// ============================================================================
// POST /v1/audio/speech — missing voice field
// ============================================================================

TEST_F(RequestHandlerTest, SpeechRejectsMissingVoice) {
    json body;
    body["model"] = "voxtral-4b";
    body["input"] = "Hello world";
    // No "voice" field

    HttpRequest req;
    req.method = "POST";
    req.path = "/v1/audio/speech";
    req.headers["content-type"] = "application/json";
    req.body = body.dump();

    auto resp = handle_speech(req, deps_);
    EXPECT_EQ(resp.status, 400);

    auto rbody = json::parse(resp.body);
    EXPECT_EQ(rbody["error"]["param"], "voice");
}
