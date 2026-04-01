/// @file test_wav_contract.cpp
/// @brief QA contract tests for WAV output across all backends.
///
/// Validates that every WAV file produced by any ITtsBackend implementation
/// conforms to the RIFF/WAVE PCM specification. These tests survive a
/// complete backend rewrite — they test the OUTPUT CONTRACT, not internals.
/// Tags: [CONTRACT], [EDGE], [BOUNDARY]

#include <gtest/gtest.h>

#include "tts/backend.hpp"
#include "tts/mock_backend.hpp"
#include "tts/voxtral_backend.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace tts;

// ============================================================================
// Helpers
// ============================================================================

namespace {

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8U)
         | (static_cast<uint32_t>(p[2]) << 16U)
         | (static_cast<uint32_t>(p[3]) << 24U);
}

uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0])
         | (static_cast<uint16_t>(p[1]) << 8U));
}

std::string create_temp_file(const std::string& content = "stub model data") {
    auto path = std::filesystem::temp_directory_path() / "voxtral_qa_model.bin";
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path.string();
}

/// Validate a WAV byte vector against the RIFF/WAVE PCM specification.
/// Returns empty string on success, error description on failure.
std::string validate_wav_structure(const std::vector<uint8_t>& wav) {
    if (wav.size() < 44) {
        return "WAV file too small: " + std::to_string(wav.size()) + " bytes (minimum 44)";
    }

    const auto* d = wav.data();

    // RIFF header
    if (std::memcmp(d, "RIFF", 4) != 0) {
        return "Missing RIFF magic bytes";
    }
    if (std::memcmp(d + 8, "WAVE", 4) != 0) {
        return "Missing WAVE format identifier";
    }

    // RIFF size = total_file_size - 8
    uint32_t riff_size = read_u32_le(d + 4);
    if (riff_size != static_cast<uint32_t>(wav.size()) - 8U) {
        return "RIFF size field (" + std::to_string(riff_size)
             + ") != file_size - 8 (" + std::to_string(wav.size() - 8) + ")";
    }

    // fmt sub-chunk
    if (std::memcmp(d + 12, "fmt ", 4) != 0) {
        return "Missing 'fmt ' sub-chunk";
    }

    uint32_t fmt_size = read_u32_le(d + 16);
    if (fmt_size != 16) {
        return "fmt sub-chunk size != 16 (non-PCM format?)";
    }

    uint16_t audio_format = read_u16_le(d + 20);
    if (audio_format != 1) {
        return "Audio format != 1 (not PCM)";
    }

    uint16_t channels = read_u16_le(d + 22);
    if (channels == 0) {
        return "Channel count is 0";
    }

    uint32_t sample_rate = read_u32_le(d + 24);
    if (sample_rate == 0) {
        return "Sample rate is 0";
    }

    uint16_t bits_per_sample = read_u16_le(d + 34);
    if (bits_per_sample == 0 || bits_per_sample % 8 != 0) {
        return "Invalid bits_per_sample: " + std::to_string(bits_per_sample);
    }

    // Computed fields must be self-consistent
    uint32_t expected_byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint32_t actual_byte_rate = read_u32_le(d + 28);
    if (actual_byte_rate != expected_byte_rate) {
        return "Byte rate mismatch: expected=" + std::to_string(expected_byte_rate)
             + " actual=" + std::to_string(actual_byte_rate);
    }

    uint16_t expected_block_align = static_cast<uint16_t>(channels * (bits_per_sample / 8));
    uint16_t actual_block_align = read_u16_le(d + 32);
    if (actual_block_align != expected_block_align) {
        return "Block align mismatch: expected=" + std::to_string(expected_block_align)
             + " actual=" + std::to_string(actual_block_align);
    }

    // data sub-chunk
    if (std::memcmp(d + 36, "data", 4) != 0) {
        return "Missing 'data' sub-chunk";
    }

    uint32_t data_size = read_u32_le(d + 40);
    if (44U + data_size != wav.size()) {
        return "data_size + header (44) != total file size: "
             + std::to_string(data_size) + " + 44 != " + std::to_string(wav.size());
    }

    // Data size must be aligned to block_align
    if (data_size % actual_block_align != 0) {
        return "data_size (" + std::to_string(data_size)
             + ") not aligned to block_align (" + std::to_string(actual_block_align) + ")";
    }

    return "";  // Success
}

}  // namespace

// ============================================================================
// [CONTRACT] VoxtralBackend WAV structural self-consistency
// The build_silent_wav function must produce fully spec-compliant WAV.
// This tests the entire header end-to-end via the validate function,
// catching any inconsistency between size fields.
// ============================================================================

TEST(WavContract, VoxtralBackendProducesValidWav) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = "WAV contract test";
    req.voice = "neutral_female";

    auto result = backend.synthesize(req);
    std::string error = validate_wav_structure(result.audio_data);
    EXPECT_TRUE(error.empty()) << "WAV validation failed: " << error;
}

// ============================================================================
// [CONTRACT] MockBackend minimal WAV header is valid
// The 44-byte header with data_size=0 must still be structurally valid.
// ============================================================================

TEST(WavContract, MockBackendMinimalHeaderIsValid) {
    MockBackend mock;
    (void)mock.initialize("/fake");

    auto result = mock.synthesize(SynthesisRequest{});
    std::string error = validate_wav_structure(result.audio_data);
    EXPECT_TRUE(error.empty()) << "MockBackend WAV validation failed: " << error;
}

// ============================================================================
// [CONTRACT] Both backends agree on audio parameters
// The output format (sample_rate, channels, bits) must be consistent
// across all ITtsBackend implementations.
// ============================================================================

