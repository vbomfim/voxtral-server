/// @file test_validation.cpp
/// @brief Developer unit tests for request validation.
///
/// Tests each validation rule independently with boundary values
/// and error format compliance.

#include <gtest/gtest.h>

#include "tts/validation.hpp"
#include "tts/voices.hpp"

#include <string>

using namespace tts;

// ============================================================================
// Content-Type validation
// ============================================================================

TEST(ValidateContentType, AcceptsApplicationJson) {
    auto r = validate_content_type("application/json");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateContentType, AcceptsJsonWithCharset) {
    auto r = validate_content_type("application/json; charset=utf-8");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateContentType, RejectsTextPlain) {
    auto r = validate_content_type("text/plain");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.http_status, 400);
    EXPECT_EQ(r.error_code, "invalid_content_type");
}

TEST(ValidateContentType, RejectsEmpty) {
    auto r = validate_content_type("");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "invalid_content_type");
}

TEST(ValidateContentType, RejectsMultipartFormData) {
    auto r = validate_content_type("multipart/form-data");
    EXPECT_FALSE(r.valid);
}

// ============================================================================
// Body size validation
// ============================================================================

TEST(ValidateBodySize, AcceptsSmallBody) {
    auto r = validate_body_size(100);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateBodySize, AcceptsExactly1MB) {
    auto r = validate_body_size(1048576);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateBodySize, RejectsOver1MB) {
    auto r = validate_body_size(1048577);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.http_status, 413);
    EXPECT_EQ(r.error_code, "payload_too_large");
}

TEST(ValidateBodySize, AcceptsZeroBytes) {
    auto r = validate_body_size(0);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateBodySize, CustomMaxAllowed) {
    auto r = validate_body_size(500, 1000);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateBodySize, CustomMaxRejected) {
    auto r = validate_body_size(1001, 1000);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.http_status, 413);
}

// ============================================================================
// Model validation
// ============================================================================

TEST(ValidateModel, AcceptsVoxtral4b) {
    auto r = validate_model("voxtral-4b");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateModel, RejectsEmpty) {
    auto r = validate_model("");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "missing_input");
    EXPECT_EQ(r.error_param, "model");
}

TEST(ValidateModel, RejectsInvalidModel) {
    auto r = validate_model("gpt-4");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "invalid_model");
    EXPECT_EQ(r.error_param, "model");
}

TEST(ValidateModel, RejectsCaseSensitive) {
    auto r = validate_model("Voxtral-4B");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "invalid_model");
}

// ============================================================================
// Input validation
// ============================================================================

TEST(ValidateInput, AcceptsNormalText) {
    auto r = validate_input("Hello, how are you?");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateInput, RejectsEmptyString) {
    auto r = validate_input("");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "missing_input");
    EXPECT_EQ(r.error_param, "input");
}

TEST(ValidateInput, RejectsWhitespaceOnly) {
    auto r = validate_input("   \t\n  ");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "missing_input");
}

TEST(ValidateInput, AcceptsSingleChar) {
    auto r = validate_input("a");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateInput, AcceptsExactly4096Chars) {
    std::string input(4096, 'a');
    auto r = validate_input(input, 4096);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateInput, Rejects4097Chars) {
    std::string input(4097, 'a');
    auto r = validate_input(input, 4096);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "string_above_max_length");
    EXPECT_EQ(r.error_param, "input");
}

TEST(ValidateInput, CustomMaxCharsAllowed) {
    std::string input(100, 'b');
    auto r = validate_input(input, 100);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateInput, CustomMaxCharsRejected) {
    std::string input(101, 'b');
    auto r = validate_input(input, 100);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "string_above_max_length");
}

TEST(ValidateInput, AcceptsTextWithLeadingWhitespace) {
    auto r = validate_input("  Hello");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateInput, AcceptsNewlineWithText) {
    auto r = validate_input("\nHello\n");
    EXPECT_TRUE(r.valid);
}

// ============================================================================
// Voice validation
// ============================================================================

