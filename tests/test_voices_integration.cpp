/// @file test_voices_integration.cpp
/// @brief QA integration and edge-case tests for VoiceCatalog.
///
/// Covers: metadata completeness, language code consistency,
/// deterministic ordering, concurrent read safety, catalog-to-backend
/// integration, voice name format validation.
/// Tags: [CONTRACT], [EDGE], [COVERAGE], [INTEGRATION]

#include <gtest/gtest.h>

#include "tts/voices.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace tts;

// ============================================================================
// [CONTRACT] Voice metadata completeness — every voice must have
// non-empty id, name, and language. A missing field would cause
// silent errors in voice lookup or API responses.
// ============================================================================

TEST(VoiceCatalogContract, AllVoicesHaveNonEmptyFields) {
    VoiceCatalog catalog;
    for (const auto& v : catalog.all()) {
        EXPECT_FALSE(v.id.empty())
            << "Voice has empty id (name=" << v.name << ")";
        EXPECT_FALSE(v.name.empty())
            << "Voice has empty name (id=" << v.id << ")";
        EXPECT_FALSE(v.language.empty())
            << "Voice has empty language (id=" << v.id << ")";
    }
}

// ============================================================================
// [CONTRACT] Language codes are valid ISO 639-1 (2 lowercase letters).
// ============================================================================

TEST(VoiceCatalogContract, LanguageCodesAreISO639_1) {
    VoiceCatalog catalog;
    // Known valid 2-letter language codes used in the catalog
    std::set<std::string> valid_codes = {
        "ar", "de", "en", "es", "fr", "hi", "it", "nl", "pt"
    };

    for (const auto& v : catalog.all()) {
        EXPECT_EQ(v.language.size(), 2U)
            << "Language code '" << v.language << "' for voice '"
            << v.id << "' is not 2 characters";
        EXPECT_TRUE(std::islower(static_cast<unsigned char>(v.language[0])))
            << "Language code '" << v.language << "' first char not lowercase";
        EXPECT_TRUE(std::islower(static_cast<unsigned char>(v.language[1])))
            << "Language code '" << v.language << "' second char not lowercase";
        EXPECT_TRUE(valid_codes.count(v.language) > 0)
            << "Unknown language code '" << v.language << "' for voice '"
            << v.id << "'";
    }
}

// ============================================================================
// [CONTRACT] All 9 languages are represented (ar, de, en, es, fr, hi, it, nl, pt)
// ============================================================================

TEST(VoiceCatalogContract, AllNineLanguagesPresent) {
    VoiceCatalog catalog;
    std::set<std::string> languages;
    for (const auto& v : catalog.all()) {
        languages.insert(v.language);
    }

    EXPECT_EQ(languages.size(), 9U);
    EXPECT_TRUE(languages.count("ar"));
    EXPECT_TRUE(languages.count("de"));
    EXPECT_TRUE(languages.count("en"));
    EXPECT_TRUE(languages.count("es"));
    EXPECT_TRUE(languages.count("fr"));
    EXPECT_TRUE(languages.count("hi"));
    EXPECT_TRUE(languages.count("it"));
    EXPECT_TRUE(languages.count("nl"));
    EXPECT_TRUE(languages.count("pt"));
}

// ============================================================================
// [CONTRACT] Voice IDs contain only safe characters (lowercase, underscores)
// This is important for URLs, JSON, and filesystem paths.
// ============================================================================

TEST(VoiceCatalogContract, VoiceIdsAreSafeIdentifiers) {
    VoiceCatalog catalog;
    for (const auto& v : catalog.all()) {
        for (char c : v.id) {
            EXPECT_TRUE(std::islower(static_cast<unsigned char>(c)) || c == '_')
                << "Voice id '" << v.id << "' contains unsafe char '"
                << c << "' (only lowercase + underscore allowed)";
        }
    }
}

// ============================================================================
// [CONTRACT] all() returns deterministic order — same order every time.
// Important for pagination and list_valid_ids() consistency.
// ============================================================================

TEST(VoiceCatalogContract, AllReturnsDeterministicOrder) {
    VoiceCatalog cat1;
    VoiceCatalog cat2;

    auto v1 = cat1.all();
    auto v2 = cat2.all();

    ASSERT_EQ(v1.size(), v2.size());
    for (size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v1[i].id, v2[i].id)
            << "Order mismatch at index " << i;
    }
}

// ============================================================================
// [CONTRACT] by_language() returns only voices of that language.
// ============================================================================

TEST(VoiceCatalogContract, ByLanguageReturnsOnlyMatchingVoices) {
    VoiceCatalog catalog;

    // Check every language individually
    std::set<std::string> languages;
    for (const auto& v : catalog.all()) {
        languages.insert(v.language);
    }

    for (const auto& lang : languages) {
        auto filtered = catalog.by_language(lang);
        EXPECT_FALSE(filtered.empty())
            << "by_language('" << lang << "') returned empty";
        for (const auto& v : filtered) {
            EXPECT_EQ(v.language, lang)
                << "Voice '" << v.id << "' has language '" << v.language
                << "' but was returned by by_language('" << lang << "')";
        }
    }
}

// ============================================================================
// [CONTRACT] Sum of per-language counts equals total count.
// ============================================================================

TEST(VoiceCatalogContract, PerLanguageCountsSumToTotal) {
    VoiceCatalog catalog;

    std::set<std::string> languages;
    for (const auto& v : catalog.all()) {
        languages.insert(v.language);
    }

    size_t sum = 0;
    for (const auto& lang : languages) {
        sum += catalog.by_language(lang).size();
    }

    EXPECT_EQ(sum, catalog.all().size())
        << "Per-language counts don't sum to total";
}

