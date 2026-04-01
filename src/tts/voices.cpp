/// @file voices.cpp
/// @brief VoiceCatalog implementation with all 20 preset voices.

#include "tts/voices.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace tts {

VoiceCatalog::VoiceCatalog() {
    // All 20 preset voices from voxtral-tts.c README.
    // Sorted by language, then by ID for deterministic iteration.
    voices_ = {
        // Arabic
        {"ar_male",         "Arabic Male",       "ar"},
        // German
        {"de_female",       "German Female",      "de"},
        {"de_male",         "German Male",        "de"},
        // English
        {"casual_female",   "Casual Female",      "en"},
        {"casual_male",     "Casual Male",        "en"},
        {"cheerful_female", "Cheerful Female",    "en"},
        {"neutral_female",  "Neutral Female",     "en"},
        {"neutral_male",    "Neutral Male",       "en"},
        // Spanish
        {"es_female",       "Spanish Female",     "es"},
        {"es_male",         "Spanish Male",       "es"},
        // French
        {"fr_female",       "French Female",      "fr"},
        {"fr_male",         "French Male",        "fr"},
        // Hindi
        {"hi_female",       "Hindi Female",       "hi"},
        {"hi_male",         "Hindi Male",         "hi"},
        // Italian
        {"it_female",       "Italian Female",     "it"},
        {"it_male",         "Italian Male",       "it"},
        // Dutch
        {"nl_female",       "Dutch Female",       "nl"},
        {"nl_male",         "Dutch Male",         "nl"},
        // Portuguese
        {"pt_female",       "Portuguese Female",  "pt"},
        {"pt_male",         "Portuguese Male",    "pt"},
    };
}

const Voice* VoiceCatalog::find(const std::string& id) const {
    auto it = std::ranges::find(voices_, id, &Voice::id);
    return it != voices_.end() ? &(*it) : nullptr;
}

const std::vector<Voice>& VoiceCatalog::all() const {
    return voices_;
}

std::vector<Voice> VoiceCatalog::by_language(const std::string& language) const {
    std::vector<Voice> result;
    std::ranges::copy_if(voices_, std::back_inserter(result),
                         [&language](const Voice& v) { return v.language == language; });
    return result;
}

bool VoiceCatalog::is_valid(const std::string& id) const {
    return find(id) != nullptr;
}

std::string VoiceCatalog::list_valid_ids() const {
    std::ostringstream oss;
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << voices_[i].id;
    }
    return oss.str();
}

}  // namespace tts
