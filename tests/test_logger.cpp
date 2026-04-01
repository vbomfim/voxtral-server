#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

// Include logger.cpp implementation details for JsonFormatter
// We test through the public API (tts::logging::initialize) and also
// test the JSON formatter directly via spdlog ostream sink.
#include "logging/logger.hpp"
#include "config/config.hpp"

// ============================================================================
// Helper: create a logger that writes to the given stream with JSON format.
// We re-create the JsonFormatter by initializing a config with json format
// and capturing output via an ostream sink.
// ============================================================================

// We need to access JsonFormatter directly. Since it's in the .cpp file,
// we'll test it through the public initialize() + spdlog API.

// Helper to create a JSON-formatted logger writing to an ostream
static std::shared_ptr<spdlog::logger> make_json_logger(std::ostringstream& oss) {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto logger = std::make_shared<spdlog::logger>("test_json", sink);
    logger->set_formatter(tts::logging::make_json_formatter());
    logger->set_level(spdlog::level::trace);
    return logger;
}

// ============================================================================
// JSON formatter output tests
// ============================================================================

TEST(JsonFormatter, OutputIsValidJson) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("Hello world");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    EXPECT_TRUE(j.is_object());
}

TEST(JsonFormatter, ContainsRequiredFields) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("Test message");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_TRUE(j.contains("level"));
    EXPECT_TRUE(j.contains("message"));
}

TEST(JsonFormatter, LevelFieldMatchesLogLevel) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->debug("debug msg");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    EXPECT_EQ(j["level"], "debug");
}

TEST(JsonFormatter, MessageFieldMatchesInput) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->warn("something went wrong");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    EXPECT_EQ(j["message"], "something went wrong");
}

TEST(JsonFormatter, TimestampIsISO8601) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("timestamp test");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    std::string ts = j["timestamp"];
    // Should match pattern: YYYY-MM-DDTHH:MM:SS.mmmZ (24 chars)
    EXPECT_EQ(ts.size(), 24u);
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[7], '-');
    EXPECT_EQ(ts[10], 'T');
    EXPECT_EQ(ts[13], ':');
    EXPECT_EQ(ts[16], ':');
    EXPECT_EQ(ts[19], '.');
    EXPECT_EQ(ts[23], 'Z');
}

TEST(JsonFormatter, EachLogCallProducesOneLine) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("line one");
    logger->warn("line two");
    logger->error("line three");
    logger->flush();

    std::string output = oss.str();
    std::istringstream iss(output);
    std::string line;
    int count = 0;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        auto j = nlohmann::json::parse(line);
        EXPECT_TRUE(j.is_object());
        count++;
    }
    EXPECT_EQ(count, 3);
}

TEST(JsonFormatter, AllLogLevelsProduceValidJson) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->trace("trace");
    logger->debug("debug");
    logger->info("info");
    logger->warn("warn");
    logger->error("error");
    logger->critical("critical");
    logger->flush();

    std::istringstream iss(oss.str());
    std::string line;
    std::vector<std::string> levels;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        auto j = nlohmann::json::parse(line);
        levels.push_back(j["level"]);
    }
    ASSERT_EQ(levels.size(), 6u);
    EXPECT_EQ(levels[0], "trace");
    EXPECT_EQ(levels[1], "debug");
    EXPECT_EQ(levels[2], "info");
    EXPECT_EQ(levels[3], "warning");  // spdlog maps "warn" → "warning"
    EXPECT_EQ(levels[4], "error");
    EXPECT_EQ(levels[5], "critical");
}

TEST(JsonFormatter, SpecialCharactersEscaped) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("quote: \" backslash: \\ newline: \n tab: \t");
    logger->flush();

    // Should parse without error — nlohmann::json handles escaping
    auto j = nlohmann::json::parse(oss.str());
    EXPECT_TRUE(j["message"].get<std::string>().find('"') != std::string::npos);
}

TEST(JsonFormatter, FormattedMessageProducesValidJson) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("TTS started: voice={} chars={} workers={}", "neutral", 4096, 2);
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    std::string msg = j["message"];
    EXPECT_TRUE(msg.find("neutral") != std::string::npos);
    EXPECT_TRUE(msg.find("4096") != std::string::npos);
    EXPECT_TRUE(msg.find("2") != std::string::npos);
}