// ============================================================================
// [EDGE] find() with case variations — must be case-sensitive.
// ============================================================================

TEST(VoiceCatalogEdge, FindIsCaseSensitive) {
    VoiceCatalog catalog;

    EXPECT_NE(catalog.find("casual_female"), nullptr);   // exact match
    EXPECT_EQ(catalog.find("Casual_Female"), nullptr);   // mixed case
    EXPECT_EQ(catalog.find("CASUAL_FEMALE"), nullptr);   // upper case
    EXPECT_EQ(catalog.find("casual_Female"), nullptr);   // partial upper
}

// ============================================================================
// [EDGE] by_language() with case variations — must be case-sensitive.
// ============================================================================

TEST(VoiceCatalogEdge, ByLanguageIsCaseSensitive) {
    VoiceCatalog catalog;

    EXPECT_FALSE(catalog.by_language("en").empty());  // correct
    EXPECT_TRUE(catalog.by_language("EN").empty());   // upper case
    EXPECT_TRUE(catalog.by_language("En").empty());   // mixed case
}

// ============================================================================
// [EDGE] find() with whitespace and special chars
// ============================================================================

TEST(VoiceCatalogEdge, FindWithWhitespace) {
    VoiceCatalog catalog;

    EXPECT_EQ(catalog.find(" casual_female"), nullptr);   // leading space
    EXPECT_EQ(catalog.find("casual_female "), nullptr);   // trailing space
    EXPECT_EQ(catalog.find("casual female"), nullptr);    // space instead of _
    EXPECT_EQ(catalog.find("casual\tfemale"), nullptr);   // tab
    EXPECT_EQ(catalog.find("casual\nfemale"), nullptr);   // newline
}

// ============================================================================
// [EDGE] by_language() with empty and unusual input
// ============================================================================

TEST(VoiceCatalogEdge, ByLanguageEmptyString) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.by_language("").empty());
}

TEST(VoiceCatalogEdge, ByLanguageLongString) {
    VoiceCatalog catalog;
    EXPECT_TRUE(catalog.by_language("english").empty());  // not ISO code
}

// ============================================================================
// [COVERAGE] Concurrent reads from multiple threads
// VoiceCatalog is documented as thread-safe for reads. Verify no
// data races under concurrent access.
// ============================================================================

TEST(VoiceCatalogIntegration, ConcurrentReadsAreThreadSafe) {
    VoiceCatalog catalog;

    constexpr int kThreads = 8;
    constexpr int kReadsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    std::atomic<int> total_found{0};
    std::atomic<int> total_all_size{0};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&catalog, &total_found, &total_all_size]() {
            for (int i = 0; i < kReadsPerThread; ++i) {
                // Mix of different operations
                auto all = catalog.all();
                total_all_size.fetch_add(static_cast<int>(all.size()),
                                         std::memory_order_relaxed);

                auto en = catalog.by_language("en");
                const Voice* v = catalog.find("casual_female");
                if (v != nullptr) {
                    total_found.fetch_add(1, std::memory_order_relaxed);
                }

                bool valid = catalog.is_valid("neutral_male");
                if (valid) {
                    total_found.fetch_add(1, std::memory_order_relaxed);
                }

                (void)catalog.list_valid_ids();
                (void)en;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Every read of all() must return 20
    EXPECT_EQ(total_all_size.load(), kThreads * kReadsPerThread * 20);

    // find + is_valid each found once per iteration = 2 * iterations
    EXPECT_EQ(total_found.load(), kThreads * kReadsPerThread * 2);
}

// ============================================================================
// [CONTRACT] list_valid_ids() contains every voice ID and no extras
// ============================================================================

TEST(VoiceCatalogContract, ListValidIdsExactMatch) {
    VoiceCatalog catalog;
    auto list = catalog.list_valid_ids();

    // Parse the comma-separated list back
    std::vector<std::string> parsed;
    std::string current;
    for (char c : list) {
        if (c == ',') {
            // trim leading space
            if (!current.empty() && current[0] == ' ') {
                current = current.substr(1);
            }
            parsed.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        if (current[0] == ' ') {
            current = current.substr(1);
        }
        parsed.push_back(current);
    }

    // Must match all() exactly
    auto all = catalog.all();
    ASSERT_EQ(parsed.size(), all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(parsed[i], all[i].id)
            << "Mismatch at index " << i;
    }
}

// ============================================================================
// [COVERAGE] Voice name format — names should be human-readable
// (e.g., "French Female" not "fr_female")
// ============================================================================

TEST(VoiceCatalogContract, VoiceNamesAreHumanReadable) {
    VoiceCatalog catalog;
    for (const auto& v : catalog.all()) {
        // Names should not contain underscores (that's the ID format)
        EXPECT_EQ(v.name.find('_'), std::string::npos)
            << "Voice name '" << v.name << "' contains underscore (id=" << v.id << ")";

        // Names should start with an uppercase letter
        EXPECT_TRUE(std::isupper(static_cast<unsigned char>(v.name[0])))
            << "Voice name '" << v.name << "' does not start with uppercase";

        // Names should be at least 2 words (e.g., "Arabic Male")
        EXPECT_NE(v.name.find(' '), std::string::npos)
            << "Voice name '" << v.name << "' appears to be single word";
    }
}
