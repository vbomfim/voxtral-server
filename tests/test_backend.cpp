/// @file test_backend.cpp
/// @brief Developer unit tests for ITtsBackend implementations.
///
/// Tests VoxtralBackend stub (valid WAV output, initialize, is_ready)
/// and MockBackend (configurable responses, failure modes).

#include <gtest/gtest.h>

#include "tts/backend.hpp"
#include "tts/voxtral_backend.hpp"
#include "tts/mock_backend.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace tts;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/// Create a temporary file and return its path.
/// The file is not cleaned up — tests are short-lived.
std::string create_temp_file(const std::string& content = "stub model data") {
    auto path = std::filesystem::temp_directory_path() / "voxtral_test_model.bin";
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path.string();
}

/// Read a little-endian uint32 from a byte buffer.
uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8U)
         | (static_cast<uint32_t>(p[2]) << 16U)
         | (static_cast<uint32_t>(p[3]) << 24U);
}

/// Read a little-endian uint16 from a byte buffer.
uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0])
         | (static_cast<uint16_t>(p[1]) << 8U));
}

}  // namespace

// ============================================================================
// VoxtralBackend — initialization
// ============================================================================

TEST(VoxtralBackend, NotReadyBeforeInitialize) {
    VoxtralBackend backend;
    EXPECT_FALSE(backend.is_ready());
}

TEST(VoxtralBackend, InitializeWithValidPath) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    EXPECT_TRUE(backend.initialize(model_path));
    EXPECT_TRUE(backend.is_ready());
}

TEST(VoxtralBackend, InitializeWithEmptyPathFails) {
    VoxtralBackend backend;
    EXPECT_FALSE(backend.initialize(""));
    EXPECT_FALSE(backend.is_ready());
}

TEST(VoxtralBackend, InitializeWithNonexistentPathFails) {
    VoxtralBackend backend;
    EXPECT_FALSE(backend.initialize("/nonexistent/path/model.bin"));
    EXPECT_FALSE(backend.is_ready());
}

TEST(VoxtralBackend, ModelNameReturnsVoxtral4b) {
    VoxtralBackend backend;
    EXPECT_EQ(backend.model_name(), "voxtral-4b");
}

// ============================================================================
// VoxtralBackend — WAV output validation
// ============================================================================

TEST(VoxtralBackend, SynthesizeProducesValidWav) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = "Hello world";
    req.voice = "neutral_female";

    auto result = backend.synthesize(req);

    // Must have at least a 44-byte WAV header
    ASSERT_GE(result.audio_data.size(), 44U);

    const auto* d = result.audio_data.data();

    // RIFF header
    EXPECT_EQ(std::memcmp(d, "RIFF", 4), 0);
    EXPECT_EQ(std::memcmp(d + 8, "WAVE", 4), 0);

    // fmt sub-chunk
    EXPECT_EQ(std::memcmp(d + 12, "fmt ", 4), 0);
    EXPECT_EQ(read_u32_le(d + 16), 16U);       // PCM sub-chunk size
    EXPECT_EQ(read_u16_le(d + 20), 1U);         // PCM format
    EXPECT_EQ(read_u16_le(d + 22), 1U);         // Mono
    EXPECT_EQ(read_u32_le(d + 24), 24000U);     // 24kHz sample rate
    EXPECT_EQ(read_u16_le(d + 34), 16U);        // 16 bits per sample

    // data sub-chunk
    EXPECT_EQ(std::memcmp(d + 36, "data", 4), 0);
    uint32_t data_size = read_u32_le(d + 40);

    // 1 second of 24kHz 16-bit mono = 48000 bytes
    EXPECT_EQ(data_size, 48000U);

    // Total file = 44 header + data_size
    EXPECT_EQ(result.audio_data.size(), 44U + data_size);
}

TEST(VoxtralBackend, SynthesizeResultMetadata) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = "Test";

    auto result = backend.synthesize(req);
    EXPECT_EQ(result.sample_rate, 24000);
    EXPECT_DOUBLE_EQ(result.duration_seconds, 1.0);
    EXPECT_GT(result.inference_time_seconds, 0.0);
}

