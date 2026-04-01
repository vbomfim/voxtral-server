/// @file voxtral_backend.cpp
/// @brief VoxtralBackend implementation — real engine + stub fallback.
///
/// When TTS_HAS_VOXTRAL_ENGINE is defined and tts_load() succeeds,
/// synthesize() runs real TTS inference via voxtral-tts.c.
/// Otherwise it falls back to the original silent WAV stub so that
/// tests work without a 7.5GB model on disk.

#include "tts/voxtral_backend.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef TTS_HAS_VOXTRAL_ENGINE
extern "C" {
#include "voxtral_tts.h"
}
#endif

namespace tts {

namespace {

/// Little-endian uint32 writer for WAV header construction.
void write_u32(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>(val & 0xFFU);
    dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
    dst[2] = static_cast<uint8_t>((val >> 16U) & 0xFFU);
    dst[3] = static_cast<uint8_t>((val >> 24U) & 0xFFU);
}

/// Little-endian uint16 writer for WAV header construction.
void write_u16(uint8_t* dst, uint16_t val) {
    dst[0] = static_cast<uint8_t>(val & 0xFFU);
    dst[1] = static_cast<uint8_t>((val >> 8U) & 0xFFU);
}

/// Build a RIFF WAV header for mono 16-bit PCM audio.
/// @param sample_rate Samples per second (e.g. 24000).
/// @param num_samples Number of 16-bit PCM samples.
/// @return 44-byte WAV header (caller appends PCM data after this).
std::vector<uint8_t> build_wav_header(int sample_rate, uint32_t num_samples) {
    constexpr int kChannels = 1;
    constexpr int kBitsPerSample = 16;
    constexpr int kBytesPerSample = kBitsPerSample / 8;

    const uint32_t data_size = num_samples * static_cast<uint32_t>(kChannels * kBytesPerSample);
    const uint32_t file_size = 36 + data_size;

    std::vector<uint8_t> hdr(44, 0);
    auto* p = hdr.data();

    // RIFF header
    std::memcpy(p, "RIFF", 4);
    write_u32(p + 4, file_size);
    std::memcpy(p + 8, "WAVE", 4);

    // fmt sub-chunk
    std::memcpy(p + 12, "fmt ", 4);
    write_u32(p + 16, 16);  // sub-chunk size (PCM)
    write_u16(p + 20, 1);   // audio format (1 = PCM)
    write_u16(p + 22, static_cast<uint16_t>(kChannels));
    write_u32(p + 24, static_cast<uint32_t>(sample_rate));
    write_u32(p + 28, static_cast<uint32_t>(sample_rate * kChannels * kBytesPerSample));
    write_u16(p + 32, static_cast<uint16_t>(kChannels * kBytesPerSample));
    write_u16(p + 34, static_cast<uint16_t>(kBitsPerSample));

    // data sub-chunk header (caller writes PCM data starting at byte 44)
    std::memcpy(p + 36, "data", 4);
    write_u32(p + 40, data_size);

    return hdr;
}

/// Build a valid WAV file: RIFF header + silent PCM data.
/// @param sample_rate  Samples per second (e.g. 24000).
/// @param duration_sec Duration in seconds.
/// @return Complete WAV file bytes (header + zeroed data), or empty on invalid input.
std::vector<uint8_t> build_silent_wav(int sample_rate, double duration_sec) {
    if (sample_rate <= 0 || duration_sec < 0.0) {
        return {};
    }

    const auto num_samples = static_cast<uint32_t>(
        static_cast<double>(sample_rate) * duration_sec);

    auto wav = build_wav_header(sample_rate, num_samples);
    // Extend with zeroed PCM data (silence)
    wav.resize(44 + num_samples * 2, 0);
    return wav;
}

#ifdef TTS_HAS_VOXTRAL_ENGINE
/// Convert float samples [-1.0, 1.0] to a complete WAV file with 16-bit PCM data.
/// @param samples   Float sample buffer from tts_generate().
/// @param n_samples Number of float samples.
/// @param sample_rate Output sample rate (e.g. 24000).
/// @return Complete WAV file bytes (header + PCM data).
std::vector<uint8_t> build_wav_from_float_samples(const float* samples, int n_samples,
                                                   int sample_rate) {
    if (n_samples <= 0 || samples == nullptr) {
        return build_silent_wav(sample_rate, 0.0);
    }

    const auto num = static_cast<uint32_t>(n_samples);
    auto wav = build_wav_header(sample_rate, num);

    // Append 16-bit PCM data converted from float [-1.0, 1.0]
    wav.resize(44 + num * 2);
    auto* pcm = wav.data() + 44;

    for (uint32_t i = 0; i < num; ++i) {
        // Clamp to [-1.0, 1.0] then scale to int16_t range
        float clamped = std::fmax(-1.0F, std::fmin(1.0F, samples[i]));
        auto sample = static_cast<int16_t>(clamped * 32767.0F);
        // Write little-endian int16
        pcm[i * 2]     = static_cast<uint8_t>(static_cast<uint16_t>(sample) & 0xFFU);
        pcm[i * 2 + 1] = static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8U) & 0xFFU);
    }

