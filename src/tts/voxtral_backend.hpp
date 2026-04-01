#pragma once

/// @file voxtral_backend.hpp
/// @brief VoxtralBackend — stub adapter for voxtral-tts.c.
///
/// TODO: Replace stub with real voxtral-tts.c calls when submodule is added.
///
/// The stub produces valid WAV files (24kHz, 16-bit mono, silence) so that
/// the full pipeline — queue → inference → HTTP response — can be tested
/// end-to-end before the real model is integrated.

#include "tts/backend.hpp"

#include <string>

namespace tts {

/// Concrete ITtsBackend that wraps voxtral-tts.c.
///
/// Current state: **STUB** — generates silent WAV audio.
/// When the voxtral-tts.c submodule is added, replace the stub
/// implementations in voxtral_backend.cpp with real API calls.
class VoxtralBackend : public ITtsBackend {
public:
    [[nodiscard]] bool initialize(const std::string& model_path) override;
    [[nodiscard]] SynthesisResult synthesize(const SynthesisRequest& request) override;
    [[nodiscard]] bool is_ready() const override;
    [[nodiscard]] std::string model_name() const override;

private:
    bool initialized_ = false;
    std::string model_path_;
};

}  // namespace tts