TEST(JsonFormatter, ComponentFieldPresent) {
    std::ostringstream oss;
    auto logger = make_json_logger(oss);

    logger->info("test");
    logger->flush();

    auto j = nlohmann::json::parse(oss.str());
    EXPECT_TRUE(j.contains("component"));
    EXPECT_EQ(j["component"], "test_json");  // logger name
}

// ============================================================================
// Log level parsing tests
// ============================================================================

TEST(LogLevel, ParsesAllLevels) {
    using tts::logging::parse_log_level;
    EXPECT_EQ(parse_log_level("trace"),    spdlog::level::trace);
    EXPECT_EQ(parse_log_level("debug"),    spdlog::level::debug);
    EXPECT_EQ(parse_log_level("info"),     spdlog::level::info);
    EXPECT_EQ(parse_log_level("warn"),     spdlog::level::warn);
    EXPECT_EQ(parse_log_level("error"),    spdlog::level::err);
    EXPECT_EQ(parse_log_level("critical"), spdlog::level::critical);
}

TEST(LogLevel, UnknownLevelDefaultsToInfo) {
    using tts::logging::parse_log_level;
    EXPECT_EQ(parse_log_level("unknown"), spdlog::level::info);
    EXPECT_EQ(parse_log_level(""),        spdlog::level::info);
    EXPECT_EQ(parse_log_level("verbose"), spdlog::level::info);
}

// ============================================================================
// Request ID tests
// ============================================================================

TEST(RequestId, SetAndGet) {
    tts::logging::set_request_id("req-12345");
    EXPECT_EQ(tts::logging::get_request_id(), "req-12345");

    // Clean up
    tts::logging::set_request_id("");
    EXPECT_EQ(tts::logging::get_request_id(), "");
}

TEST(RequestId, DefaultEmpty) {
    tts::logging::set_request_id("");
    EXPECT_TRUE(tts::logging::get_request_id().empty());
}

// ============================================================================
// Initialize function tests
// ============================================================================

TEST(LoggerInit, TextFormat_NoThrow) {
    tts::config::ServerConfig cfg;
    cfg.log_format = "text";
    cfg.log_level = "info";
    EXPECT_NO_THROW(tts::logging::initialize(cfg));
}

TEST(LoggerInit, JsonFormat_NoThrow) {
    tts::config::ServerConfig cfg;
    cfg.log_format = "json";
    cfg.log_level = "debug";
    EXPECT_NO_THROW(tts::logging::initialize(cfg));
}

TEST(LoggerInit, SetsLogLevel) {
    tts::config::ServerConfig cfg;
    cfg.log_format = "text";
    cfg.log_level = "error";
    tts::logging::initialize(cfg);
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::err);

    // Reset
    cfg.log_level = "info";
    tts::logging::initialize(cfg);
}

// ============================================================================
// Logger re-initialization: JSON → text resets formatter
// ============================================================================

TEST(LoggerInit, JsonThenText_ResetsToTextFormat) {
    auto original_logger = spdlog::default_logger();

    // Replace default logger with one that captures output
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("", sink));

    // Init as JSON
    tts::config::ServerConfig cfg;
    cfg.log_format = "json";
    cfg.log_level = "info";
    tts::logging::initialize(cfg);

    spdlog::info("json output");
    spdlog::default_logger()->flush();
    std::string json_out = oss.str();
    ASSERT_FALSE(json_out.empty());
    EXPECT_EQ(json_out[0], '{');  // JSON starts with {

    // Re-init as text
    oss.str("");
    oss.clear();
    cfg.log_format = "text";
    tts::logging::initialize(cfg);

    spdlog::info("text output");
    spdlog::default_logger()->flush();
    std::string text_out = oss.str();
    ASSERT_FALSE(text_out.empty());
    EXPECT_NE(text_out[0], '{');  // text format must NOT produce JSON

    // Restore original logger
    spdlog::set_default_logger(original_logger);
    cfg.log_level = "info";
    tts::logging::initialize(cfg);
}