    return wav;
}
#endif  // TTS_HAS_VOXTRAL_ENGINE

}  // namespace

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

VoxtralBackend::~VoxtralBackend() {
#ifdef TTS_HAS_VOXTRAL_ENGINE
    if (ctx_ != nullptr) {
        tts_free(static_cast<tts_ctx_t*>(ctx_));
        ctx_ = nullptr;
        spdlog::debug("VoxtralBackend — engine context freed");
    }
#endif
}

// ---------------------------------------------------------------------------
// initialize()
// ---------------------------------------------------------------------------

bool VoxtralBackend::initialize(const std::string& model_path) {
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

#ifdef TTS_HAS_VOXTRAL_ENGINE
    // Attempt to load the real voxtral-tts.c model
    auto* ctx = tts_load(model_path.c_str());
    if (ctx != nullptr) {
        ctx_ = ctx;
        use_real_engine_ = true;
        auto t_end = std::chrono::steady_clock::now();
        double load_time = std::chrono::duration<double>(t_end - t_start).count();
        spdlog::info("VoxtralBackend — real engine loaded in {:.3f}s (path={})",
                     load_time, model_path);
    } else {
        use_real_engine_ = false;
        spdlog::warn("VoxtralBackend — tts_load() failed, falling back to stub (path={})",
                     model_path);
    }
#else
    auto t_end = std::chrono::steady_clock::now();
    double load_time = std::chrono::duration<double>(t_end - t_start).count();
    spdlog::info("VoxtralBackend — stub mode, model path validated in {:.3f}s (path={})",
                 load_time, model_path);
#endif

    model_path_ = model_path;
    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// synthesize()
// ---------------------------------------------------------------------------

SynthesisResult VoxtralBackend::synthesize(const SynthesisRequest& request) {
    if (!initialized_) {
        throw std::logic_error(
            "VoxtralBackend::synthesize() called before successful initialize()");
    }

    auto t_start = std::chrono::steady_clock::now();

#ifdef TTS_HAS_VOXTRAL_ENGINE
    if (use_real_engine_) {
        auto* ctx = static_cast<tts_ctx_t*>(ctx_);

        float* samples = nullptr;
        int n_samples = 0;

        int rc = tts_generate(ctx, request.text.c_str(), request.voice.c_str(),
                              &samples, &n_samples);

        auto t_end = std::chrono::steady_clock::now();
        double inference_time = std::chrono::duration<double>(t_end - t_start).count();

        if (rc != 0 || samples == nullptr || n_samples <= 0) {
            spdlog::error("VoxtralBackend::synthesize — tts_generate() failed (rc={})", rc);
            // Free samples if allocated despite error
            if (samples != nullptr) {
                free(samples);  // NOLINT(cppcoreguidelines-no-malloc)
            }
            throw std::runtime_error("TTS inference failed");
        }

        constexpr int kSampleRate = TTS_SAMPLE_RATE;  // 24000
        auto wav = build_wav_from_float_samples(samples, n_samples, kSampleRate);
        double duration = static_cast<double>(n_samples) / static_cast<double>(kSampleRate);

        // Caller owns the samples buffer — free it
        free(samples);  // NOLINT(cppcoreguidelines-no-malloc)

        spdlog::debug("VoxtralBackend::synthesize — voice={} text_len={} "
                       "duration={:.3f}s inference={:.3f}s samples={}",
                       request.voice, request.text.size(), duration, inference_time, n_samples);

        SynthesisResult result;
        result.audio_data = std::move(wav);
        result.sample_rate = kSampleRate;
        result.duration_seconds = duration;
        result.inference_time_seconds = inference_time;
        return result;
    }
#endif

    // Stub path: generate 1 second of silence at 24kHz, 16-bit mono
    constexpr int kSampleRate = 24000;
    constexpr double kDuration = 1.0;
    auto wav = build_silent_wav(kSampleRate, kDuration);

    auto t_end = std::chrono::steady_clock::now();
    double inference_time = std::chrono::duration<double>(t_end - t_start).count();

    spdlog::debug("VoxtralBackend::synthesize [stub] — voice={} text_len={} "
                   "duration={:.3f}s inference={:.6f}s",
                   request.voice, request.text.size(), kDuration, inference_time);

    SynthesisResult result;
    result.audio_data = std::move(wav);
    result.sample_rate = kSampleRate;
    result.duration_seconds = kDuration;
    result.inference_time_seconds = inference_time;
    return result;
}

// ---------------------------------------------------------------------------
// is_ready() / model_name()
// ---------------------------------------------------------------------------

bool VoxtralBackend::is_ready() const {
    return initialized_;
}

std::string VoxtralBackend::model_name() const {
    return "voxtral-4b";
}

}  // namespace tts
