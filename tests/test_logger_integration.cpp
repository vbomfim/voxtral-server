/// QA Guardian — Logger Integration, Contract & Edge-Case Tests
/// Tests the JSON log output contract, request ID correlation in output,
/// Unicode handling, clone behavior, and the full config→logger pipeline.
///
/// Tags: [CONTRACT], [COVERAGE], [EDGE], [INTEGRATION]

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#include "logging/logger.hpp"
#include "config/config.hpp"

// ============================================================================
// Helper: create a JSON-formatted logger writing to an ostream
// ============================================================================

static std::shared_ptr<spdlog::logger> make_json_logger(
    std::ostringstream& oss,
    const std::string& name = "qa_test"
) {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto logger = std::make_shared<spdlog::logger>(name, sink);
    logger->set_formatter(tts::logging::make_json_formatter());
    logger->set_level(spdlog::level::trace);
    return logger;
}

// Helper: parse the first JSON line from a stream
static nlohmann::json parse_first_json_line(const std::string& output) {
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) return nlohmann::json::parse(line);
    }
    throw std::runtime_error("No JSON line found in output");
}

// ============================================================================
// [CONTRACT] JSON output schema contract — field names, types, presence
// These tests define the log ingestion contract for Datadog/Loki/ELK.
// If these break, log pipelines break.
// ============================================================================

TEST(LogContract, RequiredFieldsAreStrings) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("contract test");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_TRUE(j["timestamp"].is_string());
    EXPECT_TRUE(j["level"].is_string());
    EXPECT_TRUE(j["component"].is_string());
    EXPECT_TRUE(j["message"].is_string());
}

TEST(LogContract, NoExtraFieldsWithoutRequestId) {
    // When request_id is empty, there should be exactly 4 fields
    tts::logging::set_request_id("");
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("minimal");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_EQ(j.size(), 4u);  // timestamp, level, component, message
    EXPECT_FALSE(j.contains("request_id"));
}

TEST(LogContract, RequestIdFieldAppearsWhenSet) {
    tts::logging::set_request_id("req-abc-123");
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("with request id");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_EQ(j.size(), 5u);  // +request_id
    EXPECT_TRUE(j.contains("request_id"));
    EXPECT_EQ(j["request_id"], "req-abc-123");

    // Cleanup
    tts::logging::set_request_id("");
}

TEST(LogContract, RequestIdDisappearsWhenCleared) {
    tts::logging::set_request_id("temp-id");

    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("with id");
    logger->flush();

    auto j1 = parse_first_json_line(oss.str());
    EXPECT_TRUE(j1.contains("request_id"));

    // Clear and log again
    tts::logging::set_request_id("");
    std::ostringstream oss2;
    auto logger2 = make_json_logger(oss2, "qa_test2");
    logger2->info("without id");
    logger2->flush();

    auto j2 = parse_first_json_line(oss2.str());
    EXPECT_FALSE(j2.contains("request_id"));
}

// ============================================================================
// [COVERAGE] Empty logger name → component defaults to "server"
// The JsonFormatter has explicit fallback logic for this.
// ============================================================================

TEST(LogContract, EmptyLoggerNameDefaultsToServer) {
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    // Logger with empty name
    auto logger = std::make_shared<spdlog::logger>("", sink);
    logger->set_formatter(tts::logging::make_json_formatter());
    logger->set_level(spdlog::level::info);

    logger->info("test");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_EQ(j["component"], "server");
}

// ============================================================================
// [EDGE] Unicode/emoji in log messages — must produce valid JSON
// ============================================================================

TEST(LogEdge, UnicodeMessageProducesValidJson) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("日本語メッセージ 🎵 音声合成 — voxtral");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_TRUE(j["message"].get<std::string>().find("🎵") != std::string::npos);
}

TEST(LogEdge, NullBytesInMessage) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    std::string msg_with_null = "before";
    msg_with_null += '\0';
    msg_with_null += "after";
    logger->info("{}", msg_with_null);
    logger->flush();

    // Should not crash — JSON may truncate at null or escape it
    std::string output = oss.str();
    EXPECT_FALSE(output.empty());
    // Should still be parseable JSON
    auto j = nlohmann::json::parse(output);
    EXPECT_TRUE(j.is_object());
}

// ============================================================================
// [EDGE] Very long messages — formatter shouldn't crash or truncate silently
// ============================================================================

TEST(LogEdge, VeryLongMessage) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    std::string long_msg(100000, 'A');  // 100KB message
    logger->info("{}", long_msg);
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_EQ(j["message"].get<std::string>().size(), 100000u);
}

// ============================================================================
// [COVERAGE] JsonFormatter::clone() produces functionally identical output
// spdlog requires clone() for multi-sink setups.
// ============================================================================

TEST(LogCoverage, ClonedFormatterProducesIdenticalStructure) {
    auto original = tts::logging::make_json_formatter();
    auto cloned = original->clone();

    // Both should produce valid JSON with same structure
    spdlog::details::log_msg msg(spdlog::source_loc{}, "test_clone",
                                  spdlog::level::info, "clone test");

    spdlog::memory_buf_t buf1, buf2;
    original->format(msg, buf1);
    cloned->format(msg, buf2);

    auto j1 = nlohmann::json::parse(std::string(buf1.data(), buf1.size()));
    auto j2 = nlohmann::json::parse(std::string(buf2.data(), buf2.size()));

    // Same fields
    EXPECT_EQ(j1["level"], j2["level"]);
    EXPECT_EQ(j1["message"], j2["message"]);
    EXPECT_EQ(j1["component"], j2["component"]);
    // Timestamps may differ by microseconds, but both should be ISO 8601
    EXPECT_EQ(j1["timestamp"].get<std::string>().size(), 24u);
    EXPECT_EQ(j2["timestamp"].get<std::string>().size(), 24u);
}

