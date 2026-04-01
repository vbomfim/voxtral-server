#pragma once

/// @file mock_backend.hpp
/// @brief Configurable mock TTS backend for testing.
///
/// MockBackend lets tests control every aspect of backend behavior:
/// success/failure, latency, audio content, initialization state.

#include "tts/backend.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace tts {

/// Mock ITtsBackend for unit and integration tests.
///
/// Configure via public members before calling synthesize():
///   mock.should_fail = true;        // synthesize() throws
///   mock.latency_ms = 100;          // simulate slow inference
///   mock.audio_data = {...};         // return specific audio bytes
///   mock.sample_rate = 16000;       // override sample rate
class MockBackend : public ITtsBackend {
public:
    // --- Configuration (set before calling synthesize) ---

    /// If true, synthesize() throws std::runtime_error.
    bool should_fail = false;

    /// Error message when should_fail is true.
    std::string fail_message = "Mock backend failure";

    /// Simulated inference latency in milliseconds.
    int latency_ms = 0;

    /// Audio bytes to return. If empty, returns a minimal 44-byte WAV header.
    std::vector<uint8_t> audio_data;

    /// Sample rate to report in the result.
    int sample_rate = 24000;

    /// Audio duration to report in the result.
    double duration_seconds = 1.0;

    /// If true, initialize() returns false.
    bool fail_initialize = false;

    // --- Observation (read after calls) ---

    /// Number of times synthesize() was called.
    std::atomic<int> call_count{0};

    /// The most recent request passed to synthesize().
    SynthesisRequest last_request;

    // --- ITtsBackend interface ---

    [[nodiscard]] bool initialize(const std::string& /*model_path*/) override {
        if (fail_initialize) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    [[nodiscard]] SynthesisResult synthesize(const SynthesisRequest& request) override {
        call_count.fetch_add(1, std::memory_order_relaxed);
        last_request = request;

        if (should_fail) {
            throw std::runtime_error(fail_message);
        }

        if (latency_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(latency_ms));
        }

        SynthesisResult result;
        if (!audio_data.empty()) {
            result.audio_data = audio_data;
        } else {
            // Minimal valid WAV header (44 bytes, 0 data)
            result.audio_data = minimal_wav_header();
        }
        result.sample_rate = sample_rate;
        result.duration_seconds = duration_seconds;
        result.inference_time_seconds = 0.001;  // ~1ms fake inference
        return result;
    }

    [[nodiscard]] bool is_ready() const override {
        return initialized_;
    }

    [[nodiscard]] std::string model_name() const override {
        return "mock-backend";
    }

private:
    bool initialized_ = false;

    /// Return a minimal 44-byte WAV header with zero data bytes.
    static std::vector<uint8_t> minimal_wav_header() {
        // RIFF....WAVEfmt ............data....
        std::vector<uint8_t> h(44, 0);
        auto* p = h.data();
        // RIFF
        p[0] = 'R'; p[1] = 'I'; p[2] = 'F'; p[3] = 'F';
        // file size = 36 (no data)
        p[4] = 36; p[5] = 0; p[6] = 0; p[7] = 0;
        // WAVE
        p[8] = 'W'; p[9] = 'A'; p[10] = 'V'; p[11] = 'E';
        // fmt
        p[12] = 'f'; p[13] = 'm'; p[14] = 't'; p[15] = ' ';
        p[16] = 16; // sub-chunk size
        p[20] = 1;  // PCM
        p[22] = 1;  // mono
        // sample rate 24000 = 0x5DC0
        p[24] = 0xC0U; p[25] = 0x5DU;
        // byte rate 48000 = 0xBB80
        p[28] = 0x80U; p[29] = 0xBBU;
        p[32] = 2;  // block align
        p[34] = 16; // bits per sample
        // data
        p[36] = 'd'; p[37] = 'a'; p[38] = 't'; p[39] = 'a';
        // data size = 0
        return h;
    }
};

}  // namespace tts