TEST(ValidateVoice, AcceptsValidVoice) {
    VoiceCatalog catalog;
    auto r = validate_voice("neutral_female", catalog);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateVoice, RejectsEmptyVoice) {
    VoiceCatalog catalog;
    auto r = validate_voice("", catalog);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "missing_input");
    EXPECT_EQ(r.error_param, "voice");
}

TEST(ValidateVoice, RejectsUnknownVoice) {
    VoiceCatalog catalog;
    auto r = validate_voice("nonexistent_voice", catalog);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "invalid_voice");
    EXPECT_EQ(r.error_param, "voice");
}

TEST(ValidateVoice, ErrorMessageListsValidVoices) {
    VoiceCatalog catalog;
    auto r = validate_voice("bad_voice", catalog);
    EXPECT_FALSE(r.valid);
    // Error message should include valid voice IDs
    EXPECT_NE(r.error_message.find("neutral_female"), std::string::npos);
    EXPECT_NE(r.error_message.find("casual_male"), std::string::npos);
}

TEST(ValidateVoice, AcceptsAllCatalogVoices) {
    VoiceCatalog catalog;
    for (const auto& voice : catalog.all()) {
        auto r = validate_voice(voice.id, catalog);
        EXPECT_TRUE(r.valid) << "Voice '" << voice.id << "' should be valid";
    }
}

// ============================================================================
// Response format validation
// ============================================================================

TEST(ValidateResponseFormat, AcceptsWav) {
    auto r = validate_response_format("wav");
    EXPECT_TRUE(r.valid);
}

TEST(ValidateResponseFormat, AcceptsEmpty) {
    auto r = validate_response_format("");
    EXPECT_TRUE(r.valid);  // Empty defaults to wav
}

TEST(ValidateResponseFormat, RejectsMp3) {
    auto r = validate_response_format("mp3");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "invalid_format");
    EXPECT_EQ(r.error_param, "response_format");
}

TEST(ValidateResponseFormat, RejectsFlac) {
    auto r = validate_response_format("flac");
    EXPECT_FALSE(r.valid);
}

TEST(ValidateResponseFormat, RejectsOpus) {
    auto r = validate_response_format("opus");
    EXPECT_FALSE(r.valid);
}

// ============================================================================
// Speed validation
// ============================================================================

TEST(ValidateSpeed, AcceptsDefault) {
    auto r = validate_speed(1.0F);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateSpeed, AcceptsMinBoundary) {
    auto r = validate_speed(0.25F);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateSpeed, AcceptsMaxBoundary) {
    auto r = validate_speed(4.0F);
    EXPECT_TRUE(r.valid);
}

TEST(ValidateSpeed, RejectsBelowMin) {
    auto r = validate_speed(0.24F);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "speed_out_of_range");
    EXPECT_EQ(r.error_param, "speed");
}

TEST(ValidateSpeed, RejectsAboveMax) {
    auto r = validate_speed(4.01F);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.error_code, "speed_out_of_range");
}

TEST(ValidateSpeed, RejectsZero) {
    auto r = validate_speed(0.0F);
    EXPECT_FALSE(r.valid);
}

TEST(ValidateSpeed, RejectsNegative) {
    auto r = validate_speed(-1.0F);
    EXPECT_FALSE(r.valid);
}

TEST(ValidateSpeed, AcceptsMidRange) {
    auto r = validate_speed(2.0F);
    EXPECT_TRUE(r.valid);
}

// ============================================================================
// Error result structure
// ============================================================================

TEST(ValidationResult, DefaultIsValid) {
    ValidationResult r;
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.http_status, 200);
}

TEST(MakeValidationError, SetsAllFields) {
    auto r = make_validation_error(400, "missing_input", "Field required", "input");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.http_status, 400);
    EXPECT_EQ(r.error_type, "invalid_request_error");
    EXPECT_EQ(r.error_code, "missing_input");
    EXPECT_EQ(r.error_message, "Field required");
    EXPECT_EQ(r.error_param, "input");
}
