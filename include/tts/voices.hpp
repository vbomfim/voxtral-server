#pragma once

/// @file voices.hpp
/// @brief Voice catalog management for supported TTS voices.
///
/// The VoiceCatalog holds all 20 preset voices from voxtral-tts.c and
/// provides lookup, filtering, and validation.

#include <string>
#include <vector>

namespace tts {

/// Metadata for a single TTS voice preset.
struct Voice {
    std::string id;        // e.g. "casual_female"
    std::string name;      // e.g. "Casual Female"
    std::string language;  // ISO 639-1 code, e.g. "en"
};

/// Immutable catalog of all supported TTS voices.
///
/// Constructed once at startup with all 20 preset voices.
/// Thread-safe for concurrent reads (no mutation after construction).
class VoiceCatalog {
public:
    /// Construct the catalog with all 20 preset voices.
    VoiceCatalog();

    /// Find a voice by ID. Returns nullptr if not found.
    [[nodiscard]] const Voice* find(const std::string& id) const;

    /// Return all voices in the catalog.
    [[nodiscard]] std::vector<Voice> all() const;

    /// Return all voices matching the given language code.
    [[nodiscard]] std::vector<Voice> by_language(const std::string& language) const;

    /// Check if a voice ID exists in the catalog.
    [[nodiscard]] bool is_valid(const std::string& id) const;

    /// Comma-separated list of all valid voice IDs (for error messages).
    [[nodiscard]] std::string list_valid_ids() const;

private:
    std::vector<Voice> voices_;
};

}  // namespace tts