TEST(VoxtralBackend, SynthesizeRiffFileSize) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = "Check RIFF size";

    auto result = backend.synthesize(req);
    const auto* d = result.audio_data.data();

    // RIFF file size field = total bytes - 8
    uint32_t riff_size = read_u32_le(d + 4);
    EXPECT_EQ(riff_size, static_cast<uint32_t>(result.audio_data.size()) - 8U);
}

TEST(VoxtralBackend, SynthesizeByteRate) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    auto result = backend.synthesize(SynthesisRequest{});
    const auto* d = result.audio_data.data();

    // byte rate = sample_rate * channels * bytes_per_sample = 24000 * 1 * 2 = 48000
    EXPECT_EQ(read_u32_le(d + 28), 48000U);
    // block align = channels * bytes_per_sample = 1 * 2 = 2
    EXPECT_EQ(read_u16_le(d + 32), 2U);
}

// ============================================================================
// MockBackend — basic behavior
// ============================================================================

TEST(MockBackend, InitializeSucceedsByDefault) {
    MockBackend mock;
    EXPECT_TRUE(mock.initialize("/any/path"));
    EXPECT_TRUE(mock.is_ready());
}

TEST(MockBackend, InitializeCanFail) {
    MockBackend mock;
    mock.fail_initialize = true;
    EXPECT_FALSE(mock.initialize("/any/path"));
    EXPECT_FALSE(mock.is_ready());
}

TEST(MockBackend, ModelNameReturnsMock) {
    MockBackend mock;
    EXPECT_EQ(mock.model_name(), "mock-backend");
}

TEST(MockBackend, DefaultSynthesizeReturnsWavHeader) {
    MockBackend mock;
    (void)mock.initialize("/fake");

    SynthesisRequest req;
    req.text = "Hello";
    auto result = mock.synthesize(req);

    EXPECT_EQ(result.audio_data.size(), 44U);  // minimal WAV header
    EXPECT_EQ(result.sample_rate, 24000);
    EXPECT_DOUBLE_EQ(result.duration_seconds, 1.0);
}

TEST(MockBackend, CustomAudioData) {
    MockBackend mock;
    mock.audio_data = {0x01, 0x02, 0x03};
    mock.sample_rate = 16000;
    mock.duration_seconds = 0.5;

    auto result = mock.synthesize(SynthesisRequest{});
    EXPECT_EQ(result.audio_data, (std::vector<uint8_t>{0x01, 0x02, 0x03}));
    EXPECT_EQ(result.sample_rate, 16000);
    EXPECT_DOUBLE_EQ(result.duration_seconds, 0.5);
}

TEST(MockBackend, SynthesizeFailsWhenConfigured) {
    MockBackend mock;
    mock.should_fail = true;
    mock.fail_message = "GPU out of memory";

    EXPECT_THROW({
        (void)mock.synthesize(SynthesisRequest{});
    }, std::runtime_error);
}

TEST(MockBackend, CallCountTracking) {
    MockBackend mock;
    EXPECT_EQ(mock.call_count.load(), 0);

    (void)mock.synthesize(SynthesisRequest{});
    EXPECT_EQ(mock.call_count.load(), 1);

    (void)mock.synthesize(SynthesisRequest{});
    (void)mock.synthesize(SynthesisRequest{});
    EXPECT_EQ(mock.call_count.load(), 3);
}

TEST(MockBackend, LastRequestCaptured) {
    MockBackend mock;

    SynthesisRequest req;
    req.text = "Capture this";
    req.voice = "casual_male";
    req.speed = 1.5F;

    (void)mock.synthesize(req);
    EXPECT_EQ(mock.last_request.text, "Capture this");
    EXPECT_EQ(mock.last_request.voice, "casual_male");
    EXPECT_FLOAT_EQ(mock.last_request.speed, 1.5F);
}

TEST(MockBackend, LatencySimulation) {
    MockBackend mock;
    mock.latency_ms = 50;

    auto t0 = std::chrono::steady_clock::now();
    (void)mock.synthesize(SynthesisRequest{});
    auto t1 = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_GE(elapsed_ms, 40);  // Allow some scheduling jitter
}