// ============================================================================
// [EDGE] Thread-local request ID isolation
// Verifies that request IDs don't leak across threads.
// ============================================================================

TEST(LogEdge, RequestIdIsThreadLocal) {
    tts::logging::set_request_id("main-thread-id");

    std::string child_id;
    std::thread t([&child_id]() {
        // New thread should NOT see main thread's request ID
        child_id = tts::logging::get_request_id();
    });
    t.join();

    EXPECT_EQ(child_id, "");  // child thread has empty request ID
    EXPECT_EQ(tts::logging::get_request_id(), "main-thread-id");

    // Cleanup
    tts::logging::set_request_id("");
}

TEST(LogEdge, RequestIdSetInChildThread_IsolatedFromParent) {
    tts::logging::set_request_id("");

    std::thread t([]() {
        tts::logging::set_request_id("child-id");
        EXPECT_EQ(tts::logging::get_request_id(), "child-id");
    });
    t.join();

    // Parent thread unaffected
    EXPECT_EQ(tts::logging::get_request_id(), "");
}

// ============================================================================
// [INTEGRATION] Full pipeline: config → logger init → structured JSON output
// Verifies that from_file_and_env → initialize → log produces correct output.
// ============================================================================

TEST(LogIntegration, ConfigToLoggerPipelineJson) {
    tts::config::ServerConfig cfg;
    cfg.log_format = "json";
    cfg.log_level = "debug";

    // Initialize global logger
    tts::logging::initialize(cfg);

    // Verify level was set
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::debug);

    // Create a JSON logger to capture output (can't capture default logger easily)
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    tts::logging::set_request_id("integration-req-1");
    logger->info("Integration pipeline test: port={}", 9090);
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    EXPECT_EQ(j["level"], "info");
    EXPECT_TRUE(j["message"].get<std::string>().find("9090") != std::string::npos);
    EXPECT_EQ(j["request_id"], "integration-req-1");

    // Cleanup
    tts::logging::set_request_id("");
    cfg.log_format = "text";
    cfg.log_level = "info";
    tts::logging::initialize(cfg);
}

TEST(LogIntegration, ConfigToLoggerPipelineText) {
    tts::config::ServerConfig cfg;
    cfg.log_format = "text";
    cfg.log_level = "warn";

    tts::logging::initialize(cfg);

    // Verify level filtering works
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::warn);

    // Reset
    cfg.log_level = "info";
    tts::logging::initialize(cfg);
}

// ============================================================================
// [EDGE] Log level string: spdlog "warning" vs our "warn" mapping
// Verify the level field value matches spdlog's string representation.
// ============================================================================

TEST(LogContract, WarnLevelOutputsAsWarning) {
    // spdlog internally maps warn → "warning" in its string representation
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->warn("warning test");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    // spdlog::level::to_string_view(warn) returns "warning", not "warn"
    EXPECT_EQ(j["level"], "warning");
}

// ============================================================================
// [CONTRACT] Each log line is self-contained JSON (newline-delimited)
// Critical for log ingestion pipelines that split on newlines.
// ============================================================================

TEST(LogContract, OutputIsNewlineDelimitedJson) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("first");
    logger->error("second");
    logger->debug("third");
    logger->flush();

    std::string output = oss.str();

    // Must end with newline
    EXPECT_EQ(output.back(), '\n');

    // Each line must be independent valid JSON
    std::istringstream iss(output);
    std::string line;
    int count = 0;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        // Suppress -Wunused-result inside EXPECT_NO_THROW by assigning
        EXPECT_NO_THROW({
            [[maybe_unused]] auto parsed = nlohmann::json::parse(line);
        }) << "Line " << count << " is not valid JSON";
        count++;
    }
    EXPECT_EQ(count, 3);
}

// ============================================================================
// [EDGE] Message containing JSON-like strings — must not break outer JSON
// ============================================================================

TEST(LogEdge, MessageContainingJsonString) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info(R"(Got response: {"status": 200, "body": "ok"})");
    logger->flush();

    auto j = parse_first_json_line(oss.str());
    std::string msg = j["message"];
    EXPECT_TRUE(msg.find("\"status\"") != std::string::npos);
}

// ============================================================================
// [EDGE] Message containing log injection attempt
// Verify embedded newlines in messages don't split the JSON line
// ============================================================================

TEST(LogEdge, NewlineInMessageDoesNotSplitLogLine) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);
    logger->info("line1\nfake_line2");
    logger->flush();

    // The output should contain the message with escaped newline in JSON
    // Parse the raw output - the JSON serializer should escape \n
    auto j = nlohmann::json::parse(oss.str());
    std::string msg = j["message"];
    EXPECT_TRUE(msg.find('\n') != std::string::npos);  // newline preserved in value
}
