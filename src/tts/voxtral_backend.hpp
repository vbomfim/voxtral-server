#pragma once

/// @file voxtral_backend.hpp
/// @brief VoxtralBackend — adapter wrapping voxtral-tts.c inference engine.
///
/// When compiled with TTS_HAS_VOXTRAL_ENGINE (real voxtral-tts.c linked),
/// the backend loads the model via tts_load() and runs real TTS inference.
/// If the model fails to load (e.g. wrong path), it falls back to a silent
/// stub so that tests still pass without a 7.5GB model on disk.
///
/// When compiled without TTS_HAS_VOXTRAL_ENGINE (or with TTS_USE_STUB_BACKEND),
/// the backend always produces 1 second of silence — the original stub behavior.

#include "tts/backend.hpp"

#include <string>

namespace tts {

/// Concrete ITtsBackend that wraps voxtral-tts.c.
///
/// Produces real speech when the engine is linked and model is loaded,
/// or silent WAV audio when running in stub/fallback mode.
class VoxtralBackend : public ITtsBackend {
public:
    ~VoxtralBackend() override;

    [[nodiscard]] bool initialize(const std::string& model_path) override;
    [[nodiscard]] SynthesisResult synthesize(const SynthesisRequest& request) override;
    [[nodiscard]] bool is_ready() const override;
    [[nodiscard]] std::string model_name() const override;

private:
    bool initialized_ = false;
    std::string model_path_;

#ifdef TTS_HAS_VOXTRAL_ENGINE
    void* ctx_ = nullptr;           ///< tts_ctx_t* — opaque pointer to C engine context
    bool use_real_engine_ = false;   ///< true if tts_load() succeeded
#endif
};

}  // namespace tts
