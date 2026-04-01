/// @file voxtral_backend.cpp
/// @brief VoxtralBackend stub implementation.
///
/// TODO: Replace stub with real voxtral-tts.c calls when submodule is added.

#include "tts/voxtral_backend.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace tts {

namespace {

/// Build a valid WAV file: RIFF header + silent PCM data.
/// @param sample_rate  Samples per second (e.g. 24000).
/// @param duration_sec Duration in seconds.
/// @return Complete WAV file bytes (header + data), or empty on invalid input.
std::vector<uint8_t> build_silent_wav(int sample_rate, double duration_sec) {
    if (sample_rate <= 0 || duration_sec < 0.0) {
        return {};
    }

    const int channels = 1;
    const int bits_per_sample = 16;
    const int bytes_per_sample = bits_per_sample / 8;
    const auto num_samples = static_cast<uint32_t>(
        static_cast<double>(sample_rate) * duration_sec);
    const uint32_t data_size = num_samples * static_cast<uint32_t>(channels * bytes_per_sample);
    const uint32_t file_size = 36 + data_size;  // RIFF header (44 bytes total - 8 for RIFF+size)

    std::vector<uint8_t> wav(44 + data_size, 0);
    auto* p = wav.data();

    // Helper: write a little-endian uint32
    auto write_u32 = [](uint8_t* dst, uint32_t val) {
        dst[0] = static_cast<uint8_t>(val & 0xFFU);
        dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
        dst[2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
        dst[3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
    };

    // Helper: write a little-endian uint16
    auto write_u16 = [](uint8_t* dst, uint16_t val) {
        dst[0] = static_cast<uint8_t>(val & 0xFFU);
        dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
    };

    // RIFF header
    std::memcpy(p, "RIFF", 4);
    write_u32(p + 4, file_size);
    std::memcpy(p + 8, "WAVE", 4);

    // fmt sub-chunk
    std::memcpy(p + 12, "fmt ", 4);
    write_u32(p + 16, 16);  // sub-chunk size (PCM)
    write_u16(p + 20, 1);   // audio format (1 = PCM)
    write_u16(p + 22, static_cast<uint16_t>(channels));
    write_u32(p + 24, static_cast<uint32_t>(sample_rate));
    write_u32(p + 28, static_cast<uint32_t>(sample_rate * channels * bytes_per_sample));  // byte rate
    write_u16(p + 32, static_cast<uint16_t>(channels * bytes_per_sample));  // block align
    write_u16(p + 34, static_cast<uint16_t>(bits_per_sample));

    // data sub-chunk
    std::memcpy(p + 36, "data", 4);
    write_u32(p + 40, data_size);
    // PCM data is already zeroed (silence)

    return wav;
}

}  // namespace

bool VoxtralBackend::initialize(const std::string& model_path) {
    // Security: validate model_path exists and is readable
    if (model_path.empty()) {
        spdlog::error("VoxtralBackend::initialize — model_path is empty");
        return false;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(model_path, ec) || ec) {
        spdlog::error("VoxtralBackend::initialize — model_path does not exist: {}", model_path);
        return false;
    }
    if (!fs::is_regular_file(model_path, ec) && !fs::is_directory(model_path, ec)) {
        spdlog::error("VoxtralBackend::initialize — model_path is not a file or directory: {}",
                       model_path);
        return false;
    }

    auto t_start = std::chrono::steady_clock::now();

    // TODO: Replace stub with real voxtral-tts.c calls when submodule is added
    model_path_ = model_path;
    initialized_ = true;

    auto t_end = std::chrono::steady_clock::now();
    double load_time = std::chrono::duration<double>(t_end - t_start).count();
    spdlog::info("VoxtralBackend — model loaded in {:.3f}s (path={})", load_time, model_path);

    return true;
}

SynthesisResult VoxtralBackend::synthesize(const SynthesisRequest& request) {
    if (!initialized_) {
        throw std::logic_error(
            "VoxtralBackend::synthesize() called before successful initialize()");
    }

    auto t_start = std::chrono::steady_clock::now();

    // TODO: Replace stub with real voxtral-tts.c calls when submodule is added
    // Stub: generate 1 second of silence at 24kHz, 16-bit mono
    constexpr int kSampleRate = 24000;
    constexpr double kDuration = 1.0;
    auto wav = build_silent_wav(kSampleRate, kDuration);

    auto t_end = std::chrono::steady_clock::now();
    double inference_time = std::chrono::duration<double>(t_end - t_start).count();

    spdlog::debug("VoxtralBackend::synthesize — voice={} text_len={} duration={:.3f}s inference={:.6f}s",
                   request.voice,
                   request.text.size(),
                   kDuration,
                   inference_time);

    SynthesisResult result;
    result.audio_data = std::move(wav);
    result.sample_rate = kSampleRate;
    result.duration_seconds = kDuration;
    result.inference_time_seconds = inference_time;
    return result;
}

bool VoxtralBackend::is_ready() const {
    return initialized_;
}

std::string VoxtralBackend::model_name() const {
    return "voxtral-4b";
}

}  // namespace tts
