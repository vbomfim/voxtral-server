/// @file test_voices.cpp
/// @brief Developer unit tests for VoiceCatalog.
///
/// Tests all 20 preset voices, lookup, filtering, and validation.

#include <gtest/gtest.h>

#include "tts/voices.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace tts;

// ============================================================================
// Catalog completeness
// ============================================================================

TEST(VoiceCatalog, HasExactly20Voices) {
    VoiceCatalog catalog;
    EXPECT_EQ(catalog.all().size(), 20U);
}

TEST(VoiceCatalog, AllVoiceIdsAreUnique) {
    VoiceCatalog catalog;
    auto voices = catalog.all();
    std::vector<std::string> ids;
    ids.reserve(voices.size());
    for (const auto& v : voices) {
        ids.push_back(v.id);
    }
    std::sort(ids.begin(), ids.end());
    auto it = std::unique(ids.begin(), ids.end());
    EXPECT_EQ(it, ids.end()) << "Duplicate voice IDs found";
}

// ============================================================================
// English voices (5)
// ============================================================================

TEST(VoiceCatalog, EnglishVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("casual_female"));
    EXPECT_TRUE(catalog.is_valid("casual_male"));
    EXPECT_TRUE(catalog.is_valid("cheerful_female"));
    EXPECT_TRUE(catalog.is_valid("neutral_female"));
    EXPECT_TRUE(catalog.is_valid("neutral_male"));
}

TEST(VoiceCatalog, EnglishVoiceCount) {
    VoiceCatalog catalog;
    auto en = catalog.by_language("en");
    EXPECT_EQ(en.size(), 5U);
}

// ============================================================================
// Non-English voices (15)
// ============================================================================

TEST(VoiceCatalog, FrenchVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("fr_female"));
    EXPECT_TRUE(catalog.is_valid("fr_male"));
    EXPECT_EQ(catalog.by_language("fr").size(), 2U);
}

TEST(VoiceCatalog, GermanVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("de_female"));
    EXPECT_TRUE(catalog.is_valid("de_male"));
    EXPECT_EQ(catalog.by_language("de").size(), 2U);
}

TEST(VoiceCatalog, SpanishVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("es_female"));
    EXPECT_TRUE(catalog.is_valid("es_male"));
    EXPECT_EQ(catalog.by_language("es").size(), 2U);
}

TEST(VoiceCatalog, ItalianVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("it_female"));
    EXPECT_TRUE(catalog.is_valid("it_male"));
    EXPECT_EQ(catalog.by_language("it").size(), 2U);
}

TEST(VoiceCatalog, PortugueseVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("pt_female"));
    EXPECT_TRUE(catalog.is_valid("pt_male"));
    EXPECT_EQ(catalog.by_language("pt").size(), 2U);
}

TEST(VoiceCatalog, DutchVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("nl_female"));
    EXPECT_TRUE(catalog.is_valid("nl_male"));
    EXPECT_EQ(catalog.by_language("nl").size(), 2U);
}

TEST(VoiceCatalog, ArabicVoicePresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("ar_male"));
    EXPECT_EQ(catalog.by_language("ar").size(), 1U);
}

TEST(VoiceCatalog, HindiVoicesPresent) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("hi_female"));
    EXPECT_TRUE(catalog.is_valid("hi_male"));
    EXPECT_EQ(catalog.by_language("hi").size(), 2U);
}

// ============================================================================
// find() behavior
// ============================================================================

TEST(VoiceCatalog, FindReturnsCorrectVoice) {
    VoiceCatalog catalog;
    const Voice* v = catalog.find("casual_female");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->id, "casual_female");
    EXPECT_EQ(v->name, "Casual Female");
    EXPECT_EQ(v->language, "en");
}

TEST(VoiceCatalog, FindReturnsNullptrForInvalid) {
    VoiceCatalog catalog;
    EXPECT_EQ(catalog.find("nonexistent_voice"), nullptr);
}

TEST(VoiceCatalog, FindReturnsNullptrForEmpty) {
    VoiceCatalog catalog;
    EXPECT_EQ(catalog.find(""), nullptr);
}

TEST(VoiceCatalog, FindFrenchFemale) {
    VoiceCatalog catalog;
    const Voice* v = catalog.find("fr_female");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->name, "French Female");
    EXPECT_EQ(v->language, "fr");
}

// ============================================================================
// is_valid() behavior
// ============================================================================

TEST(VoiceCatalog, IsValidForValidIds) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.is_valid("neutral_female"));
    EXPECT_TRUE(catalog.is_valid("ar_male"));
    EXPECT_TRUE(catalog.is_valid("de_male"));
}

TEST(VoiceCatalog, IsValidReturnsFalseForInvalid) {
    VoiceCatalog catalog;
    EXPECT_FALSE(catalog.is_valid("not_a_voice"));
    EXPECT_FALSE(catalog.is_valid(""));
    EXPECT_FALSE(catalog.is_valid("NEUTRAL_FEMALE"));  // case-sensitive
}

// ============================================================================
// by_language() behavior
// ============================================================================

TEST(VoiceCatalog, ByLanguageReturnsEmptyForUnknown) {
    VoiceCatalog catalog;
    auto voices = catalog.by_language("zh");
    EXPECT_TRUE(voices.empty());
}

TEST(VoiceCatalog, ByLanguageEnglishContainsAllFive) {
    VoiceCatalog catalog;
    auto en = catalog.by_language("en");
    ASSERT_EQ(en.size(), 5U);

    std::vector<std::string> ids;
    for (const auto& v : en) {
        ids.push_back(v.id);
        EXPECT_EQ(v.language, "en");  // All must be English
    }

    // Verify all 5 English voices are present
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<std::string>{
        "casual_female", "casual_male", "cheerful_female",
        "neutral_female", "neutral_male"
    }));
}

// ============================================================================
// list_valid_ids() behavior
// ============================================================================

TEST(VoiceCatalog, ListValidIdsContainsAllIds) {
    VoiceCatalog catalog;
    auto list = catalog.list_valid_ids();

    // Every voice ID must appear in the comma-separated list
    for (const auto& v : catalog.all()) {
        EXPECT_NE(list.find(v.id), std::string::npos)
            << "Voice ID '" << v.id << "' not found in list_valid_ids()";
    }
}

TEST(VoiceCatalog, ListValidIdsIsCommaSeparated) {
    VoiceCatalog catalog;
    auto list = catalog.list_valid_ids();

    // 20 IDs separated by ", " means 19 occurrences of ", "
    size_t comma_count = 0;
    size_t pos = 0;
    while ((pos = list.find(", ", pos)) != std::string::npos) {
        ++comma_count;
        pos += 2;
    }
    EXPECT_EQ(comma_count, 19U);
}