TEST(WavContract, BackendsAgreeOnAudioFormat) {
    // VoxtralBackend
    auto model_path = create_temp_file();
    VoxtralBackend voxtral;
    ASSERT_TRUE(voxtral.initialize(model_path));
    auto voxtral_result = voxtral.synthesize(SynthesisRequest{});

    // MockBackend (default)
    MockBackend mock;
    (void)mock.initialize("/fake");
    auto mock_result = mock.synthesize(SynthesisRequest{});

    // Both must use the same audio parameters
    EXPECT_EQ(voxtral_result.sample_rate, mock_result.sample_rate)
        << "Sample rate mismatch between backends";

    // Both WAVs must parse identically for format fields
    const auto* vd = voxtral_result.audio_data.data();
    const auto* md = mock_result.audio_data.data();

    // Format: PCM
    EXPECT_EQ(read_u16_le(vd + 20), read_u16_le(md + 20));
    // Channels
    EXPECT_EQ(read_u16_le(vd + 22), read_u16_le(md + 22));
    // Sample rate
    EXPECT_EQ(read_u32_le(vd + 24), read_u32_le(md + 24));
    // Bits per sample
    EXPECT_EQ(read_u16_le(vd + 34), read_u16_le(md + 34));
}

// ============================================================================
// [CONTRACT] SynthesisResult.sample_rate matches WAV header sample rate
// The metadata field must be consistent with the WAV binary content.
// ============================================================================

TEST(WavContract, ResultSampleRateMatchesWavHeader) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    auto result = backend.synthesize(SynthesisRequest{});
    uint32_t header_rate = read_u32_le(result.audio_data.data() + 24);
    EXPECT_EQ(static_cast<uint32_t>(result.sample_rate), header_rate)
        << "SynthesisResult.sample_rate (" << result.sample_rate
        << ") disagrees with WAV header (" << header_rate << ")";
}

// ============================================================================
// [CONTRACT] SynthesisResult.duration_seconds matches WAV data size
// Duration = data_size / (sample_rate * channels * bytes_per_sample)
// ============================================================================

TEST(WavContract, ResultDurationMatchesWavDataSize) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    auto result = backend.synthesize(SynthesisRequest{});

    const auto* d = result.audio_data.data();
    uint32_t data_size = read_u32_le(d + 40);
    uint32_t byte_rate = read_u32_le(d + 28);

    double computed_duration = static_cast<double>(data_size) / static_cast<double>(byte_rate);
    EXPECT_DOUBLE_EQ(result.duration_seconds, computed_duration)
        << "duration_seconds disagrees with WAV data_size/byte_rate";
}

// ============================================================================
// [CONTRACT] inference_time_seconds is non-negative and reasonable
// ============================================================================

TEST(WavContract, InferenceTimeIsReasonable) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    auto result = backend.synthesize(SynthesisRequest{});
    EXPECT_GE(result.inference_time_seconds, 0.0);
    // Stub should complete in under 1 second
    EXPECT_LT(result.inference_time_seconds, 1.0);
}

// ============================================================================
// [EDGE] WAV with zero-length text — backend must still produce valid WAV.
// Tests that empty input doesn't cause size field corruption.
// ============================================================================

TEST(WavEdge, EmptyTextStillProducesValidWav) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = "";  // empty

    auto result = backend.synthesize(req);
    std::string error = validate_wav_structure(result.audio_data);
    EXPECT_TRUE(error.empty()) << "Empty text WAV validation failed: " << error;
}

// ============================================================================
// [EDGE] WAV with very long text — must not overflow size fields.
// ============================================================================

TEST(WavEdge, LongTextStillProducesValidWav) {
    auto model_path = create_temp_file();
    VoxtralBackend backend;
    ASSERT_TRUE(backend.initialize(model_path));

    SynthesisRequest req;
    req.text = std::string(100000, 'A');  // 100KB of text

    auto result = backend.synthesize(req);
    std::string error = validate_wav_structure(result.audio_data);
    EXPECT_TRUE(error.empty()) << "Long text WAV validation failed: " << error;
}

// ============================================================================
// [EDGE] MockBackend custom audio data must still produce valid WAV
// when the user provides their own data (non-default path).
// ============================================================================

TEST(WavEdge, MockCustomAudioPreservesUserData) {
    MockBackend mock;
    // Set custom audio that is NOT a valid WAV — verify MockBackend
    // passes it through verbatim (it's the caller's responsibility)
    mock.audio_data = {0xDE, 0xAD, 0xBE, 0xEF};
    mock.sample_rate = 16000;

    auto result = mock.synthesize(SynthesisRequest{});
    EXPECT_EQ(result.audio_data, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(result.sample_rate, 16000);
}

// ============================================================================
// [BOUNDARY] SynthesisRequest default values — verify the struct defaults.
// ============================================================================

TEST(WavContract, SynthesisRequestDefaults) {
    SynthesisRequest req;
    EXPECT_EQ(req.response_format, "wav");
    EXPECT_FLOAT_EQ(req.speed, 1.0F);
    EXPECT_TRUE(req.text.empty());
    EXPECT_TRUE(req.voice.empty());
    EXPECT_TRUE(req.model.empty());
}

TEST(WavContract, SynthesisResultDefaults) {
    SynthesisResult result;
    EXPECT_TRUE(result.audio_data.empty());
    EXPECT_EQ(result.sample_rate, 24000);
    EXPECT_DOUBLE_EQ(result.duration_seconds, 0.0);
    EXPECT_DOUBLE_EQ(result.inference_time_seconds, 0.0);
}
