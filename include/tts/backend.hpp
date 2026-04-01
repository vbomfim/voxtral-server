#pragma once

/// @file backend.hpp
/// @brief Abstract TTS backend interface (Port in Hexagonal Architecture).
///
/// All TTS engines implement ITtsBackend. The rest of the system depends
/// only on this interface, never on a concrete engine — enabling stub,
/// mock, and real implementations to be swapped freely.
///
/// Pattern mirrors openasr/whisperx-streaming-server ITranscriptionBackend.

#include <cstdint>
#include <string>
#include <vector>

namespace tts {

/// Parameters for a single text-to-speech synthesis request.
struct SynthesisRequest {
    std::string text;
    std::string voice;
    std::string model;
    std::string response_format = "wav";  // "wav" for now
    float speed = 1.0F;
};

/// Result of a synthesis operation, including audio payload and timing.
struct SynthesisResult {
    std::vector<uint8_t> audio_data;     // WAV bytes (header + PCM data)
    int sample_rate = 24000;             // Output sample rate in Hz
    double duration_seconds = 0.0;       // Audio duration in seconds
    double inference_time_seconds = 0.0; // Wall-clock inference time
};

/// Abstract interface for TTS inference backends.
///
/// Implementations:
///   - VoxtralBackend  — stub (will wrap voxtral-tts.c when submodule lands)
///   - MockBackend      — configurable mock for testing
class ITtsBackend {
public:
    virtual ~ITtsBackend() = default;

    /// Load the model from disk. Returns true on success.
    /// @param model_path  Filesystem path to the model file.
    [[nodiscard]] virtual bool initialize(const std::string& model_path) = 0;

    /// Run TTS inference on the given request.
    /// @pre is_ready() == true
    [[nodiscard]] virtual SynthesisResult synthesize(const SynthesisRequest& request) = 0;

    /// Whether the backend is initialized and ready for inference.
    [[nodiscard]] virtual bool is_ready() const = 0;

    /// Human-readable model identifier (e.g. "voxtral-4b").
    [[nodiscard]] virtual std::string model_name() const = 0;
};

}  // namespace tts
